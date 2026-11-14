#pragma once
// Data model for LATTICE (spec §3).
#include <cstdint>

namespace wa {

constexpr int NT = 3;      // tracks
constexpr int NL = 9;      // lanes per track
constexpr int NS = 32;     // stored steps per lane
constexpr int NSLOT = 16;  // pattern slots per track
constexpr int NSONG = 16;
constexpr int NROW = 64;
constexpr int NPOINT = 8;  // morph points per track
constexpr int NUNDO = 8;
constexpr int NMATRIX = 4;
constexpr int NSRC = 8;    // expander SRC inputs
constexpr int NRATIOS = 15;

enum Lane { L_GATE, L_NOTE, L_OCT, L_CHORD, L_VEL, L_LEN, L_RAT, L_MODA, L_MODB };

enum class Cond : uint8_t { Always, AofB, First, NotFirst, Fill, NotFill, Pre, NotPre, COUNT };
enum class Role : uint8_t { Anchor, Pulse, Passing, Tension, COUNT };
enum class Pin : uint8_t { None, Fired, NotFired };
enum class Dir : uint8_t { Forward, Reverse, Pendulum, Random, Drunk, Skip, COUNT };
enum class ClockSrc : uint8_t { Track, Gate1, Gate2, Gate3, ExtTrig, Onset, Threshold, COUNT };

extern const float RATIOS[NRATIOS];
extern const char* RATIO_NAMES[NRATIOS];
extern const char* LANE_NAMES[NL];
extern const char* LANE_SHORT[NL];
extern const char* DIR_NAMES[(int) Dir::COUNT];
extern const char* CLOCKSRC_NAMES[(int) ClockSrc::COUNT];
extern const char* COND_NAMES[(int) Cond::COUNT];
extern const char* ROLE_NAMES[(int) Role::COUNT];

struct Step {
	float value = 0.f;     // meaning depends on lane (§3.2)
	float alt = 0.f;       // alternate value used when the condition or probability fails
	bool hasAlt = false;
	uint8_t prob = 100;    // 0..100 %
	Cond cond = Cond::Always;
	uint8_t condA = 1, condB = 1;
	bool slide = false;    // Note, ModA, ModB
	bool accent = false;   // Gate
	int8_t nudge = 0;      // Gate: -49..+49 % of a gate-lane step
	Role role = Role::Pulse; // Gate
	Pin pin = Pin::None;
};

struct LaneSettings {
	uint8_t start = 0;     // 0..31
	uint8_t length = 16;   // 1..32
	uint8_t ratio = 4;     // index into RATIOS, 4 = x1
	Dir dir = Dir::Forward;
	uint8_t skip = 3;      // 2..7
	ClockSrc clock = ClockSrc::Track;
	uint8_t extDiv = 1;    // 1..16
	bool glide = false;    // ModA/ModB: glide every step
};

enum class Contour : uint8_t { Free, Rise, Fall, Arch, Valley, Zigzag, COUNT };
enum class EndOn : uint8_t { Any, Root, ChordTone, COUNT };
extern const char* CONTOUR_NAMES[(int) Contour::COUNT];
extern const char* ENDON_NAMES[(int) EndOn::COUNT];

struct RulesConfig {
	uint8_t intervals = 0x06; // bit 0 unison, 1 2nd, 2 3rd, 3 4th, 4 5th, 5 6th, 6 7th, 7 octave
	Contour contour = Contour::Free;
	uint8_t lo = 0, hi = 10;
	uint8_t maxRepeat = 2;
	uint8_t densMin = 25, densMax = 60;
	uint32_t mustHit = 0;     // bit per stored step
	bool leapRecovery = true;
	EndOn endOn = EndOn::Any;
};

enum class AnswerMode : uint8_t { Off, Echo, Mirror, Retro, Resolve, COUNT };
extern const char* ANSWER_NAMES[(int) AnswerMode::COUNT];

struct AnswerConfig {
	AnswerMode mode = AnswerMode::Off;
	uint8_t source = 0;      // track index lower than this track
	uint8_t displace = 0;    // 0..15
	int8_t transpose = 0;    // -7..+7
	uint8_t fill = 100;      // 0..100 %
};

enum class MatrixAction : uint8_t { Offset, Transpose, Direction, Mute, Length, Clock, COUNT };
extern const char* MATRIX_ACTION_NAMES[(int) MatrixAction::COUNT];

constexpr int MATRIX_SRC_NONE = 0;
// source codes: 0 none, 1..27 = track*9 + lane + 1, 28..35 = expander SRC 1..8
constexpr int MATRIX_SRC_COUNT = 1 + NT * NL + NSRC;

struct MatrixSlot {
	uint8_t source = MATRIX_SRC_NONE;
	uint8_t target = L_NOTE;
	MatrixAction action = MatrixAction::Transpose;
	float amount = 0.5f;     // -1..+1
};

struct Pattern {
	Step steps[NL][NS];
	LaneSettings lanes[NL];
	uint8_t path[NL][NS];   // pinned path for Random/Drunk lanes
	uint8_t pathLen[NL];    // 0 = no pinned path
	uint8_t root = 0;       // 0..11
	uint8_t scale = 0;      // 0..27
	bool followT1 = false;
	int8_t transpose = 0;   // whole track, scale degrees -14..+14
	uint8_t swing = 50;     // 50..75
	uint16_t seed = 1;      // 0..999
	RulesConfig rules;
	AnswerConfig answer;
	MatrixSlot matrix[NMATRIX];

	Pattern() { clear(); }
	void clear();
	/** Default value for a lane's steps. */
	static float defaultValue(int lane);
};

struct MorphPoint {
	bool active = false;
	int8_t slot = -1;
	float x = 0.f, y = 0.f;
};

struct SongRow {
	int8_t pattern[NT] = {-1, -1, -1}; // -1 = keep current
	uint8_t repeats = 1;               // 1..64, 0 = until NEXT
};

struct Song {
	SongRow rows[NROW];
	uint8_t count = 0;
};

enum class SyncMode : uint8_t { Free, Gate, Bars, COUNT };
enum class ChangeQuant : uint8_t { Off, Beat, Bar, GateEnd, COUNT };
extern const char* SYNC_NAMES[(int) SyncMode::COUNT];
extern const char* QUANT_NAMES[(int) ChangeQuant::COUNT];

/** Per-track settings saved with the patch but not part of a pattern. */
struct TrackSettings {
	int8_t slot = 0;          // current slot (-1 none)
	SyncMode sync = SyncMode::Free;
	uint8_t syncBars = 4;     // 1..16
	ChangeQuant quant = ChangeQuant::Bar;
	float glide = 0.1f;       // seconds 0..2
	uint8_t poly = 0;         // 0 = auto, 1..4 fixed
	bool pitchFollows = false;
	bool mute = false;
	bool morphOn = false;
	MorphPoint points[NPOINT];
	bool locked[NSLOT] = {};
	bool used[NSLOT] = {};
	float arc = 1.f, drift = 0.f, density = 0.f, spread = 0.f, mx = 0.5f, my = 0.5f;
};

// Normalised lane value (0..1) for matrix and morph (§3.2)
float normValue(int lane, float v);
float laneMin(int lane);
float laneMax(int lane);
inline int ratchetCount(float v) {
	int c = (int) v % 10;
	return c < 1 ? 1 : (c > 8 ? 8 : c);
}
inline int ratchetRamp(float v) {
	int r = (int) v / 10;
	return r < 0 ? 0 : (r > 2 ? 2 : r);
}
inline float ratchetPack(int count, int ramp) { return (float) (count + 10 * ramp); }

} // namespace wa
