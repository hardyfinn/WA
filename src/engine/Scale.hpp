#pragma once
// Scales, chords and quantising (spec §5).
#include <cstdint>

namespace wa {

constexpr int NUM_PRESET_SCALES = 24;
constexpr int NUM_USER_SCALES = 4;
constexpr int NUM_SCALES = NUM_PRESET_SCALES + NUM_USER_SCALES;
constexpr int NUM_CHORDS = 9;      // 0 single, 1-5 presets, 6-8 user
constexpr int NUM_USER_CHORDS = 3;
constexpr int MAX_CHORD = 4;

/** Module-wide user scales and chord shapes, edited on the SCALE page. */
struct ScaleSet {
	uint16_t userMask[NUM_USER_SCALES];           // bit i = semitone i is in the scale
	int8_t userChord[NUM_USER_CHORDS][MAX_CHORD]; // scale degrees
	uint8_t userChordSize[NUM_USER_CHORDS];       // 1..4

	ScaleSet();
};

struct ScaleDef {
	int n = 7;
	int semis[12] = {0, 2, 4, 5, 7, 9, 11};
};

const char* scaleName(int idx);
ScaleDef getScale(int idx, const ScaleSet& us);

/** Semitones above the root for a scale degree (negative degrees allowed). */
int degreeToSemis(const ScaleDef& s, int degree);

/** Nearest semitone (relative to C) that is in the scale built on `root`. Ties go down. */
int quantizeSemis(const ScaleDef& s, int root, int semis);

int chordSize(int chord, const ScaleSet& us);
int chordDegree(int chord, int c, const ScaleSet& us);
const char* chordName(int chord);

/** Note name for a semitone value relative to C, e.g. "D#". */
const char* noteName(int semis);

} // namespace wa
