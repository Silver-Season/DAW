#include "Metronome.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kPi = 3.14159265358979323846;
// The sine and the noise are scaled so that the louder (strong) click peaks at about 1.0 before the volume.
constexpr float kWeakLevel = 0.5f, kStrongLevel = 1.0f; // 6 dB apart
} // namespace

void Metronome::prepare (double sr) noexcept
{
    sampleRate = sr;
    decay = std::exp (-1.0 / (kDecaySeconds * sr));
    length = (int64_t) (kDecaySeconds * sr * 9.0); // e^-9: inaudible

    // RBJ band-pass, constant skirt gain (peak gain = Q): b0 = alpha, b1 = 0, b2 = -alpha
    const double w0 = 2.0 * kPi * kBandpassHz / sr, q = 2.0;
    const double alpha = std::sin (w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    b0 = alpha / a0 * 2.0; // x2: the band-pass takes a lot of energy off a noise burst
    b2 = -alpha / a0 * 2.0;
    a1 = -2.0 * std::cos (w0) / a0;
    a2 = (1.0 - alpha) / a0;
    active = false;
}

void Metronome::startClick (bool strong, Sound sound, float gain) noexcept
{
    active = true;
    currentSound = sound;
    amplitude = (strong ? kStrongLevel : kWeakLevel) * gain;
    phaseInc = 2.0 * kPi * (strong ? 1500.0 : 1000.0) / sampleRate;
    age = 0;
    envelope = 1.0;
    noiseState = 12345u;
    z1 = z2 = 0.0;
}

float Metronome::nextSample() noexcept
{
    // The envelope and the sine are evaluated at age + 1, so the very first sample of a click is not zero.
    envelope *= decay;
    ++age;
    float s;
    if (currentSound == Sound::classic)
        s = (float) std::sin (phaseInc * (double) age);
    else
    {
        noiseState = noiseState * 1664525u + 1013904223u; // fixed-seed LCG: the same click every time
        const double x = (double) (int32_t) noiseState / 2147483648.0;
        const double y = b0 * x + z1;
        z1 = -a1 * y + z2; // b1 = 0
        z2 = b2 * x - a2 * y;
        s = (float) y;
    }
    const float out = s * (float) envelope * amplitude;
    if (age >= length)
        active = false;
    return out;
}

void Metronome::render (float* left, float* right, int numSamples, double blockStartBeat, double samplesPerBeat, double beatsPerBar, float gain,
                        Sound sound, bool emit) noexcept
{
    if (numSamples <= 0)
        return;

    // Where the clicks of this block begin. The rounded sample offset decides which block a beat belongs to, so
    // that no beat is lost or doubled at a block boundary.
    constexpr int kMaxClicks = 8;
    int starts[kMaxClicks];
    bool strongs[kMaxClicks];
    int numClicks = 0;
    if (emit && gain > 0.0f && samplesPerBeat > 0.0)
    {
        const long first = (long) std::floor (blockStartBeat);
        const long last = first + (long) ((double) numSamples / samplesPerBeat) + 2;
        for (long beat = first; beat <= last && numClicks < kMaxClicks; ++beat)
        {
            const long offset = std::lround (((double) beat - blockStartBeat) * samplesPerBeat);
            if (offset < 0 || offset >= numSamples)
                continue;
            const double inBar = beatsPerBar > 1.0 ? beatsPerBar : 1.0;
            const double fromBarLine = (double) beat - std::floor ((double) beat / inBar + 1.0e-9) * inBar;
            starts[numClicks] = (int) offset;
            strongs[numClicks] = fromBarLine < 1.0e-6 || inBar <= 1.0;
            ++numClicks;
        }
    }

    int pos = 0;
    const auto ringUntil = [&] (int end) {
        for (; pos < end && active; ++pos)
        {
            const float s = nextSample();
            left[pos] += s;
            right[pos] += s;
        }
        pos = std::max (pos, end);
    };
    for (int i = 0; i < numClicks; ++i)
    {
        ringUntil (starts[i]);
        startClick (strongs[i], sound, gain);
    }
    ringUntil (numSamples);
}
