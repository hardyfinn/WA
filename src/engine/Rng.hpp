#pragma once
// Seeded, repeatable randomness for LATTICE (spec §0.4).
// xoshiro128** seeded through splitmix64. Never use rack::random for musical decisions.
#include <cstdint>

namespace wa {

inline uint64_t splitmix64(uint64_t& x) {
	uint64_t z = (x += 0x9E3779B97F4A7C15ull);
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
	return z ^ (z >> 31);
}

struct Rng {
	uint32_t s[4];

	explicit Rng(uint64_t seed = 1) { reseed(seed); }

	void reseed(uint64_t seed) {
		uint64_t x = seed;
		for (int i = 0; i < 4; i += 2) {
			uint64_t v = splitmix64(x);
			s[i] = (uint32_t) v;
			s[i + 1] = (uint32_t) (v >> 32);
		}
		if (!(s[0] | s[1] | s[2] | s[3]))
			s[0] = 1;
	}

	static uint32_t rotl(uint32_t x, int k) { return (x << k) | (x >> (32 - k)); }

	uint32_t next() {
		uint32_t r = rotl(s[1] * 5, 7) * 9;
		uint32_t t = s[1] << 9;
		s[2] ^= s[0];
		s[3] ^= s[1];
		s[1] ^= s[2];
		s[0] ^= s[3];
		s[2] ^= t;
		s[3] = rotl(s[3], 11);
		return r;
	}

	/** Uniform float in [0, 1). */
	float uniform() { return (next() >> 8) * (1.0f / 16777216.0f); }

	/** Uniform integer in [0, n). */
	int uniformInt(int n) {
		if (n <= 1)
			return 0;
		return (int) (((uint64_t) next() * (uint64_t) n) >> 32);
	}

	/** Independent stream for (seed, a, b, c), e.g. (pattern seed, track, lane, press count). */
	static Rng fork(uint64_t seed, uint64_t a, uint64_t b = 0, uint64_t c = 0) {
		uint64_t x = seed * 0x100000001B3ull ^ (a + 1) * 0x9E3779B97F4A7C15ull ^ (b + 3) * 0xC2B2AE3D27D4EB4Full ^ (c + 5) * 0x165667B19E3779F9ull;
		return Rng(splitmix64(x));
	}
};

/** Stable hash of up to four integers to [0, 1). Used where a value must not change between calls. */
inline float hash01(uint64_t a, uint64_t b, uint64_t c, uint64_t d = 0) {
	uint64_t x = (a + 11) * 0x9E3779B97F4A7C15ull ^ (b + 1) * 0xC2B2AE3D27D4EB4Full ^ (c + 7) * 0x165667B19E3779F9ull ^ (d + 13) * 0x27D4EB2F165667C5ull;
	return (splitmix64(x) >> 40) * (1.0f / 16777216.0f);
}

} // namespace wa
