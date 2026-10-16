#pragma once

#include <cmath>
#include <cstdint>
#include <numbers>

namespace daw::dsp
{

enum class LfoShape
{
    Sine,
    Triangle,
    SawUp,
    SawDown,
    Square,
    SampleAndHold,
    count
};

// Bipolar LFO in [-1, 1]. Advanced at control rate by the voice.
class LFO
{
public:
    void reset (double startPhase = 0.0) noexcept
    {
        phase = startPhase;
        held = nextRandom();
    }

    void setSeed (uint32_t s) noexcept { seed = s ? s : 1u; }

    float advance (LfoShape shape, double rateHz, int numSamples, double sampleRate) noexcept
    {
        phase += rateHz * numSamples / sampleRate;
        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            held = nextRandom();
        }
        return value (shape);
    }

    float value (LfoShape shape) const noexcept
    {
        const float p = (float) phase;
        switch (shape)
        {
            case LfoShape::Sine: return std::sin (2.0f * std::numbers::pi_v<float> * p);
            case LfoShape::Triangle: return 1.0f - 4.0f * std::abs (p - 0.5f);
            case LfoShape::SawUp: return 2.0f * p - 1.0f;
            case LfoShape::SawDown: return 1.0f - 2.0f * p;
            case LfoShape::Square: return p < 0.5f ? 1.0f : -1.0f;
            case LfoShape::SampleAndHold: return held;
            case LfoShape::count:
            default: return 0.0f;
        }
    }

private:
    float nextRandom() noexcept
    {
        seed = seed * 1664525u + 1013904223u;
        return (float) (seed >> 8) / (float) (1u << 24) * 2.0f - 1.0f;
    }

    double phase = 0.0;
    float held = 0.0f;
    uint32_t seed = 22222u;
};

} // namespace daw::dsp
