#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace daw::dsp
{

enum class FilterType
{
    LowPass12,
    LowPass24,
    HighPass12,
    HighPass24,
    BandPass12,
    Notch,
    count
};

// Topology-preserving-transform state variable filter (Zavalishin / Simper). Stable under fast
// modulation, which is what a synth filter swept by envelopes and LFOs needs.
class SVF
{
public:
    void reset() noexcept { ic1 = ic2 = 0.0f; }

    // resonance in [0, 1]; 1 is close to self-oscillation.
    void setCoefficients (float cutoffHz, float resonance, float sampleRate) noexcept
    {
        const float fc = std::clamp (cutoffHz, 10.0f, sampleRate * 0.49f);
        g = std::tan (std::numbers::pi_v<float> * fc / sampleRate);
        k = 2.0f - 1.96f * std::clamp (resonance, 0.0f, 1.0f);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    struct Outputs
    {
        float low, band, high;
    };

    Outputs tick (float v0) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return { v2, v1, v0 - k * v1 - v2 };
    }

    float k = 2.0f;

private:
    float g = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;
};

// Mono synth filter: one or two SVF stages plus pre-drive.
class SynthFilter
{
public:
    void reset() noexcept
    {
        s1.reset();
        s2.reset();
    }

    void set (FilterType t, float cutoffHz, float resonance, float drive, float sampleRate) noexcept
    {
        type = t;
        const bool fourPole = (t == FilterType::LowPass24 || t == FilterType::HighPass24);
        // In 4-pole mode resonance lives in the second stage only, the first one stays Butterworth-like.
        s1.setCoefficients (cutoffHz, fourPole ? 0.3f : resonance, sampleRate);
        s2.setCoefficients (cutoffHz, resonance, sampleRate);
        driveGain = 1.0f + drive * 9.0f;
        makeup = 1.0f / std::sqrt (driveGain);
    }

    float process (float x) noexcept
    {
        if (driveGain > 1.001f)
            x = std::tanh (x * driveGain) * makeup * 1.4f;

        switch (type)
        {
            case FilterType::LowPass12: return s1.tick (x).low;
            case FilterType::LowPass24: return s2.tick (s1.tick (x).low).low;
            case FilterType::HighPass12: return s1.tick (x).high;
            case FilterType::HighPass24: return s2.tick (s1.tick (x).high).high;
            case FilterType::BandPass12: return s1.tick (x).band * s1.k; // unity gain at the centre
            case FilterType::Notch:
            {
                auto o = s1.tick (x);
                return o.low + o.high;
            }
            case FilterType::count:
            default: return x;
        }
    }

private:
    FilterType type = FilterType::LowPass12;
    SVF s1, s2;
    float driveGain = 1.0f, makeup = 1.0f;
};

} // namespace daw::dsp
