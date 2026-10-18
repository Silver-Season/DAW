// Pieces shared by the compressor, the gate and the limiter: time constants, the lookahead delay, the high-pass filter of
// the key signal and the input meter. Private to core/src/fx.
#pragma once

#include "daw/fx/Effect.h"
#include "daw/fx/Helpers.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

namespace daw::fx::dyn
{

// One-pole coefficient that covers 63 % of a step in `ms` (the usual meaning of attack and release times).
inline float timeCoef (double ms, double sampleRate)
{
    return (float) (1.0 - std::exp (-1.0 / std::max (1.0e-3, ms * 0.001 * sampleRate)));
}

// The coefficient of `n` steps of a one-pole with per-sample coefficient `c`, for state that is advanced once per block.
inline float blockCoef (float c, int n)
{
    return 1.0f - std::pow (1.0f - c, (float) n);
}

// Moves a smoother `n` steps at once.
inline void advance (Smoother& s, int n)
{
    s.current += (s.target - s.current) * blockCoef (s.coef, n);
    if (! s.smoothing())
        s.snap();
}

inline const std::vector<std::string>& offOn()
{
    static const std::vector<std::string> choices { "Off", "On" };
    return choices;
}

// The lookahead: the audio waits while the detector already sees what comes. The delay may change between blocks; the
// jump that causes is the same one the host's delay compensation makes when the reported latency changes.
struct DelayLine
{
    void prepare (int maxDelay)
    {
        for (auto& b : lines)
            b.assign ((size_t) std::max (1, maxDelay + 1), 0.0f);
        pos = 0;
    }

    void reset()
    {
        for (auto& b : lines)
            std::fill (b.begin(), b.end(), 0.0f);
        pos = 0;
    }

    int maxDelay() const { return (int) lines[0].size() - 1; }

    // Writes `x` into channel `c` and returns the sample written `delay` steps ago. Call it for every channel, then advance().
    float push (int c, float x, int delay)
    {
        auto& b = lines[(size_t) c];
        const int size = (int) b.size();
        b[(size_t) pos] = x;
        int readPos = pos - delay;
        if (readPos < 0)
            readPos += size;
        return b[(size_t) readPos];
    }

    void advance()
    {
        if (++pos >= (int) lines[0].size())
            pos = 0;
    }

    std::array<std::vector<float>, 2> lines;
    int pos = 0;
};

// Second-order high-pass on the key signal, so that the bass does not pump the whole mix. 20 Hz (the bottom of the range)
// means off. The cutoff glides once per block: the key is not heard (except when listening), so block steps are fine.
struct KeyFilter
{
    void prepare (double sampleRate)
    {
        rate = sampleRate;
        coef20 = timeCoef (20.0, sampleRate);
        snap();
    }

    void setFreq (float hz) { target = hz; }

    void snap()
    {
        current = target;
        design();
        for (auto& f : filters)
            f.reset();
    }

    void advance (int n)
    {
        if (current != target)
        {
            const float lc = std::log (current), lt = std::log (target);
            const float next = std::exp (lc + (lt - lc) * blockCoef (coef20, n));
            current = std::abs (next - target) < 0.01f ? target : next;
            design();
        }
    }

    bool active() const { return current > kOffBelow; }

    float process (int c, float x) { return active() ? filters[(size_t) c].tick (x) : x; }

    void design()
    {
        if (! active())
        {
            for (auto& f : filters)
                f.reset(); // a fresh start the next time it turns on
            return;
        }
        for (auto& f : filters)
        {
            const double z1 = f.z1, z2 = f.z2;
            f.set (Biquad::Type::highPass, current, 0.7071, 0.0, rate);
            f.z1 = z1;
            f.z2 = z2;
        }
    }

    static constexpr float kOffBelow = 20.05f;
    double rate = 48000.0;
    float coef20 = 1.0f, current = 20.0f, target = 20.0f;
    std::array<Biquad, 2> filters;
};

// A peak level for the device's meter: rises at once, falls about 29 dB per second. Written once per block.
struct LevelMeter
{
    void prepare (double sampleRate) { fall = 1.0f - timeCoef (300.0, sampleRate); }

    void reset()
    {
        level = 0.0f;
        db.store (-120.0f, std::memory_order_relaxed);
    }

    void block (float blockPeak, int n)
    {
        level = std::max (blockPeak, level * std::pow (fall, (float) n));
        db.store (gainToDb (level), std::memory_order_relaxed);
    }

    float fall = 0.999f, level = 0.0f;
    std::atomic<float> db { -120.0f };
};

} // namespace daw::fx::dyn
