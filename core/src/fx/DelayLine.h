// Shared building blocks of the delay-based effects (reverb, delay). Private to core/src/fx.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace daw::fx
{

// Denormals appear when a feedback loop decays into silence and make every later operation on them slow. Anything this
// small is inaudible, so it is written back as zero.
inline float flushTiny (float x) { return std::abs (x) < 1.0e-15f ? 0.0f : x; }

// The smoother of Helpers.h never quite reaches its target; freeze needs exact values (a loop gain of exactly 1), so a
// smoother that is close enough jumps onto the target.
template <typename SmootherType>
inline float stepExact (SmootherType& s)
{
    if (s.smoothing())
        return s.next();
    s.snap();
    return s.current;
}

// A circular buffer whose size is a power of two, so wrapping is a mask. Read before push: tap (1) is the sample pushed
// last, so a read of delay d returns the input of d samples ago.
class DelayLine
{
public:
    // The only allocation. Reads up to `maxDelay` samples (and the interpolation taps around them) stay inside.
    void allocate (int maxDelay)
    {
        int n = 1;
        while (n < maxDelay + 4)
            n <<= 1;
        buffer.assign ((size_t) n, 0.0f);
        mask = n - 1;
        pos = 0;
        limit = (double) std::max (2, maxDelay);
    }

    void clear()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        pos = 0;
    }

    void push (float x)
    {
        buffer[(size_t) pos] = x;
        pos = (pos + 1) & mask;
    }

    float tap (int d) const { return buffer[(size_t) ((pos - d) & mask)]; }

    // Fractional delay through a 3rd-order Lagrange interpolator over the four samples around it. It needs one newer
    // neighbour, so delays below 2 samples are read at 2. At a whole number of samples the read is exact.
    float read (double d) const
    {
        d = std::clamp (d, 2.0, limit);
        const int i = (int) d;
        const float f = (float) (d - (double) i);
        const float fm1 = f - 1.0f, fm2 = f - 2.0f, fp1 = f + 1.0f;
        const float h0 = -f * fm1 * fm2 * (1.0f / 6.0f);
        const float h1 = fp1 * fm1 * fm2 * 0.5f;
        const float h2 = -fp1 * f * fm2 * 0.5f;
        const float h3 = fp1 * f * fm1 * (1.0f / 6.0f);
        return h0 * tap (i - 1) + h1 * tap (i) + h2 * tap (i + 1) + h3 * tap (i + 2);
    }

    // Fractional delay by linear interpolation: cheaper, for reads that stay outside a feedback loop (its slight
    // high-frequency loss would add up on every pass inside one).
    float readLinear (double d) const
    {
        d = std::clamp (d, 1.0, limit);
        const int i = (int) d;
        const float f = (float) (d - (double) i);
        const float a = tap (i);
        return a + (tap (i + 1) - a) * f;
    }

private:
    std::vector<float> buffer;
    int mask = 0, pos = 0;
    double limit = 2.0;
};

} // namespace daw::fx
