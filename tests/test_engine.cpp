// Unit tests for the LATTICE engine (spec §14). Run with `make test`.
#include "doctest.h"
#include "Engine.hpp"
#include "Generator.hpp"
#include <memory>
#include <vector>
#include <cmath>

using namespace wa;

namespace {

// 1 kHz simulation at 120 BPM: 8 base steps per second = 125 samples per base step.
const float DT = 1.f / 1000.f;

struct Sim {
	std::unique_ptr<Engine> e{new Engine()};
	EngineIn in;
	EngineOut out;
	std::vector<int> gateOnSample[NT];
	std::vector<float> pitchAtGate[NT];
	bool prevGate[NT] = {};
	int sample = 0;

	Sim() {
		in.dt = DT;
		in.bpm = 120.f;
		for (int t = 0; t < NT; t++) {
			for (int s = 0; s < NS; s++)
				e->edit[t].steps[L_GATE][s].value = 0.f;
			e->markEdited(t);
		}
	}
	void run(int samples) {
		for (int i = 0; i < samples; i++) {
			e->process(in, out);
			in.reset = false;
			for (int t = 0; t < NT; t++) {
				bool g = out.gate[t][0] > 5.f;
				if (g && !prevGate[t]) {
					gateOnSample[t].push_back(sample);
					pitchAtGate[t].push_back(out.pitch[t][0]);
				}
				prevGate[t] = g;
			}
			// clear one-shot buttons
			EngineIn fresh;
			fresh.dt = in.dt;
			fresh.bpm = in.bpm;
			fresh.running = in.running;
			fresh.clockConn = in.clockConn;
			fresh.ppStep = in.ppStep;
			fresh.transposeV = in.transposeV;
			fresh.character = in.character;
			fresh.amount = in.amount;
			for (int t = 0; t < NT; t++) {
				fresh.arcCV[t] = in.arcCV[t];
				fresh.tclkConn[t] = in.tclkConn[t];
			}
			fresh.pinHeld = in.pinHeld;
			fresh.fillHeld = in.fillHeld;
			fresh.freezeHeld = in.freezeHeld;
			in = fresh;
			sample++;
		}
	}
	Pattern& P(int t = 0) { return e->edit[t]; }
	void commit(int t = 0) { e->markEdited(t); }
};

} // namespace

TEST_CASE("scale degrees to pitch") {
	ScaleSet us;
	ScaleDef major = getScale(0, us);
	CHECK(degreeToSemis(major, 0) == 0);
	CHECK(degreeToSemis(major, 2) == 4);
	CHECK(degreeToSemis(major, 7) == 12);
	CHECK(degreeToSemis(major, -1) == -1);
	CHECK(degreeToSemis(major, -7) == -12);
	ScaleDef pelog = getScale(16, us);
	CHECK(pelog.n == 5);
	CHECK(degreeToSemis(pelog, 3) == 7);
	CHECK(degreeToSemis(pelog, 5) == 12);
	CHECK(degreeToSemis(pelog, -2) == -5);
	// quantise: C# in C major goes down to C (tie goes down), D# to D
	CHECK(quantizeSemis(major, 0, 1) == 0);
	CHECK(quantizeSemis(major, 0, 3) == 2);
	CHECK(quantizeSemis(major, 0, 6) == 5);
}

TEST_CASE("chords stay in key") {
	ScaleSet us;
	CHECK(chordSize(2, us) == 3);
	CHECK(chordDegree(2, 1, us) == 2);
	CHECK(chordSize(0, us) == 1);
	CHECK(chordSize(6, us) == us.userChordSize[0]);
}

TEST_CASE("gates follow the internal clock") {
	Sim s;
	for (int i = 0; i < 16; i += 4)
		s.P().steps[L_GATE][i].value = 1.f;
	s.commit();
	s.run(2000); // 16 base steps
	REQUIRE(s.gateOnSample[0].size() >= 4);
	// every 4 base steps = 500 samples apart
	for (size_t i = 1; i < 4; i++)
		CHECK(std::abs(s.gateOnSample[0][i] - s.gateOnSample[0][i - 1] - 500) <= 2);
}

TEST_CASE("lane ratio x3 moves three times per base step") {
	Sim s;
	s.P().lanes[L_GATE].ratio = 1; // x3
	for (int i = 0; i < NS; i++)
		s.P().steps[L_GATE][i].value = 1.f;
	s.P().steps[L_LEN][0].value = 30.f;
	for (int i = 0; i < NS; i++)
		s.P().steps[L_LEN][i].value = 30.f;
	s.commit();
	s.run(1000); // 8 base steps
	CHECK(s.gateOnSample[0].size() >= 23);
	CHECK(s.gateOnSample[0].size() <= 25);
}

TEST_CASE("polymeter: note lane of 3 against gate lane of 4") {
	Sim s;
	auto& p = s.P();
	p.lanes[L_GATE].length = 4;
	for (int i = 0; i < 4; i++)
		p.steps[L_GATE][i].value = 1.f;
	p.lanes[L_NOTE].length = 3;
	p.steps[L_NOTE][0].value = 0;
	p.steps[L_NOTE][1].value = 2;
	p.steps[L_NOTE][2].value = 4;
	s.commit();
	s.run(125 * 12);
	REQUIRE(s.pitchAtGate[0].size() >= 12);
	// notes cycle C E G C E G ...
	for (size_t i = 0; i < 12; i++) {
		float expected = (float) degreeToSemis(getScale(0, ScaleSet()), (int) (i % 3) * 2) / 12.f;
		CHECK(s.pitchAtGate[0][i] == doctest::Approx(expected));
	}
}

TEST_CASE("pendulum and skip orders") {
	Sim s;
	auto& p = s.P();
	p.lanes[L_GATE].length = 16;
	for (int i = 0; i < 16; i++)
		p.steps[L_GATE][i].value = 1.f;
	for (int i = 0; i < 4; i++)
		p.steps[L_NOTE][i].value = (float) i;
	p.lanes[L_NOTE].length = 4;
	p.lanes[L_NOTE].dir = Dir::Pendulum;
	s.commit();
	s.run(125 * 8);
	ScaleDef sc = getScale(0, ScaleSet());
	int expect[8] = {0, 1, 2, 3, 2, 1, 0, 1};
	REQUIRE(s.pitchAtGate[0].size() >= 8);
	for (int i = 0; i < 8; i++)
		CHECK(s.pitchAtGate[0][i] == doctest::Approx(degreeToSemis(sc, expect[i]) / 12.f));

	Sim k;
	auto& q = k.P();
	for (int i = 0; i < 16; i++)
		q.steps[L_GATE][i].value = 1.f;
	q.lanes[L_NOTE].length = 5;
	q.lanes[L_NOTE].dir = Dir::Skip;
	q.lanes[L_NOTE].skip = 2;
	for (int i = 0; i < 5; i++)
		q.steps[L_NOTE][i].value = (float) i;
	k.commit();
	k.run(125 * 5);
	int ex2[5] = {0, 2, 4, 1, 3};
	REQUIRE(k.pitchAtGate[0].size() >= 5);
	for (int i = 0; i < 5; i++)
		CHECK(k.pitchAtGate[0][i] == doctest::Approx(degreeToSemis(sc, ex2[i]) / 12.f));
}

TEST_CASE("A:B condition plays on pass A of every B") {
	Sim s;
	auto& p = s.P();
	p.lanes[L_GATE].length = 4;
	p.steps[L_GATE][0].value = 1.f;
	p.steps[L_GATE][0].cond = Cond::AofB;
	p.steps[L_GATE][0].condA = 2;
	p.steps[L_GATE][0].condB = 2;
	s.commit();
	s.run(125 * 16); // 4 passes
	CHECK(s.gateOnSample[0].size() == 2);
}

TEST_CASE("alt value replaces a failed note step, hold otherwise") {
	Sim s;
	auto& p = s.P();
	for (int i = 0; i < 16; i++)
		p.steps[L_GATE][i].value = 1.f;
	p.lanes[L_NOTE].length = 2;
	p.steps[L_NOTE][0].value = 0;
	p.steps[L_NOTE][1].value = 4;
	p.steps[L_NOTE][1].prob = 0;
	p.steps[L_NOTE][1].hasAlt = true;
	p.steps[L_NOTE][1].alt = 2;
	s.commit();
	s.run(125 * 4);
	ScaleDef sc = getScale(0, ScaleSet());
	REQUIRE(s.pitchAtGate[0].size() >= 4);
	CHECK(s.pitchAtGate[0][1] == doctest::Approx(degreeToSemis(sc, 2) / 12.f));
	CHECK(s.pitchAtGate[0][3] == doctest::Approx(degreeToSemis(sc, 2) / 12.f));
}

TEST_CASE("negative nudge fires before the step boundary") {
	Sim s;
	auto& p = s.P();
	p.steps[L_GATE][4].value = 1.f;
	p.steps[L_GATE][4].nudge = -40;
	s.commit();
	s.run(125 * 8);
	REQUIRE(s.gateOnSample[0].size() >= 1);
	// step 4 starts at sample 500; -40% of 125 = 50 samples early
	CHECK(std::abs(s.gateOnSample[0][0] - 450) <= 2);
}

TEST_CASE("ratchets split the step") {
	Sim s;
	auto& p = s.P();
	p.steps[L_GATE][0].value = 1.f;
	p.steps[L_RAT][0].value = ratchetPack(4, 0);
	s.commit();
	s.run(125);
	CHECK(s.gateOnSample[0].size() == 4);
}

TEST_CASE("chord step outputs three channels in key") {
	Sim s;
	auto& p = s.P();
	p.steps[L_GATE][0].value = 1.f;
	for (int i = 0; i < NS; i++)
		p.steps[L_CHORD][i].value = 2.f; // triad
	s.commit();
	s.run(20);
	CHECK(s.out.chans[0] == 3);
	CHECK(s.out.pitch[0][0] == doctest::Approx(0.f));
	CHECK(s.out.pitch[0][1] == doctest::Approx(4.f / 12.f));
	CHECK(s.out.pitch[0][2] == doctest::Approx(7.f / 12.f));
	CHECK(s.out.gate[0][2] > 5.f);
}

TEST_CASE("GEN is repeatable for the same seed and press count") {
	Pattern a, b;
	Rng r1 = Rng::fork(5, 0, 1, 0), r2 = Rng::fork(5, 0, 1, 0);
	genTrack(a, 0.8f, 1.f, r1);
	genTrack(b, 0.8f, 1.f, r2);
	for (int l = 0; l < NL; l++) {
		CHECK(a.lanes[l].length == b.lanes[l].length);
		for (int s = 0; s < NS; s++)
			CHECK(a.steps[l][s].value == b.steps[l][s].value);
	}
}

TEST_CASE("GEN amount 25% changes at most 4 of 16 steps and skips pinned steps") {
	Pattern p;
	for (int s = 0; s < NS; s++)
		p.steps[L_VEL][s].value = 1.234f;
	p.steps[L_VEL][3].pin = Pin::Fired;
	Rng r(7);
	genLane(p, L_VEL, 1.f, 0.25f, r);
	int changed = 0;
	for (int s = 0; s < 16; s++)
		changed += p.steps[L_VEL][s].value != 1.234f;
	CHECK(changed <= 4);
	CHECK(p.steps[L_VEL][3].value == 1.234f);
}

TEST_CASE("pinned dice freeze probability outcomes") {
	Sim s;
	auto& p = s.P();
	for (int i = 0; i < 16; i++) {
		p.steps[L_GATE][i].value = 1.f;
		p.steps[L_GATE][i].prob = 50;
	}
	s.commit();
	s.in.pinHeld = true;
	s.run(125 * 17);
	s.in.pinHeld = false;
	int pinned = 0;
	for (int i = 0; i < 16; i++)
		pinned += s.e->edit[0].steps[L_GATE][i].pin != Pin::None;
	CHECK(pinned == 16);
	// two more passes must repeat exactly
	size_t before = s.gateOnSample[0].size();
	s.run(125 * 16);
	size_t pass1 = s.gateOnSample[0].size() - before;
	s.run(125 * 16);
	size_t pass2 = s.gateOnSample[0].size() - before - pass1;
	CHECK(pass1 == pass2);
}

TEST_CASE("arc thins non-anchor steps") {
	Sim s;
	auto& p = s.P();
	for (int i = 0; i < 16; i++) {
		p.steps[L_GATE][i].value = 1.f;
		p.steps[L_GATE][i].role = (i % 4 == 0) ? Role::Anchor : Role::Tension;
	}
	s.commit();
	s.e->ts[0].arc = 0.f;
	s.run(125 * 16 - 2);
	CHECK(s.gateOnSample[0].size() == 4);
}

TEST_CASE("density drops tension before anchors") {
	Sim s;
	auto& p = s.P();
	for (int i = 0; i < 16; i++) {
		p.steps[L_GATE][i].value = 1.f;
		p.steps[L_GATE][i].role = (i % 2 == 0) ? Role::Anchor : Role::Tension;
	}
	s.commit();
	s.e->ts[0].density = -0.25f;
	s.run(125 * 16 - 2);
	CHECK(s.gateOnSample[0].size() == 8);
}

TEST_CASE("rules GEN satisfies its rules") {
	ScaleSet us;
	ScaleDef sc = getScale(0, us);
	int ok = 0;
	for (int seed = 0; seed < 200; seed++) {
		Pattern p;
		p.rules.intervals = 0x06; // 2nd, 3rd
		p.rules.contour = Contour::Arch;
		p.rules.endOn = EndOn::Root;
		p.rules.lo = 0;
		p.rules.hi = 10;
		Rng r((uint64_t) seed);
		char relaxed[32];
		REQUIRE(rulesGen(p, sc, r, relaxed));
		if (relaxed[0] == 0) {
			CHECK(rulesSatisfied(p, sc));
			ok++;
		}
	}
	CHECK(ok > 150);
}

TEST_CASE("impossible rules are relaxed, not stuck") {
	ScaleSet us;
	ScaleDef sc = getScale(0, us);
	Pattern p;
	p.rules.intervals = 0x80; // octave only
	p.rules.lo = 3;
	p.rules.hi = 5;            // octave leaps impossible inside this range
	Rng r(1);
	char relaxed[32];
	CHECK(rulesGen(p, sc, r, relaxed));
	CHECK(relaxed[0] != 0);
}

TEST_CASE("answer echo, mirror, retro, resolve") {
	Pattern src, dst;
	src.lanes[L_GATE].length = 8;
	src.lanes[L_NOTE].length = 8;
	for (int i = 0; i < 8; i++) {
		src.steps[L_GATE][i].value = (i % 2 == 0) ? 1.f : 0.f;
		src.steps[L_NOTE][i].value = (float) i;
	}
	dst.answer.mode = AnswerMode::Echo;
	dst.answer.displace = 1;
	deriveAnswer(dst, src, 7, 1);
	CHECK(dst.steps[L_GATE][1].value == 1.f);
	CHECK(dst.steps[L_GATE][0].value == 0.f);
	CHECK(dst.steps[L_NOTE][1].value == 0.f);

	dst.answer.mode = AnswerMode::Mirror;
	dst.answer.displace = 0;
	deriveAnswer(dst, src, 7, 1);
	CHECK(dst.steps[L_NOTE][2].value == 0.f); // 2*0 - 2 clamped
	CHECK(dst.steps[L_NOTE][0].value == 0.f);

	dst.answer.mode = AnswerMode::Retro;
	deriveAnswer(dst, src, 7, 1);
	CHECK(dst.steps[L_NOTE][0].value == 7.f);

	dst.answer.mode = AnswerMode::Resolve;
	dst.answer.fill = 100;
	deriveAnswer(dst, src, 7, 1);
	for (int i = 0; i < 8; i++)
		CHECK((dst.steps[L_GATE][i].value > 0.5f) == (i % 2 == 1));
	int lastNote = (int) dst.steps[L_NOTE][7].value;
	CHECK((lastNote % 7 == 0 || lastNote % 7 == 4));
}

TEST_CASE("morph weights and render") {
	MorphPoint pts[NPOINT];
	pts[0].active = true;
	pts[0].slot = 0;
	pts[0].x = 0;
	pts[0].y = 0;
	pts[1].active = true;
	pts[1].slot = 1;
	pts[1].x = 1;
	pts[1].y = 0;
	float w[NPOINT];
	morphWeights(pts, 0.f, 0.f, w);
	CHECK(w[0] == doctest::Approx(1.f));
	morphWeights(pts, 0.5f, 0.f, w);
	CHECK(w[0] == doctest::Approx(0.5f));
	float sum = 0;
	for (float x : w)
		sum += x;
	CHECK(sum == doctest::Approx(1.f));

	Pattern A, B, out;
	for (int s = 0; s < NS; s++) {
		A.steps[L_NOTE][s].value = 0;
		B.steps[L_NOTE][s].value = 7;
		A.steps[L_VEL][s].value = 0;
		B.steps[L_VEL][s].value = 10;
	}
	const Pattern* pats[NPOINT] = {&A, &B};
	// at A's position, the render equals A
	morphWeights(pts, 0.f, 0.f, w);
	morphRender(out, pats, w, 3);
	for (int s = 0; s < NS; s++)
		CHECK(out.steps[L_NOTE][s].value == 0.f);
	// moving towards B: the set of B notes only grows
	int prevCount = 0;
	for (int i = 0; i <= 10; i++) {
		morphWeights(pts, i / 10.f, 0.f, w);
		morphRender(out, pats, w, 3);
		int count = 0;
		for (int s = 0; s < NS; s++)
			count += out.steps[L_NOTE][s].value == 7.f;
		CHECK(count >= prevCount);
		prevCount = count;
	}
	CHECK(prevCount == NS);
	morphWeights(pts, 0.5f, 0.f, w);
	morphRender(out, pats, w, 3);
	CHECK(out.steps[L_VEL][0].value == doctest::Approx(5.f));
}

TEST_CASE("gesture fit maps means onto a note lane in range") {
	Pattern p;
	p.lanes[L_NOTE].length = 4;
	p.rules.lo = 2;
	p.rules.hi = 6;
	float means[4] = {0.f, 0.5f, 1.f, 0.25f};
	fitGesture(p, L_NOTE, means, 4);
	CHECK(p.steps[L_NOTE][0].value == 2.f);
	CHECK(p.steps[L_NOTE][1].value == 4.f);
	CHECK(p.steps[L_NOTE][2].value == 6.f);
	CHECK(p.steps[L_NOTE][3].value == 3.f);
}

TEST_CASE("KEEP stores the playing pattern in the next free slot") {
	Sim s;
	s.P().steps[L_GATE][0].value = 1.f;
	s.commit();
	s.run(10);
	int slot = s.e->keepTrack(0);
	CHECK(slot == 1);
	CHECK(s.e->ts[0].used[1]);
	CHECK(s.e->slots[0][1].steps[L_GATE][0].value == 1.f);
	CHECK(s.e->ts[0].points[0].active);
}

TEST_CASE("undo restores the lane after GEN") {
	Sim s;
	s.P().steps[L_NOTE][0].value = 3.f;
	s.commit();
	s.e->selLane = L_NOTE;
	s.in.pGen = true;
	s.in.character = 1.f;
	s.run(1);
	s.in.pUndo = true;
	s.run(1);
	CHECK(s.e->edit[0].steps[L_NOTE][0].value == 3.f);
}

TEST_CASE("freeze replays the last steps") {
	Sim s;
	auto& p = s.P();
	for (int i = 0; i < 16; i++)
		p.steps[L_GATE][i].value = 1.f;
	for (int i = 0; i < 16; i++)
		p.steps[L_NOTE][i].value = (float) (i % 8);
	s.commit();
	s.e->opt.freezeLen = 2;
	s.run(125 * 8);
	size_t before = s.gateOnSample[0].size();
	s.in.freezeHeld = true;
	s.run(125 * 8);
	s.in.freezeHeld = false;
	size_t during = s.gateOnSample[0].size() - before;
	CHECK(during >= 7);
	// pitches during freeze alternate between two values
	std::vector<float> ps(s.pitchAtGate[0].begin() + (long) before, s.pitchAtGate[0].end());
	REQUIRE(ps.size() >= 4);
	CHECK(ps[0] == doctest::Approx(ps[2]));
	CHECK(ps[1] == doctest::Approx(ps[3]));
}

TEST_CASE("event-driven lane advances only on track 1 gates") {
	Sim s;
	auto& p0 = s.e->edit[0];
	p0.steps[L_GATE][0].value = 1.f;
	p0.steps[L_GATE][8].value = 1.f;
	s.e->markEdited(0);
	auto& p1 = s.e->edit[1];
	for (int i = 0; i < 16; i++)
		p1.steps[L_GATE][i].value = 1.f;
	p1.lanes[L_NOTE].clock = ClockSrc::Gate1;
	p1.lanes[L_NOTE].length = 4;
	for (int i = 0; i < 4; i++)
		p1.steps[L_NOTE][i].value = (float) i;
	s.e->markEdited(1);
	s.run(125 * 16);
	// track 2's note lane moves only on track 1's two gates: note 0 from step 0, note 1 from step 8
	REQUIRE(s.pitchAtGate[1].size() >= 16);
	ScaleDef sc = getScale(0, ScaleSet());
	for (size_t i = 0; i < 16; i++)
		CHECK(s.pitchAtGate[1][i] == doctest::Approx(degreeToSemis(sc, i < 8 ? 0 : 1) / 12.f));
}

TEST_CASE("onset detector finds clicks without doubles") {
	Sim s;
	s.in.expander = true;
	int found = 0;
	auto& p1 = s.e->edit[0];
	p1.lanes[L_MODA].clock = ClockSrc::Onset;
	s.e->markEdited(0);
	for (int i = 0; i < 2000; i++) {
		s.in.expander = true;
		s.in.audio = (i % 250 < 3) ? 8.f : 0.f; // click every 250 ms
		int before = (int) s.e->tr[0].lanes[L_MODA].events;
		s.run(1);
		found += (int) s.e->tr[0].lanes[L_MODA].events - before;
	}
	CHECK(found == 8);
}
