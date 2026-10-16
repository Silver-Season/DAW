#pragma once

#include <algorithm>
#include <cmath>

namespace daw::dsp
{

// ADSR with a linear attack and exponential decay/release (times are to -80 dB).
class Envelope
{
public:
    enum class Stage
    {
        Idle,
        Attack,
        Decay,
        Sustain,
        Release
    };

    void setParameters (float attackSec, float decaySec, float sustainLevel, float releaseSec, float sampleRate) noexcept
    {
        attackInc = 1.0f / std::max (1.0f, attackSec * sampleRate);
        decayCoef = coefFor (decaySec, sampleRate);
        releaseCoef = coefFor (releaseSec, sampleRate);
        sustain = std::clamp (sustainLevel, 0.0f, 1.0f);
    }

    void noteOn (bool legato = false) noexcept
    {
        if (! legato || stage == Stage::Idle || stage == Stage::Release)
            stage = Stage::Attack; // restart from the current value, avoids clicks on retrigger
    }

    void noteOff() noexcept
    {
        if (stage != Stage::Idle)
            stage = Stage::Release;
    }

    void reset() noexcept
    {
        stage = Stage::Idle;
        value = 0.0f;
    }

    float next() noexcept
    {
        switch (stage)
        {
            case Stage::Idle: break;
            case Stage::Attack:
                value += attackInc;
                if (value >= 1.0f)
                {
                    value = 1.0f;
                    stage = Stage::Decay;
                }
                break;
            case Stage::Decay:
                value = sustain + (value - sustain) * decayCoef;
                if (value - sustain < 1.0e-4f)
                {
                    value = sustain;
                    stage = Stage::Sustain;
                }
                break;
            case Stage::Sustain: value = sustain; break;
            case Stage::Release:
                value *= releaseCoef;
                if (value < 1.0e-4f)
                {
                    value = 0.0f;
                    stage = Stage::Idle;
                }
                break;
        }
        return value;
    }

    // Advances n samples, returns the final value (used for modulation envelopes at control rate).
    float advance (int n) noexcept
    {
        float v = value;
        for (int i = 0; i < n; ++i)
            v = next();
        return v;
    }

    bool isActive() const noexcept { return stage != Stage::Idle; }
    Stage getStage() const noexcept { return stage; }
    float getValue() const noexcept { return value; }

private:
    static float coefFor (float seconds, float sampleRate) noexcept
    {
        const float samples = std::max (1.0f, seconds * sampleRate);
        return std::exp (-9.2103404f / samples); // ln(10000): reaches -80 dB after `seconds`
    }

    Stage stage = Stage::Idle;
    float value = 0.0f;
    float attackInc = 1.0f, decayCoef = 0.0f, releaseCoef = 0.0f, sustain = 1.0f;
};

} // namespace daw::dsp
