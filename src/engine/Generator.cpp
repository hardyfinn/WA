#include "Generator.hpp"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace wa {

namespace {

float clampf(float x, float a, float b) {
	return x < a ? a : (x > b ? b : x);
}

int clampi(int x, int a, int b) {
	return x < a ? a : (x > b ? b : x);
}

int floorModI(int a, int b) {
	int m = a % b;
	return m < 0 ? m + b : m;
}

/** Choose `count` distinct loop positions out of L, skipping pinned steps. Writes a mask. */
void choosePositions(const Pattern& p, int lane, float amount, Rng& rng, bool sel[NS]) {
	const LaneSettings& ls = p.lanes[lane];
	int L = ls.length;
	int order[NS];
	int n = 0;
	for (int i = 0; i < L; i++) {
		sel[i] = false;
		if (p.steps[lane][loopIndex(ls, i)].pin == Pin::None)
			order[n++] = i;
	}
	for (int i = L; i < NS; i++)
		sel[i] = false;
	// Fisher-Yates shuffle
	for (int i = n - 1; i > 0; i--) {
		int j = rng.uniformInt(i + 1);
		std::swap(order[i], order[j]);
	}
	int count = (int) std::lround(clampf(amount, 0.f, 1.f) * L);
	if (count > n)
		count = n;
	for (int i = 0; i < count; i++)
		sel[order[i]] = true;
}

Role roleForPosition(int i) {
	if (i % 4 == 0)
		return Role::Anchor;
	if (i % 2 == 0)
		return Role::Pulse;
	return Role::Passing;
}

float genValue(int lane, int i, int L, float c, Rng& rng, float walkPrev, float ph1, float ph2) {
	switch (lane) {
		case L_OCT: {
			float r = rng.uniform();
			int sign = rng.uniform() < 0.5f ? -1 : 1;
			if (r < 0.1f * c)
				return (float) (2 * sign);
			if (r < 0.5f * c)
				return (float) sign;
			return 0.f;
		}
		case L_CHORD:
			return rng.uniform() < 0.5f * c ? (float) (1 + rng.uniformInt(5)) : 0.f;
		case L_VEL:
			return clampf(7.5f + (rng.uniform() * 2.f - 1.f) * 2.5f * c, 0.f, 10.f);
		case L_LEN:
			return std::round(10.f + rng.uniform() * 190.f * c);
		case L_RAT: {
			if (rng.uniform() < 0.3f * c) {
				int maxC = c > 0.8f ? 8 : 4;
				int count = 2 + rng.uniformInt(maxC - 1);
				return ratchetPack(count, rng.uniformInt(3));
			}
			return 1.f;
		}
		case L_MODA:
		case L_MODB: {
			float u = L > 0 ? (float) i / L : 0.f;
			float smooth = 3.f * std::sin(2.f * (float) M_PI * u + ph1) + 1.5f * std::sin(4.f * (float) M_PI * u + ph2);
			float jag = rng.uniform() * 10.f - 5.f;
			return clampf((1.f - c) * smooth + c * jag, -5.f, 5.f);
		}
		case L_NOTE: {
			int step;
			if (rng.uniform() < 1.f - c)
				step = 1;
			else
				step = 2 + rng.uniformInt(4);
			if (rng.uniform() < 0.5f)
				step = -step;
			int v = (int) walkPrev + step;
			if (v < 0)
				v = -v;
			if (v > 15)
				v = 30 - v;
			return (float) clampi(v, 0, 15);
		}
		default:
			return 0.f;
	}
}

} // namespace

void genLane(Pattern& p, int lane, float c, float amount, Rng& rng) {
	c = clampf(c, 0.f, 1.f);
	LaneSettings& ls = p.lanes[lane];
	int L = ls.length;
	bool sel[NS];
	choosePositions(p, lane, amount, rng, sel);

	if (lane == L_GATE) {
		float density = 0.15f + 0.7f * c;
		for (int i = 0; i < L; i++) {
			if (!sel[i])
				continue;
			Step& s = p.steps[L_GATE][loopIndex(ls, i)];
			float w = (i % 4 == 0) ? 1.5f : 1.f;
			bool on = rng.uniform() < std::min(1.f, density * w);
			s.value = on ? 1.f : 0.f;
			s.role = roleForPosition(i);
			if (on && s.role == Role::Passing && rng.uniform() < 0.25f)
				s.role = Role::Tension;
			s.accent = on && s.role == Role::Anchor && rng.uniform() < 0.5f * c;
		}
		return;
	}

	float ph1 = rng.uniform() * 6.2832f;
	float ph2 = rng.uniform() * 6.2832f;
	float walk = (float) rng.uniformInt(8);
	for (int i = 0; i < L; i++) {
		float v = genValue(lane, i, L, c, rng, walk, ph1, ph2);
		if (lane == L_NOTE)
			walk = v;
		if (!sel[i])
			continue;
		Step& s = p.steps[lane][loopIndex(ls, i)];
		if (lane == L_VEL) {
			// Anchors get a little more weight
			const LaneSettings& gl = p.lanes[L_GATE];
			const Step& g = p.steps[L_GATE][loopIndex(gl, i % gl.length)];
			if (g.role == Role::Anchor)
				v = std::min(10.f, v + 1.f);
		}
		s.value = v;
		if (lane == L_MODA || lane == L_MODB)
			s.slide = c < 0.5f;
		if (lane == L_NOTE)
			s.slide = false;
	}
}

void genTrack(Pattern& p, float c, float amount, Rng& rng) {
	if (c > 0.5f) {
		const int lens[8] = {3, 5, 6, 7, 9, 11, 12, 13};
		for (int l = 1; l < NL; l++)
			p.lanes[l].length = (uint8_t) lens[rng.uniformInt(8)];
	}
	for (int l = 0; l < NL; l++)
		genLane(p, l, c, amount, rng);
}

void genSingleStep(Pattern& p, int lane, int idx, float c, Rng& rng) {
	Step& s = p.steps[lane][idx];
	if (s.pin != Pin::None)
		return;
	switch (lane) {
		case L_GATE:
			s.value = s.value > 0.5f ? 0.f : 1.f;
			break;
		case L_NOTE:
			s.value = genValue(L_NOTE, 0, 1, c, rng, s.value, 0, 0);
			break;
		default:
			s.value = genValue(lane, 0, 1, c, rng, s.value, rng.uniform() * 6.28f, 0.f);
			break;
	}
}

// ---------------------------------------------------------------------------
// Rules (§7.3)

namespace {

struct RuleState {
	const RulesConfig* r;
	int n;            // scale size
	int m;            // gated note count
	int target[NS];   // contour target per note (or -1 for free)
	bool useContour, useLeap, useRepeat, useEnd;
	uint8_t intervals;
	int out[NS];
	int nodes;
	Rng* rng;
};

bool endOk(const RuleState& st, int d) {
	if (!st.useEnd)
		return true;
	int m = floorModI(d, st.n);
	switch (st.r->endOn) {
		case EndOn::Root: return m == 0;
		case EndOn::ChordTone: return m == 0 || m == 2 || m == 4;
		default: return true;
	}
}

bool candidateOk(const RuleState& st, int j, int d) {
	if (j > 0) {
		int prev = st.out[j - 1];
		int delta = d - prev;
		int ad = std::abs(delta);
		if (ad > 7 || !(st.intervals & (1 << ad)))
			return false;
		if (st.useRepeat && delta == 0) {
			int run = 1;
			for (int k = j - 1; k > 0 && st.out[k] == st.out[k - 1]; k--)
				run++;
			if (run + 1 > st.r->maxRepeat)
				return false;
		}
		if (st.useLeap && j > 1) {
			int pd = prev - st.out[j - 2];
			if (std::abs(pd) > 2) {
				if (!(ad == 1 && ((pd > 0 && delta < 0) || (pd < 0 && delta > 0))))
					return false;
			}
		}
	}
	if (j == st.m - 1 && !endOk(st, d))
		return false;
	return true;
}

bool dfs(RuleState& st, int j) {
	if (j == st.m)
		return true;
	if (++st.nodes > 5000)
		return false;
	int lo = st.r->lo, hi = st.r->hi;
	if (hi < lo)
		std::swap(lo, hi);
	int cands[16];
	float keys[16];
	int nc = 0;
	for (int d = lo; d <= hi && nc < 16; d++) {
		if (!candidateOk(st, j, d))
			continue;
		cands[nc] = d;
		float jitter = st.rng->uniform() * 2.f - 1.f;
		if (st.useContour && st.target[j] >= 0)
			keys[nc] = std::fabs((float) (d - st.target[j])) + jitter;
		else
			keys[nc] = st.rng->uniform();
		nc++;
	}
	// sort by key (insertion sort, nc <= 16)
	for (int a = 1; a < nc; a++) {
		for (int b = a; b > 0 && keys[b] < keys[b - 1]; b--) {
			std::swap(keys[b], keys[b - 1]);
			std::swap(cands[b], cands[b - 1]);
		}
	}
	for (int k = 0; k < nc; k++) {
		st.out[j] = cands[k];
		if (dfs(st, j + 1))
			return true;
		if (st.nodes > 5000)
			return false;
	}
	return false;
}

int contourTarget(Contour c, int j, int m, int lo, int hi) {
	float u = m > 1 ? (float) j / (m - 1) : 0.f;
	float span = (float) (hi - lo);
	switch (c) {
		case Contour::Rise: return lo + (int) std::lround(u * span);
		case Contour::Fall: return hi - (int) std::lround(u * span);
		case Contour::Arch: {
			float a = u < 0.6f ? u / 0.6f : (1.f - u) / 0.4f;
			return lo + (int) std::lround(a * span);
		}
		case Contour::Valley: {
			float a = u < 0.6f ? u / 0.6f : (1.f - u) / 0.4f;
			return hi - (int) std::lround(a * span);
		}
		case Contour::Zigzag: {
			float mid = (lo + hi) * 0.5f;
			return (int) std::lround(mid + ((j % 2) ? -1.f : 1.f) * span * 0.25f);
		}
		default: return -1;
	}
}

} // namespace

bool rulesGen(Pattern& p, const ScaleDef& sc, Rng& rng, char* relaxed) {
	relaxed[0] = 0;
	const RulesConfig& r = p.rules;
	LaneSettings& gl = p.lanes[L_GATE];
	int L = gl.length;

	// Notes align with gates: the Note lane copies the Gate lane's timing.
	LaneSettings& nl = p.lanes[L_NOTE];
	nl.start = gl.start;
	nl.length = gl.length;
	nl.ratio = gl.ratio;
	nl.dir = gl.dir;
	nl.skip = gl.skip;
	nl.clock = gl.clock;
	nl.extDiv = gl.extDiv;

	// 1. Gates
	bool on[NS] = {};
	int pinnedOn = 0;
	int mustCount = 0;
	for (int i = 0; i < L; i++) {
		int idx = loopIndex(gl, i);
		const Step& s = p.steps[L_GATE][idx];
		if (s.pin != Pin::None) {
			on[i] = s.value > 0.5f;
			pinnedOn += on[i];
		}
		else if (r.mustHit & (1u << idx)) {
			on[i] = true;
			mustCount++;
		}
	}
	int dMin = std::min(r.densMin, r.densMax), dMax = std::max(r.densMin, r.densMax);
	int kMin = (int) std::ceil(dMin / 100.f * L);
	int kMax = (int) std::floor(dMax / 100.f * L);
	if (kMax < kMin)
		kMax = kMin;
	int k = kMin + rng.uniformInt(kMax - kMin + 1);
	if (k < 1)
		k = 1;
	int have = pinnedOn + mustCount;
	while (have < k) {
		float total = 0.f;
		for (int i = 0; i < L; i++)
			if (!on[i] && p.steps[L_GATE][loopIndex(gl, i)].pin == Pin::None)
				total += (i % 4 == 0) ? 1.5f : 1.f;
		if (total <= 0.f)
			break;
		float u = rng.uniform() * total;
		for (int i = 0; i < L; i++) {
			if (on[i] || p.steps[L_GATE][loopIndex(gl, i)].pin != Pin::None)
				continue;
			u -= (i % 4 == 0) ? 1.5f : 1.f;
			if (u <= 0.f) {
				on[i] = true;
				break;
			}
		}
		have++;
	}
	int gated[NS];
	int m = 0;
	for (int i = 0; i < L; i++) {
		Step& s = p.steps[L_GATE][loopIndex(gl, i)];
		if (s.pin == Pin::None) {
			s.value = on[i] ? 1.f : 0.f;
			s.role = roleForPosition(i);
			if (on[i] && s.role == Role::Passing && rng.uniform() < 0.25f)
				s.role = Role::Tension;
		}
		if (on[i])
			gated[m++] = i;
	}
	if (m == 0)
		return false;

	// 2. Notes: depth-first search, relaxing rules when the search fails
	RuleState st;
	st.r = &r;
	st.n = sc.n > 0 ? sc.n : 7;
	st.m = m;
	st.rng = &rng;
	st.intervals = r.intervals ? r.intervals : 0x06;
	st.useContour = r.contour != Contour::Free;
	st.useLeap = r.leapRecovery;
	st.useRepeat = true;
	st.useEnd = r.endOn != EndOn::Any;
	int lo = std::min(r.lo, r.hi), hi = std::max(r.lo, r.hi);
	for (int j = 0; j < m; j++)
		st.target[j] = contourTarget(r.contour, j, m, lo, hi);

	const char* relaxNames[5] = {"contour", "leaps", "repeats", "end", "intervals"};
	bool ok = false;
	int relaxStage = 0;
	while (true) {
		st.nodes = 0;
		if (dfs(st, 0)) {
			ok = true;
			break;
		}
		if (relaxStage >= 5)
			break;
		switch (relaxStage) {
			case 0: st.useContour = false; break;
			case 1: st.useLeap = false; break;
			case 2: st.useRepeat = false; break;
			case 3: st.useEnd = false; break;
			case 4: st.intervals |= 0x07; break; // unison, 2nd and 3rd
		}
		std::snprintf(relaxed, 32, "RELAXED: %s", relaxNames[relaxStage]);
		relaxStage++;
	}
	if (!ok) {
		// Fall back to a stepwise walk inside the range
		int d = lo;
		for (int j = 0; j < m; j++) {
			st.out[j] = d;
			d = clampi(d + (rng.uniform() < 0.5f ? -1 : 1), lo, hi);
		}
		std::snprintf(relaxed, 32, "RELAXED: all");
	}

	// Write notes; ungated positions hold the previous note
	int j = 0;
	int cur = st.out[0];
	for (int i = 0; i < L; i++) {
		if (j < m && gated[j] == i)
			cur = st.out[j++];
		Step& s = p.steps[L_NOTE][loopIndex(nl, i)];
		if (s.pin == Pin::None) {
			s.value = (float) cur;
			s.slide = false;
		}
	}
	return true;
}

bool rulesSatisfied(const Pattern& p, const ScaleDef& sc) {
	const RulesConfig& r = p.rules;
	const LaneSettings& gl = p.lanes[L_GATE];
	const LaneSettings& nl = p.lanes[L_NOTE];
	int L = gl.length;
	int n = sc.n > 0 ? sc.n : 7;
	int notes[NS];
	int m = 0;
	int gates = 0;
	for (int i = 0; i < L; i++) {
		int idx = loopIndex(gl, i);
		bool g = p.steps[L_GATE][idx].value > 0.5f;
		if ((r.mustHit & (1u << idx)) && !g)
			return false;
		if (g) {
			gates++;
			notes[m++] = (int) p.steps[L_NOTE][loopIndex(nl, i)].value;
		}
	}
	int dMin = std::min(r.densMin, r.densMax), dMax = std::max(r.densMin, r.densMax);
	if (gates < (int) std::ceil(dMin / 100.f * L) && gates > 1)
		return false;
	if (gates > std::max((int) std::floor(dMax / 100.f * L), (int) std::ceil(dMin / 100.f * L)) && gates > 1)
		return false;
	int lo = std::min(r.lo, r.hi), hi = std::max(r.lo, r.hi);
	int run = 1;
	for (int j = 0; j < m; j++) {
		if (notes[j] < lo || notes[j] > hi)
			return false;
		if (j > 0) {
			int d = notes[j] - notes[j - 1];
			int ad = std::abs(d);
			if (ad > 7 || !(r.intervals & (1 << ad)))
				return false;
			run = (d == 0) ? run + 1 : 1;
			if (run > r.maxRepeat)
				return false;
			if (r.leapRecovery && j > 1) {
				int pd = notes[j - 1] - notes[j - 2];
				if (std::abs(pd) > 2 && !(ad == 1 && ((pd > 0) != (d > 0))))
					return false;
			}
		}
	}
	if (m > 0) {
		int e = floorModI(notes[m - 1], n);
		if (r.endOn == EndOn::Root && e != 0)
			return false;
		if (r.endOn == EndOn::ChordTone && !(e == 0 || e == 2 || e == 4))
			return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Call and answer (§7.4)

void deriveAnswer(Pattern& dst, const Pattern& src, int scaleN, uint64_t seed) {
	const AnswerConfig& a = dst.answer;
	if (a.mode == AnswerMode::Off)
		return;
	int n = scaleN > 0 ? scaleN : 7;
	for (int l : {L_GATE, L_NOTE, L_OCT}) {
		dst.lanes[l] = src.lanes[l];
		dst.pathLen[l] = 0;
	}
	const LaneSettings& sg = src.lanes[L_GATE];
	const LaneSettings& sn = src.lanes[L_NOTE];
	const LaneSettings& so = src.lanes[L_OCT];
	int L = sg.length;
	int D = a.displace;

	auto srcGate = [&](int i) -> const Step& { return src.steps[L_GATE][loopIndex(sg, floorModI(i, L))]; };
	auto srcNote = [&](int i) -> int { return (int) src.steps[L_NOTE][loopIndex(sn, floorModI(i, sn.length))].value; };
	auto srcOct = [&](int i) -> const Step& { return src.steps[L_OCT][loopIndex(so, floorModI(i, so.length))]; };

	auto setGate = [&](int i, const Step& s, bool on) {
		Step& d = dst.steps[L_GATE][loopIndex(sg, i)];
		d = s;
		d.value = on ? 1.f : 0.f;
		d.pin = Pin::None;
	};
	// Octave lane: copied as is
	for (int i = 0; i < NS; i++) {
		dst.steps[L_OCT][i] = src.steps[L_OCT][i];
		dst.steps[L_OCT][i].pin = Pin::None;
	}
	(void) srcOct;

	switch (a.mode) {
		case AnswerMode::Echo:
		case AnswerMode::Mirror:
		case AnswerMode::Retro: {
			int d0 = 0;
			for (int i = 0; i < L; i++)
				if (srcGate(i).value > 0.5f) {
					d0 = srcNote(i);
					break;
				}
			for (int i = 0; i < L; i++) {
				int si = (a.mode == AnswerMode::Retro) ? (L - 1 - floorModI(i - D, L)) : (i - D);
				const Step& g = srcGate(si);
				setGate(i, g, g.value > 0.5f);
			}
			// Note lane length follows the gate lane so rotated notes line up
			dst.lanes[L_NOTE].length = (uint8_t) L;
			dst.lanes[L_NOTE].start = sg.start;
			for (int i = 0; i < L; i++) {
				int si = (a.mode == AnswerMode::Retro) ? (L - 1 - floorModI(i - D, L)) : (i - D);
				int note = srcNote(si);
				if (a.mode == AnswerMode::Mirror)
					note = 2 * d0 - note;
				Step& d = dst.steps[L_NOTE][loopIndex(sg, i)];
				d = Step();
				d.value = (float) clampi(note + a.transpose, 0, 15);
			}
			break;
		}
		case AnswerMode::Resolve: {
			dst.lanes[L_NOTE].length = (uint8_t) L;
			dst.lanes[L_NOTE].start = sg.start;
			bool gate[NS];
			int count = 0;
			for (int i = 0; i < L; i++) {
				bool srcOn = srcGate(i).value > 0.5f;
				gate[i] = !srcOn && hash01(seed, 77, (uint64_t) i) * 100.f < a.fill;
				count += gate[i];
			}
			int last = -1;
			for (int i = 0; i < L; i++)
				if (gate[i])
					last = i;
			int prev = 0;
			for (int i = 0; i < L; i++)
				if (srcGate(i).value > 0.5f) {
					prev = srcNote(i);
					break;
				}
			for (int i = 0; i < L; i++) {
				Step g;
				g.role = Role::Passing;
				setGate(i, g, gate[i]);
				if (gate[i]) {
					// most recent source note at or before i (cyclic)
					int recent = prev;
					for (int k = 0; k < L; k++) {
						int si = floorModI(i - k, L);
						if (srcGate(si).value > 0.5f) {
							recent = srcNote(si);
							break;
						}
					}
					int target = recent;
					int best = 1000;
					for (int t : {recent, recent + 2, recent + 4, recent - n + 2, recent - n + 4}) {
						int dist = std::abs(t - prev);
						if (dist < best) {
							best = dist;
							target = t;
						}
					}
					if (i == last) {
						// land on degree 0 or 4 (mod n), whichever is closest
						int bestT = prev;
						int bd = 1000;
						for (int t = prev - n; t <= prev + n; t++) {
							int mm = floorModI(t, n);
							if ((mm == 0 || mm == 4 % n) && std::abs(t - prev) < bd) {
								bd = std::abs(t - prev);
								bestT = t;
							}
						}
						prev = bestT;
					}
					else if (target > prev)
						prev++;
					else if (target < prev)
						prev--;
					prev = clampi(prev, 0, 15);
				}
				Step& d = dst.steps[L_NOTE][loopIndex(sg, i)];
				d = Step();
				d.value = (float) clampi(prev + a.transpose, 0, 15);
			}
			(void) count;
			break;
		}
		default: break;
	}
}

// ---------------------------------------------------------------------------
// Morph (§8)

void morphWeights(const MorphPoint pts[NPOINT], float x, float y, float w[NPOINT]) {
	float sum = 0.f;
	for (int j = 0; j < NPOINT; j++) {
		w[j] = 0.f;
		if (!pts[j].active || pts[j].slot < 0)
			continue;
		float dx = pts[j].x - x, dy = pts[j].y - y;
		w[j] = 1.f / (dx * dx + dy * dy + 1e-4f);
		sum += w[j];
	}
	if (sum <= 0.f)
		return;
	float sum2 = 0.f;
	for (int j = 0; j < NPOINT; j++) {
		w[j] /= sum;
		if (w[j] < 0.02f)
			w[j] = 0.f;
		sum2 += w[j];
	}
	if (sum2 > 0.f)
		for (int j = 0; j < NPOINT; j++)
			w[j] /= sum2;
}

void morphRender(Pattern& out, const Pattern* const pats[NPOINT], const float w[NPOINT], uint64_t seed) {
	int best = -1;
	float bw = 0.f;
	for (int j = 0; j < NPOINT; j++)
		if (pats[j] && w[j] > bw) {
			bw = w[j];
			best = j;
		}
	if (best < 0)
		return;
	const Pattern& B = *pats[best];
	// Track-wide and lane settings from the strongest point
	for (int l = 0; l < NL; l++) {
		out.lanes[l] = B.lanes[l];
		out.pathLen[l] = B.pathLen[l];
		for (int s = 0; s < NS; s++)
			out.path[l][s] = B.path[l][s];
	}
	out.root = B.root;
	out.scale = B.scale;
	out.followT1 = B.followT1;
	out.transpose = B.transpose;
	out.swing = B.swing;
	out.seed = B.seed;
	out.rules = B.rules;
	out.answer = B.answer;
	for (int i = 0; i < NMATRIX; i++)
		out.matrix[i] = B.matrix[i];

	for (int l = 0; l < NL; l++) {
		bool continuous = (l == L_VEL || l == L_LEN || l == L_MODA || l == L_MODB);
		for (int s = 0; s < NS; s++) {
			// weighted choice with a stable per-step number
			float u = hash01(seed, (uint64_t) l, (uint64_t) s, 991);
			int pick = best;
			float acc = 0.f;
			for (int j = 0; j < NPOINT; j++) {
				if (!pats[j] || w[j] <= 0.f)
					continue;
				acc += w[j];
				if (u < acc) {
					pick = j;
					break;
				}
			}
			out.steps[l][s] = pats[pick]->steps[l][s];
			if (continuous) {
				float v = 0.f;
				for (int j = 0; j < NPOINT; j++)
					if (pats[j] && w[j] > 0.f)
						v += w[j] * pats[j]->steps[l][s].value;
				out.steps[l][s].value = v;
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Gesture (§12)

void fitGesture(Pattern& p, int lane, const float* means, int n) {
	const LaneSettings& ls = p.lanes[lane];
	int L = std::min<int>(n, ls.length);
	for (int i = 0; i < L; i++) {
		Step& s = p.steps[lane][loopIndex(ls, i)];
		if (s.pin != Pin::None)
			continue;
		float m = clampf(means[i], 0.f, 1.f);
		switch (lane) {
			case L_GATE: s.value = m > 0.5f ? 1.f : 0.f; break;
			case L_NOTE: {
				int lo = std::min(p.rules.lo, p.rules.hi), hi = std::max(p.rules.lo, p.rules.hi);
				s.value = (float) (lo + (int) std::lround(m * (hi - lo)));
				break;
			}
			case L_OCT: s.value = std::round(-3.f + 6.f * m); break;
			case L_CHORD: s.value = std::round(8.f * m); break;
			case L_RAT: s.value = ratchetPack(1 + (int) std::lround(7.f * m), ratchetRamp(s.value)); break;
			case L_VEL: s.value = 10.f * m; break;
			case L_LEN: s.value = std::round(1.f + 399.f * m); break;
			default: s.value = -5.f + 10.f * m; break;
		}
	}
}

} // namespace wa
