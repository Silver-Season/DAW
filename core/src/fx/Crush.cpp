// Bitcrusher ("crush"): sample-and-hold downsampling plus word-length reduction (docs/spec/06-effects-mixing.md, task 6.2).
//
// No oversampling and no filtering on purpose: the aliasing of the hold and the stair-steps of the quantiser are the effect.
#include "daw/fx/BasicEffect.h"

#include <cstdint>

namespace daw::fx
{
namespace
{
class CrushEffect final : public BasicEffect
{
public:
    enum Param { pBits, pRate, pDither };

    CrushEffect()
        : BasicEffect ({ { "bits", "Bits", "bit", 1.0f, 24.0f, 8.0f, false, {} },
                         { "rate", "Rate", "Hz", 100.0f, 48000.0f, 11025.0f, true, {} },
                         { "dither", "Dither", "", 0.0f, 1.0f, 0.0f, false, { "Off", "On" } } })
    {
        refresh();
    }

    std::string_view type() const override { return "crush"; }

protected:
    void prepareWet (double, int) override { refresh(); }

    void resetWet() override
    {
        // One step short of a full cycle: the first sample is taken at once, the next one a whole period later.
        phase = 1.0 - increment;
        held[0] = held[1] = 0.0f;
        rng[0] = 0x9E3779B9u;
        rng[1] = 0x85EBCA6Bu;
    }

    void paramChanged (int, float) override { refresh(); }

    void processWet (float* const* io, int numChannels, int numSamples, const float* const*) override
    {
        const bool dither = value (pDither) >= 0.5f;
        for (int i = 0; i < numSamples; ++i)
        {
            phase += increment;
            // The small allowance makes a rate that divides the sample rate hold for exactly the same number of samples
            // every time, instead of one more now and then when the sum of the increments lands just below 1.
            if (phase >= 1.0 - 1.0e-9)
            {
                phase -= 1.0;
                if (phase >= 1.0) // a rate above the sample rate: every sample is taken
                    phase = 0.0;
                for (int c = 0; c < numChannels; ++c)
                    held[c] = quantise (io[c][i], dither, rng[c]);
            }
            for (int c = 0; c < numChannels; ++c)
                io[c][i] = held[c];
        }
    }

private:
    // Rounds to 2^bits levels: a two's-complement grid, -2^(bits-1) ... 2^(bits-1) - 1 steps of 2^(1-bits), so that zero is a
    // level (silence stays silent) and exactly 2^bits values come out. One bit would then be {-1, 0}, a half-wave; it gets the
    // symmetric three levels -1, 0, 1 instead. The dither is triangular, +-1 step, which turns the quantiser's distortion into
    // a steady hiss.
    float quantise (float x, bool dither, uint32_t& state) const
    {
        double v = std::clamp ((double) x, -1.0, 1.0) * scale;
        if (dither)
            v += uniform (state) - uniform (state);
        const double q = std::clamp (std::floor (v + 0.5), lowest, highest);
        return (float) (q / scale);
    }

    static double uniform (uint32_t& state)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (double) (state >> 8) * (1.0 / 16777216.0);
    }

    void refresh()
    {
        const int bits = std::clamp ((int) std::lround (value (pBits)), 1, 24);
        scale = std::ldexp (1.0, bits - 1);
        lowest = -scale;
        highest = bits == 1 ? 1.0 : scale - 1.0;
        increment = std::min (1.0, (double) value (pRate) / rate);
    }

    double scale = 128.0, lowest = -128.0, highest = 127.0;
    double increment = 11025.0 / 48000.0, phase = 1.0;
    float held[2] { 0.0f, 0.0f };
    uint32_t rng[2] { 0x9E3779B9u, 0x85EBCA6Bu };
};
} // namespace

std::unique_ptr<Effect> makeCrush() { return std::make_unique<CrushEffect>(); }
} // namespace daw::fx
