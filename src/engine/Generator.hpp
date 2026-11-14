#pragma once
// Generation, rules, call-and-answer, morph and gesture fitting (spec §7.1, §7.3, §7.4, §8, §12).
// All functions are pure: they only touch the patterns passed in, so they can be unit tested.
#include "Types.hpp"
#include "Scale.hpp"
#include "Rng.hpp"

namespace wa {

/** Stored step index for loop position i of a lane. */
inline int loopIndex(const LaneSettings& ls, int i) {
	return (ls.start + i) % NS;
}

/** Character GEN on one lane (§7.1.1). Pinned steps are never changed. */
void genLane(Pattern& p, int lane, float character, float amount, Rng& rng);

/** Shift+GEN: all lanes. With character > 0.5, non-gate lane lengths are randomised. */
void genTrack(Pattern& p, float character, float amount, Rng& rng);

/** Re-roll a single step with the character rules (used by drift). */
void genSingleStep(Pattern& p, int lane, int idx, float character, Rng& rng);

/** Rule-based GEN (§7.3). Returns false only if nothing could be generated.
 *  `relaxed` receives a short message naming the rules relaxed ("" if none), at most 31 chars. */
bool rulesGen(Pattern& p, const ScaleDef& sc, Rng& rng, char* relaxed);

/** Check a generated pattern against its rules (used by tests and to report). */
bool rulesSatisfied(const Pattern& p, const ScaleDef& sc);

/** Derive the answer track's Gate, Note and Octave lanes from `src` (§7.4). */
void deriveAnswer(Pattern& dst, const Pattern& src, int scaleN, uint64_t seed);

/** Morph weights by inverse distance squared (§8). Writes NPOINT weights; inactive points get 0. */
void morphWeights(const MorphPoint pts[NPOINT], float x, float y, float w[NPOINT]);

/** Render a morphed pattern from up to NPOINT patterns and weights. `pats[j]` may be null when w[j] == 0. */
void morphRender(Pattern& out, const Pattern* const pats[NPOINT], const float w[NPOINT], uint64_t seed);

/** Fit 0..1 gesture means onto a lane's loop (§12). */
void fitGesture(Pattern& p, int lane, const float* means, int n);

} // namespace wa
