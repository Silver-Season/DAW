#pragma once

#include <cstdint>

namespace daw::gen
{

// A small PCG32 generator. The standard distributions (std::uniform_real_distribution and friends) give different
// numbers on different standard libraries, so the generators draw everything from here: a seed then produces the
// same pattern or patch on macOS and Linux.
class Rng
{
public:
    // `salt` separates the streams of different generators, so that chords and drums made with the same seed do
    // not use the same numbers.
    Rng (uint32_t seed, uint32_t salt) noexcept
    {
        uint64_t z = ((uint64_t) seed << 32) ^ salt;
        // splitmix64 spreads close seeds (1, 2, 3...) over the whole state
        z += 0x9e3779b97f4a7c15ull;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        z ^= z >> 31;
        state = z;
        increment = ((uint64_t) salt << 1) | 1u;
        next();
    }

    uint32_t next() noexcept
    {
        const uint64_t old = state;
        state = old * 6364136223846793005ull + increment;
        const auto xorShifted = (uint32_t) (((old >> 18) ^ old) >> 27);
        const auto rot = (uint32_t) (old >> 59);
        return (xorShifted >> rot) | (xorShifted << ((32 - rot) & 31));
    }

    // [0, 1)
    double uniform() noexcept { return (double) next() * (1.0 / 4294967296.0); }
    double range (double lo, double hi) noexcept { return lo + (hi - lo) * uniform(); }
    // [lo, hi], both inclusive
    int integer (int lo, int hi) noexcept
    {
        if (hi <= lo)
            return lo;
        return lo + (int) (next() % (uint32_t) (hi - lo + 1));
    }
    bool chance (double p) noexcept { return uniform() < p; }

private:
    uint64_t state = 0, increment = 1;
};

} // namespace daw::gen
