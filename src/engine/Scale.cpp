#include "Scale.hpp"
#include <cstdlib>

namespace wa {

namespace {

struct Preset {
	const char* name;
	int n;
	int s[12];
};

const Preset PRESETS[NUM_PRESET_SCALES] = {
	{"Major", 7, {0, 2, 4, 5, 7, 9, 11}},
	{"Minor", 7, {0, 2, 3, 5, 7, 8, 10}},
	{"Dorian", 7, {0, 2, 3, 5, 7, 9, 10}},
	{"Phrygian", 7, {0, 1, 3, 5, 7, 8, 10}},
	{"Lydian", 7, {0, 2, 4, 6, 7, 9, 11}},
	{"Mixolyd", 7, {0, 2, 4, 5, 7, 9, 10}},
	{"Locrian", 7, {0, 1, 3, 5, 6, 8, 10}},
	{"HarmMin", 7, {0, 2, 3, 5, 7, 8, 11}},
	{"MelMin", 7, {0, 2, 3, 5, 7, 9, 11}},
	{"MajPent", 5, {0, 2, 4, 7, 9}},
	{"MinPent", 5, {0, 3, 5, 7, 10}},
	{"Blues", 6, {0, 3, 5, 6, 7, 10}},
	{"WholeT", 6, {0, 2, 4, 6, 8, 10}},
	{"Chromat", 12, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
	{"Hirajo", 5, {0, 2, 3, 7, 8}},
	{"In-sen", 5, {0, 1, 5, 7, 10}},
	{"Pelog", 5, {0, 1, 3, 7, 8}},
	{"HungMin", 7, {0, 2, 3, 6, 7, 8, 11}},
	{"PhrDom", 7, {0, 1, 4, 5, 7, 8, 10}},
	{"DblHarm", 7, {0, 1, 4, 5, 7, 8, 11}},
	{"DimHW", 8, {0, 1, 3, 4, 6, 7, 9, 10}},
	{"DimWH", 8, {0, 2, 3, 5, 6, 8, 9, 11}},
	{"LydDom", 7, {0, 2, 4, 6, 7, 9, 10}},
	{"Altered", 7, {0, 1, 3, 4, 6, 8, 10}},
};

const char* USER_NAMES[NUM_USER_SCALES] = {"User 1", "User 2", "User 3", "User 4"};

const int CHORD_SIZES[6] = {1, 2, 3, 4, 3, 3};
const int CHORDS[6][4] = {
	{0, 0, 0, 0},
	{0, 4, 0, 0},
	{0, 2, 4, 0},
	{0, 2, 4, 6},
	{0, 3, 4, 0},
	{0, 1, 2, 0},
};
const char* CHORD_NAMES[NUM_CHORDS] = {"-", "5th", "Triad", "7th", "Sus", "Clust", "User1", "User2", "User3"};

const char* NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

int floorDiv(int a, int b) {
	int q = a / b;
	if ((a % b != 0) && ((a < 0) != (b < 0)))
		q--;
	return q;
}

int floorMod(int a, int b) {
	return a - floorDiv(a, b) * b;
}

} // namespace

ScaleSet::ScaleSet() {
	userMask[0] = 0xAB5; // major
	userMask[1] = 0x5AD; // minor
	userMask[2] = 0x295; // major pentatonic
	userMask[3] = 0x4A9; // minor pentatonic
	const int8_t def[NUM_USER_CHORDS][MAX_CHORD] = {{0, 2, 4, 7}, {0, 4, 7, 0}, {0, 1, 4, 0}};
	const uint8_t sizes[NUM_USER_CHORDS] = {4, 3, 3};
	for (int i = 0; i < NUM_USER_CHORDS; i++) {
		for (int j = 0; j < MAX_CHORD; j++)
			userChord[i][j] = def[i][j];
		userChordSize[i] = sizes[i];
	}
}

const char* scaleName(int idx) {
	if (idx < 0)
		idx = 0;
	if (idx < NUM_PRESET_SCALES)
		return PRESETS[idx].name;
	if (idx < NUM_SCALES)
		return USER_NAMES[idx - NUM_PRESET_SCALES];
	return "?";
}

ScaleDef getScale(int idx, const ScaleSet& us) {
	ScaleDef d;
	if (idx >= 0 && idx < NUM_PRESET_SCALES) {
		d.n = PRESETS[idx].n;
		for (int i = 0; i < d.n; i++)
			d.semis[i] = PRESETS[idx].s[i];
		return d;
	}
	if (idx >= NUM_PRESET_SCALES && idx < NUM_SCALES) {
		uint16_t mask = us.userMask[idx - NUM_PRESET_SCALES] | 1; // root always present
		d.n = 0;
		for (int i = 0; i < 12; i++)
			if (mask & (1 << i))
				d.semis[d.n++] = i;
		return d;
	}
	return d;
}

int degreeToSemis(const ScaleDef& s, int degree) {
	int n = s.n > 0 ? s.n : 1;
	return 12 * floorDiv(degree, n) + s.semis[floorMod(degree, n)];
}

int quantizeSemis(const ScaleDef& s, int root, int semis) {
	int best = semis;
	int bestDist = 1000;
	// Search candidates in the octaves around `semis`; ties go down because we test lower first.
	for (int oct = floorDiv(semis - root, 12) - 1; oct <= floorDiv(semis - root, 12) + 1; oct++) {
		for (int i = 0; i < s.n; i++) {
			int cand = root + oct * 12 + s.semis[i];
			int dist = std::abs(cand - semis);
			if (dist < bestDist || (dist == bestDist && cand < best)) {
				bestDist = dist;
				best = cand;
			}
		}
	}
	return best;
}

int chordSize(int chord, const ScaleSet& us) {
	if (chord >= 0 && chord < 6)
		return CHORD_SIZES[chord];
	if (chord >= 6 && chord < NUM_CHORDS) {
		int s = us.userChordSize[chord - 6];
		return s < 1 ? 1 : (s > MAX_CHORD ? MAX_CHORD : s);
	}
	return 1;
}

int chordDegree(int chord, int c, const ScaleSet& us) {
	if (c < 0 || c >= MAX_CHORD)
		return 0;
	if (chord >= 0 && chord < 6)
		return CHORDS[chord][c];
	if (chord >= 6 && chord < NUM_CHORDS)
		return us.userChord[chord - 6][c];
	return 0;
}

const char* chordName(int chord) {
	if (chord < 0 || chord >= NUM_CHORDS)
		return "?";
	return CHORD_NAMES[chord];
}

const char* noteName(int semis) {
	return NOTE_NAMES[floorMod(semis, 12)];
}

} // namespace wa
