#pragma once

#include "daw/dsp/Wavetable.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace daw::dsp
{

// Wavetable oscillator with up to 16 unison voices, detune, stereo spread and centre/side blend.
class WavetableOscillator
{
public:
    static constexpr int kMaxUnison = 16;

    void setTable (const Wavetable* t) noexcept { table = t; }

    // Resets every unison phase. randomAmount 0 = all voices start at `phase`, 1 = fully random.
    void reset (float phase, float randomAmount, uint32_t seed) noexcept
    {
        uint32_t s = seed ? seed : 1u;
        for (int i = 0; i < kMaxUnison; ++i)
        {
            s = s * 1664525u + 1013904223u;
            const double r = (double) (s >> 8) / (double) (1u << 24);
            double p = phase + randomAmount * r;
            phases[(size_t) i] = p - std::floor (p);
        }
    }

    // detuneCents is the spread between the outermost voices, spread in [0, 1], blend in [0, 1]
    // (0 = only the centre voice is audible, 1 = side voices as loud as the centre).
    void setUnison (int voices, float detuneCents, float spread, float blend) noexcept
    {
        numVoices = voices < 1 ? 1 : (voices > kMaxUnison ? kMaxUnison : voices);
        float total = 0.0f;
        for (int i = 0; i < numVoices; ++i)
        {
            const float pos = numVoices == 1 ? 0.0f : (2.0f * (float) i / (float) (numVoices - 1) - 1.0f); // -1 .. 1
            ratios[(size_t) i] = std::pow (2.0f, pos * detuneCents * 0.5f / 1200.0f);
            const bool centre = numVoices == 1 || std::abs (pos) <= 1.0f / (float) (numVoices - 1) + 1.0e-4f;
            const float gain = centre ? 1.0f : blend;
            const float pan = pos * spread; // -1 .. 1
            const float angle = (pan + 1.0f) * 0.25f * 3.14159265f;
            gainL[(size_t) i] = gain * std::cos (angle);
            gainR[(size_t) i] = gain * std::sin (angle);
            total += gain * gain;
        }
        const float norm = 1.0f / std::sqrt (std::max (total, 1.0e-6f)) * 1.41421356f; // centre voice at unity
        for (int i = 0; i < numVoices; ++i)
        {
            gainL[(size_t) i] *= norm;
            gainR[(size_t) i] *= norm;
        }
    }

    // Renders one stereo sample at frequency `freqHz`.
    void tick (double freqHz, float framePos, double sampleRate, float& outL, float& outR) noexcept
    {
        float l = 0.0f, r = 0.0f;
        if (table != nullptr)
        {
            const double baseInc = freqHz / sampleRate;
            const int mip = Wavetable::mipForIncrement (baseInc * ratios[(size_t) (numVoices - 1)]);
            for (int i = 0; i < numVoices; ++i)
            {
                const float s = table->sample (framePos, phases[(size_t) i], mip);
                l += s * gainL[(size_t) i];
                r += s * gainR[(size_t) i];
                double p = phases[(size_t) i] + baseInc * ratios[(size_t) i];
                if (p >= 1.0)
                    p -= std::floor (p);
                phases[(size_t) i] = p;
            }
        }
        outL = l;
        outR = r;
    }

    int getNumVoices() const noexcept { return numVoices; }

private:
    const Wavetable* table = nullptr;
    int numVoices = 1;
    std::array<double, kMaxUnison> phases {};
    std::array<float, kMaxUnison> ratios { 1.0f }, gainL { 0.70710678f }, gainR { 0.70710678f };
};

} // namespace daw::dsp
