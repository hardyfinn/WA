#include "Engine.hpp"
#include "Generator.hpp"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace wa {

const char* PAGE_NAMES[NUM_PAGES] = {"LANES", "STEP", "PATTERN", "ROLE", "RULES", "ANSWER", "MORPH", "MATRIX", "SONG", "SCALE"};

namespace {

template <typename T>
T clampv(T x, T a, T b) {
	return x < a ? a : (x > b ? b : x);
}

int64_t floorMod64(int64_t a, int64_t b) {
	int64_t m = a % b;
	return m < 0 ? m + b : m;
}

double swingWarp(double T, int swing) {
	double s = clampv(swing, 50, 75) / 100.0;
	if (s <= 0.5)
		return T;
	double p = std::floor(T / 2.0);
	double f = T - 2.0 * p;
	double tw = (f < 2.0 * s) ? f / (2.0 * s) : 1.0 + (f - 2.0 * s) / (2.0 - 2.0 * s);
	return 2.0 * p + tw;
}

const float DEFAULT_POINTS[NPOINT][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0.5f, 0}, {1, 0.5f}, {0.5f, 1}, {0, 0.5f}};

int cycle(int v, int d, int n) {
	int r = (v + d) % n;
	return r < 0 ? r + n : r;
}

} // namespace

// ---------------------------------------------------------------------------

Engine::Engine() {
	for (int t = 0; t < NT; t++)
		tr[t].frames = new FreezeFrame[FREEZE_CAP]();
	gestBuf = new float[60000]();
	initialize();
}

Engine::~Engine() {
	for (int t = 0; t < NT; t++)
		delete[] tr[t].frames;
	delete[] gestBuf;
}

void Engine::initialize() {
	for (int t = 0; t < NT; t++) {
		for (int s = 0; s < NSLOT; s++) {
			slots[t][s].clear();
			keepStamp[t][s] = 0;
		}
		edit[t].clear();
		edit[t].seed = (uint16_t) (1 + t);
		ts[t] = TrackSettings();
		ts[t].used[0] = true;
		ts[t].slot = 0;
		slots[t][0] = edit[t];
		home[t] = edit[t];
		undoHead[t] = undoCount[t] = 0;
		genCount[t] = 0;
		dirty[t] = true;
		tr[t].queued = -1;
	}
	// A gentle default so the module makes sound when first patched: gates on the beat
	for (int t = 0; t < NT; t++) {
		for (int s = 0; s < NS; s += 4)
			edit[t].steps[L_GATE][s].value = (t == 0) ? 1.f : 0.f;
		for (int s = 0; s < NS; s++)
			edit[t].steps[L_GATE][s].role = (s % 4 == 0) ? Role::Anchor : ((s % 2 == 0) ? Role::Pulse : Role::Passing);
		slots[t][0] = edit[t];
		home[t] = edit[t];
	}
	for (int i = 0; i < NSONG; i++)
		songs[i] = Song();
	scales = ScaleSet();
	opt = Options();
	songOn = false;
	songIdx = songRow = songRepeat = 0;
	selTrack = selLane = 0;
	page = P_LANES;
	selStep = 0;
	selPoint = -1;
	hi = false;
	stampCounter = 1;
	resetAll();
	for (int t = 0; t < NT; t++)
		rebuildPlay(t);
}

void Engine::randomizeAll(float character) {
	for (int t = 0; t < NT; t++) {
		pushUndo(t);
		Rng rng = Rng::fork(edit[t].seed, (uint64_t) t, 99, genCount[t]++);
		genTrack(edit[t], character, 1.f, rng);
		markEdited(t);
	}
}

void Engine::setMsg(const char* m) {
	std::snprintf(msg, sizeof(msg), "%s", m);
	msgTime = 2.5f;
}

void Engine::markEdited(int t) {
	home[t] = edit[t];
	dirty[t] = true;
}

void Engine::pushUndo(int t) {
	undoBuf[t][undoHead[t]] = edit[t];
	undoHead[t] = (undoHead[t] + 1) % NUNDO;
	if (undoCount[t] < NUNDO)
		undoCount[t]++;
}

void Engine::undo(int t) {
	if (undoCount[t] <= 0) {
		setMsg("NOTHING TO UNDO");
		return;
	}
	undoHead[t] = (undoHead[t] + NUNDO - 1) % NUNDO;
	edit[t] = undoBuf[t][undoHead[t]];
	undoCount[t]--;
	markEdited(t);
	editKey = -1;
	setMsg("UNDO");
}

int Engine::keepTrack(int t) {
	int slot = -1;
	for (int s = 0; s < NSLOT; s++)
		if (!ts[t].used[s]) {
			slot = s;
			break;
		}
	if (slot < 0) {
		uint32_t oldest = 0xFFFFFFFF;
		for (int s = 0; s < NSLOT; s++) {
			if (ts[t].locked[s] || s == ts[t].slot)
				continue;
			if (keepStamp[t][s] < oldest) {
				oldest = keepStamp[t][s];
				slot = s;
			}
		}
	}
	if (slot < 0) {
		setMsg("ALL SLOTS LOCKED");
		return -1;
	}
	Pattern& P = slots[t][slot];
	P = play[t];
	// play from where it was kept
	for (int l = 0; l < NL; l++)
		P.lanes[l].start = (uint8_t) tr[t].lanes[l].curIdx;
	P.answer.mode = AnswerMode::Off;
	ts[t].used[slot] = true;
	keepStamp[t][slot] = stampCounter++;
	// add to the morph map if there is a free point
	bool present = false;
	for (int j = 0; j < NPOINT; j++)
		if (ts[t].points[j].active && ts[t].points[j].slot == slot)
			present = true;
	if (!present) {
		for (int j = 0; j < NPOINT; j++) {
			if (!ts[t].points[j].active) {
				ts[t].points[j].active = true;
				ts[t].points[j].slot = (int8_t) slot;
				ts[t].points[j].x = DEFAULT_POINTS[j][0];
				ts[t].points[j].y = DEFAULT_POINTS[j][1];
				break;
			}
		}
	}
	char b[40];
	std::snprintf(b, sizeof(b), "T%d KEPT -> %d", t + 1, slot + 1);
	setMsg(b);
	return slot;
}

void Engine::loadSlot(int t, int slot) {
	if (slot < 0 || slot >= NSLOT)
		return;
	int cur = ts[t].slot;
	if (cur >= 0 && cur < NSLOT && ts[t].used[cur])
		slots[t][cur] = edit[t];
	if (!ts[t].used[slot]) {
		slots[t][slot].clear();
		slots[t][slot].seed = edit[t].seed;
		slots[t][slot].scale = edit[t].scale;
		slots[t][slot].root = edit[t].root;
		ts[t].used[slot] = true;
		keepStamp[t][slot] = stampCounter++;
	}
	edit[t] = slots[t][slot];
	ts[t].slot = (int8_t) slot;
	markEdited(t);
	rebuildPlay(t);
}

void Engine::resetTrack(int t) {
	TrackRun& r = tr[t];
	for (int l = 0; l < NL; l++) {
		LaneRun& lr = r.lanes[l];
		float val = lr.val;
		lr = LaneRun();
		lr.val = val;
	}
	r.nFires = 0;
	r.rng.reseed((uint64_t) edit[t].seed * 7919u + (uint64_t) t);
}

void Engine::resetAll() {
	for (int t = 0; t < NT; t++) {
		TrackRun& r = tr[t];
		r.T = r.Tw = r.dTw = 0.0;
		r.pulses = -1;
		r.started = false;
		r.lastBase = r.lastBeat = r.lastBar = r.lastSync = -1;
		r.frozen = false;
		resetTrack(t);
	}
}

bool Engine::answerLane(int t, int l) const {
	return t > 0 && edit[t].answer.mode != AnswerMode::Off && (l == L_GATE || l == L_NOTE || l == L_OCT);
}

void Engine::rebuildPlay(int t) {
	TrackSettings& s = ts[t];
	bool morphed = false;
	if (s.morphOn) {
		const Pattern* pats[NPOINT] = {};
		int n = 0;
		for (int j = 0; j < NPOINT; j++) {
			if (s.points[j].active && s.points[j].slot >= 0 && s.used[(int) s.points[j].slot]) {
				pats[j] = &slots[t][(int) s.points[j].slot];
				n++;
			}
		}
		if (n > 0) {
			MorphPoint pts[NPOINT];
			for (int j = 0; j < NPOINT; j++) {
				pts[j] = s.points[j];
				if (!pats[j])
					pts[j].active = false;
			}
			float x = lastMx[t] >= 0 ? lastMx[t] : s.mx;
			float y = lastMy[t] >= 0 ? lastMy[t] : s.my;
			morphWeights(pts, x, y, morphW[t]);
			play[t] = edit[t];
			morphRender(play[t], pats, morphW[t], (uint64_t) edit[t].seed * 31u + (uint64_t) t);
			morphed = true;
		}
	}
	if (!morphed) {
		play[t] = edit[t];
		for (int j = 0; j < NPOINT; j++)
			morphW[t][j] = 0.f;
	}
	if (t > 0 && edit[t].answer.mode != AnswerMode::Off) {
		play[t].answer = edit[t].answer;
		int src = clampv<int>(edit[t].answer.source, 0, t - 1);
		deriveAnswer(play[t], play[src], getScale(play[t].scale, scales).n, (uint64_t) edit[t].seed);
		answerSeen[t] = playVersion[src];
	}
	// auto poly channels: largest chord in the chord loop
	int mx = 1;
	const LaneSettings& cl = play[t].lanes[L_CHORD];
	for (int i = 0; i < cl.length; i++)
		mx = std::max(mx, chordSize((int) play[t].steps[L_CHORD][(cl.start + i) % NS].value, scales));
	tr[t].chanAuto = mx;
	playVersion[t]++;
	dirty[t] = false;
}

// ---------------------------------------------------------------------------
// Matrix (§9)

float Engine::srcValue(int code, const EngineIn& in) {
	if (code <= 0)
		return 0.f;
	if (code <= NT * NL) {
		int t = (code - 1) / NL, l = (code - 1) % NL;
		return normValue(l, tr[t].lanes[l].val);
	}
	int si = code - 1 - NT * NL;
	if (si < NSRC && in.expander)
		return clampv(in.src[si] / 10.f, 0.f, 1.f);
	return 0.f;
}

int Engine::effLen(int t, int l, const EngineIn& in) {
	int L = play[t].lanes[l].length;
	for (const MatrixSlot& m : play[t].matrix)
		if (m.source && m.target == l && m.action == MatrixAction::Length)
			L += (int) std::lround(srcValue(m.source, in) * m.amount * 8.f);
	return clampv(L, 1, NS);
}

int Engine::matrixOffset(int t, int l, int L, const EngineIn& in) {
	int off = 0;
	for (const MatrixSlot& m : play[t].matrix)
		if (m.source && m.target == l && m.action == MatrixAction::Offset)
			off += (int) std::lround(srcValue(m.source, in) * m.amount * L);
	return off;
}

int Engine::matrixClockShift(int t, int l, const EngineIn& in) {
	int sh = 0;
	for (const MatrixSlot& m : play[t].matrix)
		if (m.source && m.target == l && m.action == MatrixAction::Clock && srcValue(m.source, in) > 0.5f)
			sh += m.amount >= 0.f ? 1 : -1;
	return sh;
}

bool Engine::matrixMute(int t, int l, const EngineIn& in) {
	for (const MatrixSlot& m : play[t].matrix) {
		if (!m.source || m.target != l || m.action != MatrixAction::Mute)
			continue;
		float v = srcValue(m.source, in);
		if (m.amount >= 0.f ? v > 0.5f : v < 0.5f)
			return true;
	}
	return false;
}

int Engine::matrixTranspose(int t, const EngineIn& in) {
	int tr_ = 0;
	for (const MatrixSlot& m : play[t].matrix)
		if (m.source && m.target == L_NOTE && m.action == MatrixAction::Transpose)
			tr_ += (int) std::lround(srcValue(m.source, in) * m.amount * 14.f);
	return tr_;
}

float Engine::laneRatio(int t, int l, const EngineIn& in) {
	int idx = clampv(play[t].lanes[l].ratio + (int) matrixClockShift(t, l, in), 0, NRATIOS - 1);
	float r = RATIOS[idx];
	if (l > 0) {
		float delta = ((l % 2) ? -1.f : 1.f) / (float) (l + 2);
		r *= 1.f + ts[t].spread * delta;
	}
	return r;
}

float Engine::arcValue(int t, const EngineIn& in) const {
	return clampv(ts[t].arc + in.arcCV[t] / 10.f, 0.f, 1.f);
}

float Engine::densityValue(int t, const EngineIn& in) const {
	return clampv(ts[t].density + (in.expander ? in.densCV[t] / 5.f : 0.f), -1.f, 1.f);
}

// ---------------------------------------------------------------------------
// Lanes (§6.4)

int Engine::position(int t, int l, int64_t mm, int L) {
	const Pattern& P = play[t];
	const LaneSettings& ls = P.lanes[l];
	LaneRun& lr = tr[t].lanes[l];
	int i = 0;
	if (P.pathLen[l] > 0 && (ls.dir == Dir::Random || ls.dir == Dir::Drunk)) {
		i = P.path[l][floorMod64(mm, P.pathLen[l])] % L;
	}
	else {
		switch (ls.dir) {
			case Dir::Forward: i = (int) floorMod64(mm, L); break;
			case Dir::Reverse: i = L - 1 - (int) floorMod64(mm, L); break;
			case Dir::Pendulum: {
				if (L < 2) {
					i = 0;
					break;
				}
				int64_t per = 2 * L - 2;
				int j = (int) floorMod64(mm, per);
				i = j < L ? j : (int) per - j;
				break;
			}
			case Dir::Skip: i = (int) floorMod64(mm * (int64_t) clampv<int>(ls.skip, 2, 7), L); break;
			case Dir::Random: {
				if (L < 2) {
					i = 0;
					break;
				}
				i = tr[t].rng.uniformInt(L - 1);
				if (i >= lr.prevI)
					i++;
				break;
			}
			case Dir::Drunk: {
				int d = tr[t].rng.uniform() < 0.5f ? -1 : 1;
				i = (int) floorMod64(lr.prevI + d, L);
				break;
			}
			default: break;
		}
	}
	if (lr.reversed)
		i = L - 1 - i;
	i = clampv(i, 0, L - 1);
	lr.prevI = i;
	return i;
}

void Engine::decide(int t, int l, int64_t m, const EngineIn& in) {
	Pattern& P = play[t];
	const LaneSettings& ls = P.lanes[l];
	LaneRun& lr = tr[t].lanes[l];
	int L = effLen(t, l, in);

	// matrix direction flips
	for (int q = 0; q < NMATRIX; q++) {
		const MatrixSlot& ms = P.matrix[q];
		if (!ms.source || ms.target != l || ms.action != MatrixAction::Direction)
			continue;
		float v = srcValue(ms.source, in);
		if (lr.dirPrev[q] < 0.5f && v >= 0.5f)
			lr.reversed = !lr.reversed;
		lr.dirPrev[q] = v;
	}

	int64_t mm = m + matrixOffset(t, l, L, in);
	int i = position(t, l, mm, L);
	int idx = (ls.start + i) % NS;
	const Step& s = P.steps[l][idx];
	bool passed;
	if (s.pin == Pin::Fired)
		passed = true;
	else if (s.pin == Pin::NotFired)
		passed = false;
	else {
		int64_t pass = m / L;
		bool c = true;
		switch (s.cond) {
			case Cond::Always: c = true; break;
			case Cond::AofB: {
				int B = clampv<int>(s.condB, 1, 8);
				int A = clampv<int>(s.condA, 1, B);
				c = floorMod64(pass, B) == A - 1;
				break;
			}
			case Cond::First: c = pass == 0; break;
			case Cond::NotFirst: c = pass != 0; break;
			case Cond::Fill: c = fillActive; break;
			case Cond::NotFill: c = !fillActive; break;
			case Cond::Pre: c = lr.prevCond; break;
			case Cond::NotPre: c = !lr.prevCond; break;
			default: break;
		}
		if (s.cond != Cond::Pre && s.cond != Cond::NotPre && s.cond != Cond::Always)
			lr.prevCond = c;
		float prob = s.prob + (fillActive ? opt.fillAmount : 0.f);
		bool rolled = s.prob < 100;
		passed = c && (prob >= 100.f || tr[t].rng.uniform() * 100.f < prob);
		// pinned dice (§7.1.3)
		if (pinActive && t == selTrack && rolled) {
			Pin pn = passed ? Pin::Fired : Pin::NotFired;
			P.steps[l][idx].pin = pn;
			edit[t].steps[l][idx].pin = pn;
			home[t].steps[l][idx].pin = pn;
		}
	}
	// pinned path recording
	if (ls.dir == Dir::Random || ls.dir == Dir::Drunk) {
		if (pinActive && t == selTrack && P.pathLen[l] == 0) {
			if (!lr.pathRec && floorMod64(m, L) == 0) {
				lr.pathRec = true;
				lr.pathCount = 0;
			}
			if (lr.pathRec) {
				lr.pathBuf[lr.pathCount++] = (uint8_t) i;
				if (lr.pathCount >= L) {
					lr.pathRec = false;
					for (Pattern* pp : {&P, &edit[t], &home[t]}) {
						pp->pathLen[l] = (uint8_t) L;
						for (int k = 0; k < L; k++)
							pp->path[l][k] = lr.pathBuf[k];
					}
					setMsg("PATH PINNED");
				}
			}
		}
		else
			lr.pathRec = false;
	}
	lr.haveNext = true;
	lr.nextI = i;
	lr.nextIdx = idx;
	lr.nextPassed = passed;
}

void Engine::advanceLane(int t, int l, const EngineIn& in) {
	TrackRun& r = tr[t];
	LaneRun& lr = r.lanes[l];
	Pattern& P = play[t];
	lr.moves++;
	if (!lr.haveNext)
		decide(t, l, lr.moves, in);
	lr.curI = lr.nextI;
	lr.curIdx = lr.nextIdx;
	lr.curPassed = lr.nextPassed;
	lr.haveNext = false;
	int L = effLen(t, l, in);
	bool atStart = floorMod64(lr.moves, L) == 0;
	if (atStart)
		passStart(t, l, lr.moves > 0, in);
	const Step& s = P.steps[l][lr.curIdx];
	bool eventDriven = P.lanes[l].clock != ClockSrc::Track;

	// look-ahead decision for the next step
	decide(t, l, lr.moves + 1, in);

	if (l == L_GATE) {
		lr.val = (lr.curPassed && s.value > 0.5f) ? 1.f : 0.f;
		if (eventDriven) {
			// fired after all lanes have moved this sample (runLanes)
			if (r.nFires < 8)
				r.fires[r.nFires++] = {-1e18, lr.curIdx, lr.curPassed};
		}
		else {
			if (s.nudge >= 0 && r.nFires < 8)
				r.fires[r.nFires++] = {(double) lr.k + s.nudge / 100.0, lr.curIdx, lr.curPassed};
			const Step& ns = P.steps[l][lr.nextIdx];
			if (ns.nudge < 0 && r.nFires < 8)
				r.fires[r.nFires++] = {(double) (lr.k + 1) + ns.nudge / 100.0, lr.nextIdx, lr.nextPassed};
		}
		return;
	}
	if (matrixMute(t, l, in))
		return;
	if (lr.curPassed)
		lr.val = s.value;
	else if (s.hasAlt)
		lr.val = s.alt;
	lr.slide = s.slide;
	if (l == L_NOTE || l == L_OCT || l == L_CHORD) {
		if (ts[t].pitchFollows && r.noteOn)
			computePitches(t, s.slide, in);
	}
	if (l == L_MODA || l == L_MODB) {
		int k = l - L_MODA;
		r.modTgt[k] = lr.val;
		if ((s.slide || P.lanes[l].glide) && ts[t].glide > 0.001f)
			r.modRate[k] = std::fabs(r.modTgt[k] - r.modCur[k]) / ts[t].glide;
		else {
			r.modCur[k] = r.modTgt[k];
			r.modRate[k] = 0.f;
		}
	}
}

void Engine::passStart(int t, int l, bool completed, const EngineIn& in) {
	(void) in;
	TrackRun& r = tr[t];
	// gesture real-time recording uses pass boundaries of the target lane
	if (t == gestTrack && l == gestLane && opt.gestureMode == 0) {
		if (gestRec) {
			finishGesture();
		}
		else if (gestArmed) {
			gestArmed = false;
			gestRec = true;
			for (int i = 0; i < NS; i++) {
				gestSum[i] = 0.0;
				gestCount[i] = 0;
			}
		}
	}
	if (!completed)
		return;
	if (l == L_GATE) {
		if (ts[t].sync == SyncMode::Gate) {
			for (int k = 1; k < NL; k++) {
				LaneRun& o = r.lanes[k];
				o.pos = 0.0;
				o.k = -1;
				o.moves = -1;
				o.haveNext = false;
			}
		}
		if (ts[t].quant == ChangeQuant::GateEnd && r.queued >= 0)
			r.gateEndHit = true;
	}
}

void Engine::changePattern(int t) {
	TrackRun& r = tr[t];
	int q = r.queued;
	r.queued = -1;
	if (q < 0)
		return;
	loadSlot(t, q);
	for (int l = 0; l < NL; l++) {
		LaneRun& lr = r.lanes[l];
		lr.pos = 0.0;
		lr.k = -1;
		lr.moves = -1;
		lr.haveNext = false;
		lr.reversed = false;
	}
	r.nFires = 0;
}

void Engine::runLanes(int t, const EngineIn& in) {
	TrackRun& r = tr[t];
	for (int l = 0; l < NL; l++) {
		LaneRun& lr = r.lanes[l];
		ClockSrc cs = play[t].lanes[l].clock;
		if (cs == ClockSrc::Track) {
			lr.pos += r.dTw * laneRatio(t, l, in);
			int guard = 0;
			while ((int64_t) std::floor(lr.pos) > lr.k && guard < 8) {
				lr.k++;
				guard++;
				advanceLane(t, l, in);
			}
			if ((int64_t) std::floor(lr.pos) > lr.k)
				lr.k = (int64_t) std::floor(lr.pos);
		}
		else {
			bool ev = false;
			switch (cs) {
				case ClockSrc::Gate1: ev = (t > 0) ? gateEvtNow[0] : r.gateEventPrev; break;
				case ClockSrc::Gate2: ev = (t > 1) ? gateEvtNow[1] : tr[1].gateEventPrev; break;
				case ClockSrc::Gate3: ev = tr[2].gateEventPrev; break;
				case ClockSrc::ExtTrig: ev = extEvent; break;
				case ClockSrc::Onset: ev = onsetEvent; break;
				case ClockSrc::Threshold: ev = threshEvent; break;
				default: break;
			}
			if (t == 0 && cs == ClockSrc::Gate1)
				ev = r.gateEventPrev;
			if (ev) {
				lr.events++;
				if (lr.events % std::max<int>(1, play[t].lanes[l].extDiv) == 0) {
					lr.k++;
					advanceLane(t, l, in);
				}
			}
		}
	}
	// Gate fires happen after every lane has moved, so a note reads this step's values.
	// Fires are processed in time order (nudged steps can be queued out of order).
	double gpos = r.lanes[L_GATE].pos;
	while (true) {
		int best = -1;
		for (int f = 0; f < r.nFires; f++)
			if (r.fires[f].t <= gpos && (best < 0 || r.fires[f].t < r.fires[best].t))
				best = f;
		if (best < 0)
			break;
		Fire fi = r.fires[best];
		r.fires[best] = r.fires[--r.nFires];
		fireGate(t, fi.idx, fi.passed, in);
	}
}

// ---------------------------------------------------------------------------
// Notes (§7.0)

int Engine::followOffset(int t, const EngineIn& in) {
	if (t == 0 || !play[t].followT1)
		return 0;
	const LaneSettings& nl = play[0].lanes[L_NOTE];
	float startNote = play[0].steps[L_NOTE][nl.start].value;
	return (int) std::lround(tr[0].lanes[L_NOTE].val - startNote) + matrixTranspose(0, in);
}

void Engine::computePitches(int t, bool glide, const EngineIn& in) {
	TrackRun& r = tr[t];
	const Pattern& P = play[t];
	ScaleDef sc = getScale(P.scale, scales);
	int deg = (int) std::lround(r.lanes[L_NOTE].val) + P.transpose + matrixTranspose(t, in) + followOffset(t, in);
	int oct = (int) std::lround(r.lanes[L_OCT].val);
	int chord = clampv((int) std::lround(r.lanes[L_CHORD].val), 0, NUM_CHORDS - 1);
	int n = chordSize(chord, scales);
	int trSemis = (int) std::lround(in.transposeV * 12.f);
	r.chordN = n;
	for (int c = 0; c < 4; c++) {
		if (c >= n)
			continue;
		int d = deg + chordDegree(chord, c, scales);
		int semis = P.root + degreeToSemis(sc, d) + 12 * oct;
		if (trSemis != 0)
			semis = quantizeSemis(sc, P.root, semis + trSemis);
		float v = semis / 12.f;
		r.pitchTgt[c] = v;
		if (glide && ts[t].glide > 0.001f)
			r.glideRate[c] = std::fabs(v - r.pitchCur[c]) / ts[t].glide;
		else {
			r.pitchCur[c] = v;
			r.glideRate[c] = 0.f;
		}
	}
}

bool Engine::gateHigh(const TrackRun& r) const {
	if (!r.noteOn || r.gapTimer > 0.f)
		return false;
	double sd = r.stepDur;
	if (r.ratN <= 1)
		return r.noteT < r.lenFrac * sd;
	double sub = sd / r.ratN;
	int j = (int) std::floor(r.noteT / sub);
	if (j >= r.ratN)
		return false;
	double w = r.noteT - j * sub;
	return w < std::min(r.lenFrac, 0.95f) * sub;
}

void Engine::fireGate(int t, int idx, bool passed, const EngineIn& in) {
	const Pattern& P = play[t];
	const Step& s = P.steps[L_GATE][idx];
	bool on = s.value > 0.5f;
	float dens = densityValue(t, in);
	Role role = s.role;
	if (on) {
		if (!passed)
			return;
		if (role != Role::Anchor) {
			float a = arcValue(t, in);
			float th = (int) role * 0.25f;
			if (a <= th - 0.1f)
				return;
			if (a < th + 0.1f && tr[t].rng.uniform() >= (a - (th - 0.1f)) / 0.2f)
				return;
		}
		if (dens < 0.f) {
			float drop = (int) role * 0.25f + hash01(P.seed, (uint64_t) t, (uint64_t) idx, 5) * 0.25f;
			if (drop >= 1.f + dens)
				return;
		}
	}
	else {
		if (!(dens > 0.f && hash01(P.seed, (uint64_t) t, (uint64_t) idx, 9) < dens))
			return;
		role = Role::Passing;
	}
	if (ts[t].mute || (in.expander && in.muteGate[t]) || matrixMute(t, L_GATE, in))
		return;
	triggerNote(t, s, role, in);
}

void Engine::triggerNote(int t, const Step& g, Role role, const EngineIn& in) {
	TrackRun& r = tr[t];
	const Pattern& P = play[t];
	bool wasHigh = gateHigh(r);
	bool tie = wasHigh && r.lenFrac >= 1.f && r.ratN <= 1;
	float vel = r.lanes[L_VEL].val;
	if (g.accent) {
		vel += opt.accentAmount;
		r.accPending = true;
	}
	if (role == Role::Passing || role == Role::Tension)
		vel += opt.arcVel * arcValue(t, in);
	r.vel = clampv(vel, 0.f, 10.f);
	r.lenFrac = clampv(r.lanes[L_LEN].val, 1.f, 400.f) / 100.f;
	r.ratN = ratchetCount(r.lanes[L_RAT].val);
	r.ratRamp = ratchetRamp(r.lanes[L_RAT].val);
	if (play[t].lanes[L_GATE].clock == ClockSrc::Track) {
		double ratio = laneRatio(t, L_GATE, in);
		r.stepDur = 1.0 / std::max(1e-3, r.rate * ratio);
	}
	else {
		double dtg = r.timeSec - r.lastGateTime;
		r.stepDur = (dtg > 0.005 && dtg < 4.0) ? dtg : 0.125;
	}
	r.lastGateTime = r.timeSec;
	if (wasHigh && !tie)
		r.gapTimer = 0.001f;
	r.noteOn = true;
	r.noteT = 0.0;
	bool slide = r.lanes[L_NOTE].slide || tie;
	computePitches(t, slide && (r.lanes[L_NOTE].slide), in);
	(void) P;
	r.gateEvent = true;
}

// ---------------------------------------------------------------------------
// Transport (§6.1-6.3)

void Engine::advanceTransport(int t, const EngineIn& in) {
	TrackRun& r = tr[t];
	r.dTw = 0.0;
	bool own = in.tclkConn[t];
	bool ext = own || in.clockConn;
	bool pulse = own ? in.tclkPulse[t] : in.clockPulse;
	int pps = own ? 1 : std::max(1, in.ppStep);
	if (!in.running)
		return;
	if (ext) {
		r.sincePulse += in.dt;
		if (pulse) {
			r.pulses++;
			if (r.pulses > 0 && r.sincePulse > 1e-5) {
				r.iv[0] = r.iv[1];
				r.iv[1] = r.sincePulse;
			}
			r.sincePulse = 0.0;
			double snapT = r.pulses / (double) pps;
			if (!r.started) {
				r.started = true;
				r.T = snapT;
			}
			else
				r.T = std::max(r.T, snapT);
			r.lastSnap = snapT;
			double avg = 0.5 * (r.iv[0] + r.iv[1]);
			r.rate = clampv((1.0 / pps) / std::max(1e-4, avg), 0.05, 2000.0);
		}
		else if (r.started) {
			r.T += r.rate * in.dt;
			r.T = std::min(r.T, r.lastSnap + 1.0 / pps - 1e-9);
		}
	}
	else {
		r.started = true;
		r.rate = clampv((double) in.bpm, 30.0, 300.0) / 60.0 * 4.0;
		r.T += r.rate * in.dt;
	}
	if (!r.started)
		return;
	double tw = swingWarp(r.T, play[t].swing);
	r.dTw = std::max(0.0, tw - r.Tw);
	r.Tw = tw;
}

void Engine::boundary(int t, int kind, const EngineIn& in, EngineOut& out) {
	// kind: 0 base step, 1 beat, 2 bar
	TrackRun& r = tr[t];
	ChangeQuant q = ts[t].quant;
	bool hit = (q == ChangeQuant::Off && kind == 0) || (q == ChangeQuant::Beat && kind == 1) || (q == ChangeQuant::Bar && kind == 2);
	if (hit) {
		if (r.queued >= 0)
			changePattern(t);
		if (t == 0) {
			out.eop = true;
			if (songOn && songs[songIdx].count > 0) {
				const Song& S = songs[songIdx];
				int row = clampv(songRow, 0, S.count - 1);
				const SongRow& R = S.rows[row];
				bool advance = false;
				if (R.repeats == 0) {
					advance = songNextPending;
					songNextPending = false;
				}
				else {
					songRepeat++;
					advance = songRepeat >= R.repeats;
				}
				if (advance) {
					songRepeat = 0;
					songRow++;
					if (songRow >= S.count) {
						songRow = 0;
						out.eos = true;
					}
					songApplyRow(false);
				}
			}
		}
	}
	if (kind == 2) {
		// drift and HOME happen on bars
		if (r.pendingHome) {
			r.pendingHome = false;
			edit[t] = home[t];
			dirty[t] = true;
		}
		if (ts[t].drift > 0.f && r.rng.uniform() < ts[t].drift)
			doDrift(t, in.character);
		if (ts[t].sync == SyncMode::Bars) {
			int64_t bar = (int64_t) std::floor(r.T / 16.0);
			if (bar > 0 && bar % std::max<int>(1, ts[t].syncBars) == 0) {
				for (int l = 0; l < NL; l++) {
					LaneRun& lr = r.lanes[l];
					lr.pos = 0.0;
					lr.k = -1;
					lr.moves = -1;
					lr.haveNext = false;
				}
				r.nFires = 0;
			}
		}
	}
	int mq = opt.muteQuant;
	if (r.pendingMute >= 0 && (mq == 0 || (mq == 1 && kind >= 1) || (mq == 2 && kind == 2))) {
		ts[t].mute = r.pendingMute == 1;
		r.pendingMute = -1;
	}
}

void Engine::songApplyRow(bool immediate) {
	const Song& S = songs[songIdx];
	if (S.count == 0)
		return;
	const SongRow& R = S.rows[clampv(songRow, 0, S.count - 1)];
	for (int t = 0; t < NT; t++) {
		if (R.pattern[t] < 0)
			continue;
		if (immediate) {
			tr[t].queued = R.pattern[t];
			changePattern(t);
		}
		else
			tr[t].queued = R.pattern[t];
	}
}

void Engine::doDrift(int t, float character) {
	Pattern& E = edit[t];
	const Pattern& H = home[t];
	const int lanes[3] = {L_GATE, L_NOTE, L_VEL};
	int total = 0, diff = 0;
	int diffs[3 * NS][2];
	for (int l : lanes) {
		const LaneSettings& ls = E.lanes[l];
		for (int i = 0; i < ls.length; i++) {
			int idx = (ls.start + i) % NS;
			total++;
			if (E.steps[l][idx].value != H.steps[l][idx].value) {
				diffs[diff][0] = l;
				diffs[diff][1] = idx;
				diff++;
			}
		}
	}
	TrackRun& r = tr[t];
	int cap = (int) std::floor(0.5f * ts[t].drift * total);
	if (diff > 0 && diff >= cap) {
		int k = r.rng.uniformInt(diff);
		int l = diffs[k][0], idx = diffs[k][1];
		E.steps[l][idx].value = H.steps[l][idx].value;
	}
	else if (cap > 0) {
		int l = lanes[r.rng.uniformInt(3)];
		const LaneSettings& ls = E.lanes[l];
		int idx = (ls.start + r.rng.uniformInt(ls.length)) % NS;
		Rng g = Rng::fork(E.seed, (uint64_t) t, 555, (uint64_t) sampleCount);
		genSingleStep(E, l, idx, character, g);
	}
	dirty[t] = true; // home is kept as it was
}

void Engine::finishGesture() {
	gestRec = false;
	gestArmed = false;
	int t = gestTrack, l = gestLane;
	const LaneSettings& ls = edit[t].lanes[l];
	int L = ls.length;
	float means[NS];
	if (opt.gestureMode == 0) {
		float last = 0.5f;
		for (int i = 0; i < L; i++) {
			if (gestCount[i] > 0)
				last = (float) (gestSum[i] / gestCount[i]);
			means[i] = last;
		}
	}
	else {
		if (gestLen < 1) {
			setMsg("GESTURE TOO SHORT");
			return;
		}
		for (int i = 0; i < L; i++) {
			int a = (int) ((int64_t) gestLen * i / L);
			int b = (int) ((int64_t) gestLen * (i + 1) / L);
			if (b <= a)
				b = a + 1;
			double s = 0.0;
			int n = 0;
			for (int j = a; j < b && j < gestLen; j++) {
				s += gestBuf[j];
				n++;
			}
			means[i] = n ? (float) (s / n) : 0.5f;
		}
	}
	pushUndo(t);
	fitGesture(edit[t], l, means, L);
	markEdited(t);
	setMsg("GESTURE FITTED");
}

// ---------------------------------------------------------------------------
// UI (§10)

void Engine::toggleMute(int t) {
	bool target = tr[t].pendingMute >= 0 ? !(tr[t].pendingMute == 1) : !ts[t].mute;
	tr[t].pendingMute = target ? 1 : 0;
	if (opt.muteQuant == 0) {
		ts[t].mute = target;
		tr[t].pendingMute = -1;
	}
}

void Engine::editStepValue(int t, int lane, int idx, int d, bool shift) {
	Step& s = edit[t].steps[lane][idx];
	switch (lane) {
		case L_GATE: s.value = d > 0 ? 1.f : 0.f; break;
		case L_NOTE: s.value = (float) clampv((int) s.value + d, 0, 15); break;
		case L_OCT: s.value = (float) clampv((int) s.value + d, -3, 3); break;
		case L_CHORD: s.value = (float) clampv((int) s.value + d, 0, NUM_CHORDS - 1); break;
		case L_VEL: s.value = clampv(s.value + 0.1f * d, 0.f, 10.f); break;
		case L_LEN: s.value = clampv(s.value + (float) d, 1.f, 400.f); break;
		case L_RAT:
			if (shift)
				s.value = ratchetPack(ratchetCount(s.value), cycle(ratchetRamp(s.value), d > 0 ? 1 : -1, 3));
			else
				s.value = ratchetPack(clampv(ratchetCount(s.value) + d, 1, 8), ratchetRamp(s.value));
			break;
		default: s.value = clampv(s.value + 0.05f * d, -5.f, 5.f); break;
	}
}

void Engine::applyEncoder(int e, int d, bool shift) {
	int t = selTrack;
	Pattern& E = edit[t];
	TrackSettings& S = ts[t];
	int key = (t << 16) | (page << 8) | selLane;

	auto beginEdit = [&]() {
		if (editKey != key) {
			pushUndo(t);
			editKey = key;
		}
	};

	// Track-level performance encoders (no undo)
	switch (e) {
		case E_ARC: S.arc = clampv(S.arc + 0.01f * d, 0.f, 1.f); return;
		case E_DRIFT: S.drift = clampv(S.drift + 0.01f * d, 0.f, 1.f); return;
		case E_DENS: S.density = clampv(S.density + 0.01f * d, -1.f, 1.f); return;
		case E_SPREAD: S.spread = clampv(S.spread + 0.01f * d, 0.f, 1.f); return;
		case E_X: S.mx = clampv(S.mx + 0.01f * d, 0.f, 1.f); return;
		case E_Y: S.my = clampv(S.my + 0.01f * d, 0.f, 1.f); return;
		case E_SEED:
			beginEdit();
			E.seed = (uint16_t) cycle(E.seed, d, 1000);
			markEdited(t);
			return;
		default: break;
	}

	if (e >= E_LEN && e <= E_DIR) {
		if (answerLane(t, selLane)) {
			setMsg("ANSWER LANE");
			return;
		}
		beginEdit();
		LaneSettings& ls = E.lanes[selLane];
		switch (e) {
			case E_LEN: ls.length = (uint8_t) clampv(ls.length + d, 1, NS); break;
			case E_START: ls.start = (uint8_t) cycle(ls.start, d, NS); break;
			case E_CLOCK:
				if (shift) {
					ls.extDiv = (uint8_t) clampv(ls.extDiv + d, 1, 16);
				}
				else {
					int c = (ls.clock == ClockSrc::Track) ? ls.ratio : NRATIOS - 1 + (int) ls.clock;
					c = clampv(c + d, 0, NRATIOS - 1 + (int) ClockSrc::COUNT - 1);
					if (c < NRATIOS) {
						ls.clock = ClockSrc::Track;
						ls.ratio = (uint8_t) c;
					}
					else
						ls.clock = (ClockSrc) (c - (NRATIOS - 1));
				}
				break;
			case E_DIR:
				if (shift)
					ls.skip = (uint8_t) clampv(ls.skip + d, 2, 7);
				else
					ls.dir = (Dir) cycle((int) ls.dir, d > 0 ? 1 : -1, (int) Dir::COUNT);
				break;
			default: break;
		}
		markEdited(t);
		return;
	}

	int k = e; // step knob 0..15
	int raw = k + (hi ? 16 : 0);
	switch (page) {
		case P_LANES: {
			if (answerLane(t, selLane)) {
				setMsg("ANSWER LANE");
				return;
			}
			beginEdit();
			editStepValue(t, selLane, raw, d, shift);
			markEdited(t);
			break;
		}
		case P_STEP: {
			if (answerLane(t, selLane)) {
				setMsg("ANSWER LANE");
				return;
			}
			beginEdit();
			Step& s = E.steps[selLane][selStep];
			switch (k) {
				case 0: s.prob = (uint8_t) clampv(s.prob + d, 0, 100); break;
				case 1: s.cond = (Cond) cycle((int) s.cond, d > 0 ? 1 : -1, (int) Cond::COUNT); break;
				case 2: s.condA = (uint8_t) clampv(s.condA + d, 1, (int) s.condB); break;
				case 3:
					s.condB = (uint8_t) clampv(s.condB + d, 1, 8);
					if (s.condA > s.condB)
						s.condA = s.condB;
					break;
				case 4: {
					if (!s.hasAlt) {
						s.hasAlt = true;
						s.alt = s.value;
					}
					Step tmp = s;
					tmp.value = s.alt;
					std::swap(E.steps[selLane][selStep], tmp);
					editStepValue(t, selLane, selStep, d, shift);
					float nv = E.steps[selLane][selStep].value;
					std::swap(E.steps[selLane][selStep], tmp);
					E.steps[selLane][selStep].alt = nv;
					break;
				}
				case 5: s.slide = d > 0; break;
				case 6: s.accent = d > 0; break;
				case 7: s.nudge = (int8_t) clampv(s.nudge + d, -49, 49); break;
				case 8: E.steps[L_GATE][selStep].role = (Role) clampv((int) E.steps[L_GATE][selStep].role + d, 0, (int) Role::COUNT - 1); break;
				case 9: s.pin = (Pin) cycle((int) s.pin, d > 0 ? 1 : -1, 3); break;
				default: return;
			}
			markEdited(t);
			break;
		}
		case P_PATTERN: {
			switch (k) {
				case 0: S.sync = (SyncMode) cycle((int) S.sync, d > 0 ? 1 : -1, (int) SyncMode::COUNT); break;
				case 1: S.syncBars = (uint8_t) clampv(S.syncBars + d, 1, 16); break;
				case 2: S.quant = (ChangeQuant) cycle((int) S.quant, d > 0 ? 1 : -1, (int) ChangeQuant::COUNT); break;
				case 3:
					beginEdit();
					E.swing = (uint8_t) clampv(E.swing + d, 50, 75);
					markEdited(t);
					break;
				case 4: S.glide = clampv(S.glide + 0.01f * d, 0.f, 2.f); break;
				case 5: S.poly = (uint8_t) clampv(S.poly + d, 0, 4); break;
				case 6: S.pitchFollows = d > 0; break;
				default: break;
			}
			break;
		}
		case P_RULES: {
			beginEdit();
			RulesConfig& R = E.rules;
			if (k < 8) {
				if (d > 0)
					R.intervals |= (uint8_t) (1 << k);
				else
					R.intervals &= (uint8_t) ~(1 << k);
			}
			else switch (k) {
				case 8: R.contour = (Contour) cycle((int) R.contour, d > 0 ? 1 : -1, (int) Contour::COUNT); break;
				case 9: R.lo = (uint8_t) clampv(R.lo + d, 0, 15); break;
				case 10: R.hi = (uint8_t) clampv(R.hi + d, 0, 15); break;
				case 11: R.maxRepeat = (uint8_t) clampv(R.maxRepeat + d, 1, 4); break;
				case 12: R.densMin = (uint8_t) clampv(R.densMin + 5 * d, 0, 100); break;
				case 13: R.densMax = (uint8_t) clampv(R.densMax + 5 * d, 0, 100); break;
				case 14: R.leapRecovery = d > 0; break;
				case 15: R.endOn = (EndOn) cycle((int) R.endOn, d > 0 ? 1 : -1, (int) EndOn::COUNT); break;
			}
			markEdited(t);
			break;
		}
		case P_ANSWER: {
			if (t == 0)
				return;
			beginEdit();
			AnswerConfig& A = E.answer;
			switch (k) {
				case 0: A.mode = (AnswerMode) cycle((int) A.mode, d > 0 ? 1 : -1, (int) AnswerMode::COUNT); break;
				case 1: A.source = (uint8_t) clampv(A.source + d, 0, t - 1); break;
				case 2: A.displace = (uint8_t) clampv(A.displace + d, 0, 15); break;
				case 3: A.transpose = (int8_t) clampv(A.transpose + d, -7, 7); break;
				case 4: A.fill = (uint8_t) clampv(A.fill + 5 * d, 0, 100); break;
				default: break;
			}
			markEdited(t);
			break;
		}
		case P_MORPH: {
			if (selPoint < 0 || selPoint >= NPOINT || !S.points[selPoint].active)
				return;
			if (k == 0)
				S.points[selPoint].x = clampv(S.points[selPoint].x + 0.01f * d, 0.f, 1.f);
			else if (k == 1)
				S.points[selPoint].y = clampv(S.points[selPoint].y + 0.01f * d, 0.f, 1.f);
			dirty[t] = true;
			break;
		}
		case P_MATRIX: {
			beginEdit();
			MatrixSlot& m = E.matrix[k / 4];
			switch (k % 4) {
				case 0: m.source = (uint8_t) cycle(m.source, d, MATRIX_SRC_COUNT); break;
				case 1: m.target = (uint8_t) cycle(m.target, d, NL); break;
				case 2: m.action = (MatrixAction) cycle((int) m.action, d > 0 ? 1 : -1, (int) MatrixAction::COUNT); break;
				case 3: m.amount = clampv(m.amount + 0.05f * d, -1.f, 1.f); break;
			}
			markEdited(t);
			break;
		}
		case P_SONG: {
			Song& S2 = songs[songIdx];
			switch (k) {
				case 0:
					songIdx = clampv(songIdx + d, 0, NSONG - 1);
					songRow = 0;
					break;
				case 1: songRow = clampv(songRow + d, 0, std::max(0, (int) songs[songIdx].count - 1)); break;
				case 2:
				case 3:
				case 4: {
					if (S2.count == 0) {
						S2.count = 1;
						S2.rows[0] = SongRow();
					}
					SongRow& R = S2.rows[clampv(songRow, 0, S2.count - 1)];
					R.pattern[k - 2] = (int8_t) clampv(R.pattern[k - 2] + d, -1, NSLOT - 1);
					break;
				}
				case 5: {
					if (S2.count == 0)
						return;
					SongRow& R = S2.rows[clampv(songRow, 0, S2.count - 1)];
					R.repeats = (uint8_t) clampv(R.repeats + d, 0, 64);
					break;
				}
				case 6: {
					bool on = d > 0;
					if (on && !songOn && S2.count > 0) {
						songOn = true;
						songRow = 0;
						songRepeat = 0;
						songApplyRow(true);
					}
					else if (!on)
						songOn = false;
					break;
				}
				default: break;
			}
			break;
		}
		case P_SCALE: {
			beginEdit();
			switch (k) {
				case 0: E.root = (uint8_t) cycle(E.root, d, 12); break;
				case 1: E.scale = (uint8_t) cycle(E.scale, d, NUM_SCALES); break;
				case 2:
					if (t > 0)
						E.followT1 = d > 0;
					break;
				case 3: E.transpose = (int8_t) clampv(E.transpose + d, -14, 14); break;
				case 8: selUserChord = clampv(selUserChord + d, 0, NUM_USER_CHORDS - 1); break;
				case 9:
				case 10:
				case 11:
				case 12: {
					int8_t& v = scales.userChord[selUserChord][k - 9];
					v = (int8_t) clampv(v + d, -7, 14);
					break;
				}
				case 13: scales.userChordSize[selUserChord] = (uint8_t) clampv(scales.userChordSize[selUserChord] + d, 1, MAX_CHORD); break;
				default: break;
			}
			markEdited(t);
			for (int u = 0; u < NT; u++)
				dirty[u] = true;
			break;
		}
		default: break;
	}
}

void Engine::handleStepButton(int i, bool shift) {
	int t = selTrack;
	Pattern& E = edit[t];
	TrackSettings& S = ts[t];
	int raw = i + (hi ? 16 : 0);
	switch (page) {
		case P_LANES:
		case P_STEP:
			selStep = raw;
			if (shift) {
				page = P_STEP;
				break;
			}
			if (page == P_LANES && selLane == L_GATE) {
				if (answerLane(t, L_GATE)) {
					setMsg("ANSWER LANE");
					break;
				}
				if (editKey != ((t << 16) | (page << 8) | selLane)) {
					pushUndo(t);
					editKey = (t << 16) | (page << 8) | selLane;
				}
				Step& s = E.steps[L_GATE][raw];
				s.value = s.value > 0.5f ? 0.f : 1.f;
				markEdited(t);
			}
			break;
		case P_PATTERN:
			if (i >= NSLOT)
				break;
			if (shift)
				S.locked[i] = !S.locked[i];
			else {
				tr[t].queued = i;
				if (ts[t].quant == ChangeQuant::Off || !tr[t].started)
					changePattern(t);
			}
			break;
		case P_ROLE: {
			pushUndo(t);
			Step& s = E.steps[L_GATE][raw];
			s.role = (Role) cycle((int) s.role, 1, (int) Role::COUNT);
			markEdited(t);
			break;
		}
		case P_RULES:
			E.rules.mustHit ^= (1u << raw);
			if (E.rules.mustHit & (1u << raw))
				E.steps[L_GATE][raw].value = 1.f;
			markEdited(t);
			break;
		case P_MORPH: {
			if (i >= NSLOT)
				break;
			int found = -1;
			for (int j = 0; j < NPOINT; j++)
				if (S.points[j].active && S.points[j].slot == i)
					found = j;
			if (shift) {
				if (found >= 0) {
					S.points[found].active = false;
					S.points[found].slot = -1;
					if (selPoint == found)
						selPoint = -1;
					dirty[t] = true;
				}
				break;
			}
			if (!S.used[i]) {
				setMsg("SLOT EMPTY");
				break;
			}
			if (found >= 0) {
				selPoint = found;
				break;
			}
			for (int j = 0; j < NPOINT; j++) {
				if (!S.points[j].active) {
					S.points[j].active = true;
					S.points[j].slot = (int8_t) i;
					S.points[j].x = DEFAULT_POINTS[j][0];
					S.points[j].y = DEFAULT_POINTS[j][1];
					selPoint = j;
					dirty[t] = true;
					return;
				}
			}
			setMsg("MAP FULL (8)");
			break;
		}
		case P_SONG: {
			Song& S2 = songs[songIdx];
			if (i == 0 && S2.count < NROW) {
				int at = S2.count == 0 ? 0 : clampv(songRow + 1, 0, (int) S2.count);
				for (int r = S2.count; r > at; r--)
					S2.rows[r] = S2.rows[r - 1];
				S2.rows[at] = at > 0 ? S2.rows[at - 1] : SongRow();
				S2.count++;
				songRow = at;
			}
			else if (i == 1 && S2.count > 0) {
				for (int r = songRow; r < S2.count - 1; r++)
					S2.rows[r] = S2.rows[r + 1];
				S2.count--;
				songRow = clampv(songRow, 0, std::max(0, S2.count - 1));
			}
			break;
		}
		case P_SCALE: {
			int sc = E.scale;
			if (sc >= NUM_PRESET_SCALES && i < 12 && i > 0) {
				scales.userMask[sc - NUM_PRESET_SCALES] ^= (uint16_t) (1 << i);
				for (int u = 0; u < NT; u++)
					dirty[u] = true;
			}
			break;
		}
		default: break;
	}
}

void Engine::handleUI(const EngineIn& in) {
	bool shift = in.shift;
	for (int t = 0; t < NT; t++)
		if (in.pTrack[t]) {
			selTrack = t;
			if (page == P_ANSWER && t == 0)
				page = P_LANES;
			editKey = -1;
		}
	for (int l = 0; l < NL; l++)
		if (in.pLane[l]) {
			selLane = l;
			editKey = -1;
		}
	if (in.pPage) {
		do {
			page = cycle(page, shift ? -1 : 1, NUM_PAGES);
		} while (page == P_ANSWER && selTrack == 0);
		editKey = -1;
	}
	if (in.pHi)
		hi = !hi;
	for (int i = 0; i < 16; i++)
		if (in.pStep[i])
			handleStepButton(i, shift);
	for (int t = 0; t < NT; t++)
		if (in.pMute[t])
			toggleMute(t);

	int t = selTrack;
	bool genAction = in.pGen || in.genTrig;
	if (genAction) {
		if ((page == P_RULES && shift) || false) {
			// handled below with rule gen
		}
		else if (shift) {
			pushUndo(t);
			Rng rng = Rng::fork(edit[t].seed, (uint64_t) t, 100, genCount[t]++);
			genTrack(edit[t], in.character, in.amount, rng);
			markEdited(t);
			setMsg("GEN TRACK");
		}
		else {
			if (answerLane(t, selLane)) {
				setMsg("ANSWER LANE");
			}
			else {
				pushUndo(t);
				Rng rng = Rng::fork(edit[t].seed, (uint64_t) t, (uint64_t) selLane, genCount[t]++);
				genLane(edit[t], selLane, in.character, in.amount, rng);
				markEdited(t);
				char b[40];
				std::snprintf(b, sizeof(b), "GEN %s", LANE_NAMES[selLane]);
				setMsg(b);
			}
		}
		editKey = -1;
	}
	if ((genAction && page == P_RULES && shift) || in.ruleGenTrig) {
		pushUndo(t);
		Rng rng = Rng::fork(edit[t].seed, (uint64_t) t, 200, genCount[t]++);
		char relaxed[32];
		rulesGen(edit[t], getScale(edit[t].scale, scales), rng, relaxed);
		markEdited(t);
		setMsg(relaxed[0] ? relaxed : "RULE GEN");
		editKey = -1;
	}
	if (in.pKeep || in.keepTrig) {
		if (in.pKeep && shift && page == P_ANSWER && t > 0) {
			pushUndo(t);
			AnswerConfig a = edit[t].answer;
			Pattern baked = play[t];
			for (int l : {L_GATE, L_NOTE, L_OCT}) {
				edit[t].lanes[l] = baked.lanes[l];
				for (int s = 0; s < NS; s++)
					edit[t].steps[l][s] = baked.steps[l][s];
			}
			edit[t].answer = a;
			edit[t].answer.mode = AnswerMode::Off;
			markEdited(t);
			setMsg("ANSWER BAKED");
		}
		else if ((in.pKeep && shift) || (in.keepTrig && opt.keepAllFromInput)) {
			for (int u = 0; u < NT; u++)
				keepTrack(u);
			setMsg("KEPT ALL");
		}
		else
			keepTrack(t);
	}
	if (in.pUndo)
		undo(t);
	if (in.pHome) {
		tr[t].pendingHome = true;
		setMsg("HOME AT BAR");
	}
	if (in.pMorph) {
		if (shift) {
			if (ts[t].morphOn) {
				pushUndo(t);
				Pattern baked = play[t];
				baked.answer = edit[t].answer;
				edit[t] = baked;
				ts[t].morphOn = false;
				markEdited(t);
				setMsg("MORPH BAKED");
			}
		}
		else {
			ts[t].morphOn = !ts[t].morphOn;
			dirty[t] = true;
			setMsg(ts[t].morphOn ? "MORPH ON" : "MORPH OFF");
		}
	}
	if (in.pRec) {
		if (gestRec && opt.gestureMode == 1) {
			finishGesture();
		}
		else if (gestRec || gestArmed) {
			gestRec = gestArmed = false;
			setMsg("GESTURE CANCELLED");
		}
		else {
			gestTrack = t;
			gestLane = selLane;
			if (answerLane(t, selLane)) {
				setMsg("ANSWER LANE");
			}
			else if (opt.gestureMode == 0) {
				gestArmed = true;
				setMsg("REC AT NEXT PASS");
			}
			else {
				gestRec = true;
				gestLen = 0;
				gestAcc = 0.0;
				setMsg("REC... PRESS TO STOP");
			}
		}
	}
	// PIN: shift+PIN clears the lane, held 1 s clears the track
	if (in.pPin && shift) {
		Pattern* ps[3] = {&edit[t], &play[t], &home[t]};
		for (Pattern* p : ps) {
			for (int s = 0; s < NS; s++)
				p->steps[selLane][s].pin = Pin::None;
			p->pathLen[selLane] = 0;
		}
		setMsg("LANE PINS CLEARED");
	}
	if (shift && in.pinHeld) {
		pinHoldTime += in.dt;
		if (pinHoldTime >= 1.f && pinHoldTime - in.dt < 1.f) {
			Pattern* ps[3] = {&edit[t], &play[t], &home[t]};
			for (Pattern* p : ps)
				for (int l = 0; l < NL; l++) {
					for (int s = 0; s < NS; s++)
						p->steps[l][s].pin = Pin::None;
					p->pathLen[l] = 0;
				}
			setMsg("TRACK PINS CLEARED");
		}
	}
	else
		pinHoldTime = 0.f;
	pinActive = (in.pinHeld && !shift) || (in.expander && in.pinGate);

	for (int e = 0; e < NUM_ENC; e++)
		if (in.enc[e])
			applyEncoder(e, in.enc[e], shift);

	// Expander queue inputs
	if (in.expander)
		for (int u = 0; u < NT; u++)
			if (in.queueTrig[u]) {
				int slot = clampv((int) std::floor(in.queueV[u] / 10.f * NSLOT), 0, NSLOT - 1);
				tr[u].queued = slot;
			}
	if (in.expander && in.nextTrig)
		songNextPending = true;
}

// ---------------------------------------------------------------------------

void Engine::process(const EngineIn& in, EngineOut& out) {
	sampleCount++;
	out.eop = out.eos = false;
	fillActive = in.fill || in.fillHeld;
	if (msgTime > 0.f) {
		msgTime -= in.dt;
		if (msgTime <= 0.f)
			msg[0] = 0;
	}
	handleUI(in);

	if (in.reset) {
		resetAll();
		if (songOn) {
			songRow = 0;
			songRepeat = 0;
			songApplyRow(true);
		}
	}

	// listening sources
	extEvent = in.expander && in.eventTrig;
	onsetEvent = false;
	if (in.expander) {
		float rect = std::fabs(in.audio);
		float aCoef = 1.f - std::exp(-in.dt / 0.001f);
		float rCoef = 1.f - std::exp(-in.dt / 0.05f);
		onsetEnv += (rect > onsetEnv ? aCoef : rCoef) * (rect - onsetEnv);
		onsetHistAcc += in.dt;
		if (onsetHistAcc >= 0.001) {
			onsetHistAcc -= 0.001;
			onsetHistIdx = (onsetHistIdx + 1) % 10;
			float old = onsetHist[onsetHistIdx];
			onsetHist[onsetHistIdx] = onsetEnv;
			float need = 5.f * std::pow(10.f, in.sensDb / 20.f);
			if (onsetRefractory <= 0.f && onsetEnv - old > need) {
				onsetEvent = true;
				onsetRefractory = 0.05f;
			}
		}
		if (onsetRefractory > 0.f)
			onsetRefractory -= in.dt;
		float s1 = in.src[0];
		threshEvent = false;
		if (!threshHigh && s1 > in.thresh + 0.05f) {
			threshHigh = true;
			threshEvent = true;
		}
		else if (threshHigh && s1 < in.thresh - 0.05f)
			threshHigh = false;
	}
	else
		threshEvent = false;

	// rebuild play patterns where needed (sources first, so answers follow)
	for (int t = 0; t < NT; t++) {
		TrackSettings& S = ts[t];
		if (S.morphOn) {
			float x = clampv(S.mx + (in.expander ? in.xCV[t] / 10.f : 0.f), 0.f, 1.f);
			float y = clampv(S.my + (in.expander ? in.yCV[t] / 10.f : 0.f), 0.f, 1.f);
			if (std::fabs(x - lastMx[t]) > 0.002f || std::fabs(y - lastMy[t]) > 0.002f) {
				lastMx[t] = x;
				lastMy[t] = y;
				dirty[t] = true;
			}
		}
		if (t > 0 && edit[t].answer.mode != AnswerMode::Off) {
			int src = clampv<int>(edit[t].answer.source, 0, t - 1);
			if (answerSeen[t] != playVersion[src])
				dirty[t] = true;
		}
		if (dirty[t])
			rebuildPlay(t);
	}

	for (int t = 0; t < NT; t++) {
		TrackRun& r = tr[t];
		r.timeSec += in.dt;
		r.gateEventPrev = r.gateEvent;
		r.gateEvent = false;
		double prevT = r.T;
		advanceTransport(t, in);
		if (r.started && in.running) {
			// boundaries
			int64_t base = (int64_t) std::floor(r.T);
			int64_t beat = (int64_t) std::floor(r.T / 4.0);
			int64_t bar = (int64_t) std::floor(r.T / 16.0);
			bool first = r.lastBase < 0;
			if (!first) {
				if (base != r.lastBase)
					boundary(t, 0, in, out);
				if (beat != r.lastBeat)
					boundary(t, 1, in, out);
				if (bar != r.lastBar)
					boundary(t, 2, in, out);
			}
			r.lastBase = base;
			r.lastBeat = beat;
			r.lastBar = bar;
			runLanes(t, in);
			if (r.gateEndHit) {
				r.gateEndHit = false;
				if (r.queued >= 0)
					changePattern(t);
			}
		}
		(void) prevT;
		gateEvtNow[t] = r.gateEvent;

		// note state
		if (r.noteOn) {
			r.noteT += in.dt;
			if (r.gapTimer > 0.f)
				r.gapTimer -= in.dt;
			if (r.noteT > std::max(1.0, (double) r.lenFrac) * r.stepDur + 0.01)
				r.noteOn = false;
		}
		for (int c = 0; c < 4; c++) {
			if (r.glideRate[c] > 0.f) {
				float d = r.pitchTgt[c] - r.pitchCur[c];
				float stepv = r.glideRate[c] * in.dt;
				if (std::fabs(d) <= stepv) {
					r.pitchCur[c] = r.pitchTgt[c];
					r.glideRate[c] = 0.f;
				}
				else
					r.pitchCur[c] += d > 0 ? stepv : -stepv;
			}
		}
		for (int k = 0; k < 2; k++) {
			if (r.modRate[k] > 0.f) {
				float d = r.modTgt[k] - r.modCur[k];
				float stepv = r.modRate[k] * in.dt;
				if (std::fabs(d) <= stepv) {
					r.modCur[k] = r.modTgt[k];
					r.modRate[k] = 0.f;
				}
				else
					r.modCur[k] += d > 0 ? stepv : -stepv;
			}
		}

		// live outputs
		int chans = ts[t].poly == 0 ? r.chanAuto : ts[t].poly;
		bool high = gateHigh(r);
		float vel = r.vel;
		if (r.ratN > 1 && r.ratRamp != 0) {
			double sub = r.stepDur / r.ratN;
			int j = clampv((int) std::floor(r.noteT / sub), 0, r.ratN - 1);
			float f = (float) j / (float) (r.ratN - 1);
			float factor = r.ratRamp == 1 ? 0.4f + 0.6f * f : 1.f - 0.6f * f;
			vel *= factor;
		}
		bool acc = r.accPending;
		r.accPending = false;

		// freeze (§10.3)
		bool freezeNow = (in.freezeHeld && t == selTrack) || (in.expander && in.freezeGate[t]);
		if (freezeNow && !r.frozen) {
			r.frozen = true;
			int len = (int) std::lround(opt.freezeLen / std::max(0.05, r.rate) * 1000.0);
			r.fLoopLen = clampv(len, 1, FREEZE_CAP - 1);
			r.fLoopStart = (r.fWrite - r.fLoopLen + FREEZE_CAP) % FREEZE_CAP;
			r.fPlay = 0;
		}
		else if (!freezeNow && r.frozen)
			r.frozen = false;
		r.accRec = r.accRec || acc;
		r.fAcc += in.dt;
		bool tick = false;
		if (r.fAcc >= 0.001) {
			r.fAcc -= 0.001;
			tick = true;
		}
		if (r.frozen) {
			if (tick)
				r.fPlay = (r.fPlay + 1) % std::max(1, r.fLoopLen);
			const FreezeFrame& f = r.frames[(r.fLoopStart + r.fPlay) % FREEZE_CAP];
			out.chans[t] = std::max<int>(1, f.chans);
			for (int c = 0; c < 4; c++) {
				out.pitch[t][c] = f.pitch[c];
				out.gate[t][c] = (f.gateMask & (1 << c)) ? 10.f : 0.f;
			}
			out.vel[t] = f.vel;
			out.mod[t][0] = f.mod[0];
			out.mod[t][1] = f.mod[1];
			out.acc[t] = tick && f.acc;
		}
		else {
			out.chans[t] = chans;
			for (int c = 0; c < 4; c++) {
				out.pitch[t][c] = r.pitchCur[c];
				out.gate[t][c] = (high && c < r.chordN && c < chans) ? 10.f : 0.f;
			}
			out.vel[t] = vel;
			out.mod[t][0] = r.modCur[0];
			out.mod[t][1] = r.modCur[1];
			out.acc[t] = acc;
			if (tick) {
				FreezeFrame& f = r.frames[r.fWrite];
				for (int c = 0; c < 4; c++)
					f.pitch[c] = out.pitch[t][c];
				f.gateMask = 0;
				for (int c = 0; c < 4; c++)
					if (out.gate[t][c] > 5.f)
						f.gateMask |= (uint8_t) (1 << c);
				f.chans = (uint8_t) chans;
				f.vel = vel;
				f.mod[0] = out.mod[t][0];
				f.mod[1] = out.mod[t][1];
				f.acc = r.accRec ? 1 : 0;
				r.accRec = false;
				r.fWrite = (r.fWrite + 1) % FREEZE_CAP;
			}
		}
	}

	// gesture capture
	{
		float g = (opt.gestureFromExp && in.expander) ? clampv(in.src[1] / 10.f, 0.f, 1.f) : clampv(in.gesture, 0.f, 1.f);
		if (gestRec) {
			if (opt.gestureMode == 0) {
				int i = tr[gestTrack].lanes[gestLane].curI;
				if (i >= 0 && i < NS) {
					gestSum[i] += g;
					gestCount[i]++;
				}
			}
			else {
				gestAcc += in.dt;
				while (gestAcc >= 0.001) {
					gestAcc -= 0.001;
					if (gestLen < 60000)
						gestBuf[gestLen++] = g;
					else {
						finishGesture();
						break;
					}
				}
			}
		}
		gestTraceAcc += in.dt;
		if (gestTraceAcc >= 1.0 / 30.0) {
			gestTraceAcc = 0.0;
			gestTrace[gestHead] = g;
			gestHead = (gestHead + 1) % 64;
		}
	}

	snapAcc += in.dt;
	if (snapAcc >= 1.0 / 60.0) {
		snapAcc = 0.0;
		writeSnapshot(in);
	}
}

void Engine::writeSnapshot(const EngineIn& in) {
	int w = 1 - snapIdx.load(std::memory_order_relaxed);
	Snapshot& s = snaps[w];
	int t = selTrack;
	s.track = t;
	s.lane = selLane;
	s.page = page;
	s.selStep = selStep;
	s.selPoint = selPoint;
	s.selUserChord = selUserChord;
	s.hi = hi;
	s.shift = in.shift;
	s.running = in.running;
	s.expander = in.expander;
	s.bpm = (float) (tr[t].rate * 15.0);
	s.pat = play[t];
	s.editPat = edit[t];
	for (int l = 0; l < NL; l++) {
		s.curIdx[l] = tr[t].lanes[l].curIdx;
		s.laneVal[l] = tr[t].lanes[l].val;
	}
	for (int u = 0; u < NT; u++) {
		s.ts[u] = ts[u];
		s.queued[u] = tr[u].queued;
		s.frozen[u] = tr[u].frozen;
		s.gateHigh[u] = gateHigh(tr[u]);
	}
	for (int j = 0; j < NPOINT; j++)
		s.morphW[j] = morphW[t][j];
	s.song = songs[songIdx];
	s.songIdx = songIdx;
	s.songRow = songRow;
	s.songOn = songOn;
	s.scales = scales;
	std::memcpy(s.msg, msg, sizeof(msg));
	s.gestArmed = gestArmed;
	s.gestRec = gestRec;
	std::memcpy(s.gestTrace, gestTrace, sizeof(gestTrace));
	s.gestHead = gestHead;
	s.undoCount = undoCount[t];
	snapIdx.store(w, std::memory_order_release);
}

} // namespace wa
