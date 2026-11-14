#include "Types.hpp"

namespace wa {

const float RATIOS[NRATIOS] = {4.f, 3.f, 2.f, 1.5f, 1.f, 2.f / 3.f, 1.f / 2.f, 1.f / 3.f, 1.f / 4.f, 1.f / 5.f, 1.f / 6.f, 1.f / 7.f, 1.f / 8.f, 1.f / 12.f, 1.f / 16.f};
const char* RATIO_NAMES[NRATIOS] = {"x4", "x3", "x2", "x3/2", "x1", "/3:2", "/2", "/3", "/4", "/5", "/6", "/7", "/8", "/12", "/16"};
const char* LANE_NAMES[NL] = {"Gate", "Note", "Octave", "Chord", "Velocity", "Length", "Ratchet", "Mod A", "Mod B"};
const char* LANE_SHORT[NL] = {"G", "N", "O", "C", "V", "L", "R", "A", "B"};
const char* DIR_NAMES[(int) Dir::COUNT] = {"Fwd", "Rev", "Pend", "Rand", "Drunk", "Skip"};
const char* CLOCKSRC_NAMES[(int) ClockSrc::COUNT] = {"TRK", "G1", "G2", "G3", "EXT", "ONS", "THR"};
const char* COND_NAMES[(int) Cond::COUNT] = {"Always", "A:B", "First", "!First", "Fill", "!Fill", "Pre", "!Pre"};
const char* ROLE_NAMES[(int) Role::COUNT] = {"Anchor", "Pulse", "Passing", "Tension"};
const char* CONTOUR_NAMES[(int) Contour::COUNT] = {"Free", "Rise", "Fall", "Arch", "Valley", "Zigzag"};
const char* ENDON_NAMES[(int) EndOn::COUNT] = {"Any", "Root", "Chord"};
const char* ANSWER_NAMES[(int) AnswerMode::COUNT] = {"Off", "Echo", "Mirror", "Retro", "Resolve"};
const char* MATRIX_ACTION_NAMES[(int) MatrixAction::COUNT] = {"Offset", "Transp", "Dir", "Mute", "Length", "Clock"};
const char* SYNC_NAMES[(int) SyncMode::COUNT] = {"Free", "Gate", "Bars"};
const char* QUANT_NAMES[(int) ChangeQuant::COUNT] = {"Off", "Beat", "Bar", "GateEnd"};

float Pattern::defaultValue(int lane) {
	switch (lane) {
		case L_VEL: return 7.5f;
		case L_LEN: return 50.f;
		case L_RAT: return 1.f;
		default: return 0.f;
	}
}

void Pattern::clear() {
	for (int l = 0; l < NL; l++) {
		lanes[l] = LaneSettings();
		pathLen[l] = 0;
		for (int s = 0; s < NS; s++) {
			steps[l][s] = Step();
			steps[l][s].value = defaultValue(l);
			steps[l][s].alt = defaultValue(l);
			path[l][s] = (uint8_t) s;
		}
	}
	root = 0;
	scale = 0;
	followT1 = false;
	transpose = 0;
	swing = 50;
	seed = 1;
	rules = RulesConfig();
	answer = AnswerConfig();
	for (int i = 0; i < NMATRIX; i++)
		matrix[i] = MatrixSlot();
}

float laneMin(int lane) {
	switch (lane) {
		case L_OCT: return -3.f;
		case L_VEL: return 0.f;
		case L_LEN: return 1.f;
		case L_RAT: return 1.f;
		case L_MODA:
		case L_MODB: return -5.f;
		default: return 0.f;
	}
}

float laneMax(int lane) {
	switch (lane) {
		case L_GATE: return 1.f;
		case L_NOTE: return 15.f;
		case L_OCT: return 3.f;
		case L_CHORD: return 8.f;
		case L_VEL: return 10.f;
		case L_LEN: return 400.f;
		case L_RAT: return 8.f;
		case L_MODA:
		case L_MODB: return 5.f;
		default: return 1.f;
	}
}

float normValue(int lane, float v) {
	float x;
	switch (lane) {
		case L_GATE: x = v > 0.5f ? 1.f : 0.f; break;
		case L_NOTE: x = v / 15.f; break;
		case L_OCT: x = (v + 3.f) / 6.f; break;
		case L_CHORD: x = v / 8.f; break;
		case L_VEL: x = v / 10.f; break;
		case L_LEN: x = (v - 1.f) / 399.f; break;
		case L_RAT: x = (ratchetCount(v) - 1) / 7.f; break;
		default: x = (v + 5.f) / 10.f; break;
	}
	return x < 0.f ? 0.f : (x > 1.f ? 1.f : x);
}

} // namespace wa
