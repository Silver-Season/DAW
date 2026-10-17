#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace voice
{
inline float
midiFromHz (float hz)
{
    return hz > 0 ? 69.0f + 12.0f * std::log2 (hz / 440.0f) : -1.0f;
}
inline float
hzFromMidi (float note)
{
    return 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f);
}
inline int
nearestNote (float note, int root, int scale)
{
    static constexpr bool allowed[4][12]{ { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
                                          { 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1 },
                                          { 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0 },
                                          { 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0 } };
    const int around = (int)std::round (note);
    int best = around;
    float distance = 100;
    for (int n = around - 12; n <= around + 12; ++n)
        if (allowed[std::clamp (scale, 0, 3)][((n - root) % 12 + 12) % 12] && std::abs (note - (float)n) < distance)
        {
            best = n;
            distance = std::abs (note - (float)n);
        }
    return best;
}
class PitchDetector
{
  public:
    void prepare (double sr)
    {
        stride = std::max (1, (int)(sr / 12000));
        rate = sr / stride;
        ring.fill (0);
        pos = filled = sub = hop = 0;
        sum = 0;
        frequency = confidence = 0;
    }
    bool push (float sample)
    {
        sum += sample;
        if (++sub < stride)
            return false;
        ring[(size_t)pos] = sum / (float)stride;
        sum = 0;
        sub = 0;
        pos = (pos + 1) % 768;
        filled = std::min (768, filled + 1);
        if (++hop < 64 || filled < 768)
            return false;
        hop = 0;
        analyse ();
        return true;
    }
    float frequency = 0, confidence = 0;

  private:
    void analyse ()
    {
        std::array<float, 768> x{};
        float energy = 0;
        for (int i = 0; i < 768; ++i)
        {
            x[(size_t)i] = ring[(size_t)((pos + i) % 768)];
            energy += x[(size_t)i] * x[(size_t)i];
        }
        if (energy / 768 < 1.0e-6f)
        {
            frequency = confidence = 0;
            return;
        }
        const int maxTau = std::min (383, (int)(rate / 65)), minTau = std::max (2, (int)(rate / 1000));
        std::array<float, 384> cmnd{};
        cmnd[0] = 1;
        float running = 0;
        for (int tau = 1; tau <= maxTau; ++tau)
        {
            float d = 0;
            for (int i = 0; i < 384; ++i)
            {
                const auto z = x[(size_t)i] - x[(size_t)(i + tau)];
                d += z * z;
            }
            running += d;
            cmnd[(size_t)tau] = running > 0 ? d * (float)tau / running : 1;
        }
        int chosen = -1;
        for (int tau = minTau; tau < maxTau; ++tau)
            if (cmnd[(size_t)tau] < .15f)
            {
                while (tau + 1 <= maxTau && cmnd[(size_t)(tau + 1)] < cmnd[(size_t)tau])
                    ++tau;
                chosen = tau;
                break;
            }
        if (chosen < 0)
        {
            frequency = confidence = 0;
            return;
        }
        float period = (float)chosen;
        if (chosen > 1 && chosen < maxTau)
        {
            const auto a = cmnd[(size_t)(chosen - 1)], b = cmnd[(size_t)chosen], c = cmnd[(size_t)(chosen + 1)];
            const auto den = a - 2 * b + c;
            if (std::abs (den) > 1e-9f)
                period += std::clamp (.5f * (a - c) / den, -.5f, .5f);
        }
        frequency = (float)(rate / period);
        confidence = std::clamp (1 - cmnd[(size_t)chosen], 0.0f, 1.0f);
    }
    std::array<float, 768> ring{};
    int pos = 0, filled = 0, stride = 4, sub = 0, hop = 0;
    float sum = 0;
    double rate = 12000;
};
}
