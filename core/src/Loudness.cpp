#include "daw/dsp/Loudness.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace daw::dsp
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;
    double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// The two stages of the K-filter, bilinear from their analog prototypes (the derivation libebur128 and BS.1770's
// own 48 kHz table agree on), so any rate gets the right curve.
void kWeighting (double rate, Biquad& shelf, Biquad& highPass)
{
    {
        const double gainDb = 3.999843853973347, q = 0.7071752369554196, fc = 1681.974450955533;
        const double k = std::tan (kPi * fc / rate);
        const double vh = std::pow (10.0, gainDb / 20.0), vb = std::pow (vh, 0.4996667741545416);
        const double a0 = 1.0 + k / q + k * k;
        shelf.b0 = (vh + vb * k / q + k * k) / a0;
        shelf.b1 = 2.0 * (k * k - vh) / a0;
        shelf.b2 = (vh - vb * k / q + k * k) / a0;
        shelf.a1 = 2.0 * (k * k - 1.0) / a0;
        shelf.a2 = (1.0 - k / q + k * k) / a0;
    }
    {
        const double q = 0.5003270373238773, fc = 38.13547087602444;
        const double k = std::tan (kPi * fc / rate);
        const double a0 = 1.0 + k / q + k * k;
        highPass.b0 = 1.0;
        highPass.b1 = -2.0;
        highPass.b2 = 1.0;
        highPass.a1 = 2.0 * (k * k - 1.0) / a0;
        highPass.a2 = (1.0 - k / q + k * k) / a0;
    }
}

double lufsOf (double meanSquare) { return meanSquare > 0.0 ? -0.691 + 10.0 * std::log10 (meanSquare) : -200.0; }

// Mean squares (L + R) of blocks of `blockLen` frames starting every `hop` frames, from per-frame K-weighted power.
std::vector<double> blockPowers (const std::vector<double>& prefix, size_t blockLen, size_t hop)
{
    std::vector<double> out;
    if (blockLen == 0 || prefix.size() <= blockLen)
        return out;
    const size_t frames = prefix.size() - 1;
    for (size_t start = 0; start + blockLen <= frames; start += hop)
        out.push_back ((prefix[start + blockLen] - prefix[start]) / (double) blockLen);
    return out;
}

double gatedLoudness (const std::vector<double>& powers, double relativeLu, double& threshold)
{
    double sum = 0.0;
    size_t n = 0;
    for (const double p : powers)
        if (lufsOf (p) > -70.0)
        {
            sum += p;
            ++n;
        }
    if (n == 0)
    {
        threshold = -70.0;
        return -70.0;
    }
    threshold = lufsOf (sum / (double) n) + relativeLu;
    sum = 0.0;
    n = 0;
    for (const double p : powers)
        if (lufsOf (p) > -70.0 && lufsOf (p) > threshold)
        {
            sum += p;
            ++n;
        }
    return n > 0 ? lufsOf (sum / (double) n) : -70.0;
}
} // namespace

double truePeakDb (const float* samples, size_t numFrames)
{
    // 4x: three points between each pair of samples from a Hann-windowed sinc of 16 taps per side.
    constexpr int kSide = 16;
    double kernel[3][2 * kSide];
    for (int phase = 1; phase <= 3; ++phase)
        for (int k = -kSide + 1; k <= kSide; ++k)
        {
            const double x = (double) k - (double) phase / 4.0; // distance from the point to sample n + k
            const double sinc = std::abs (x) < 1.0e-12 ? 1.0 : std::sin (kPi * x) / (kPi * x);
            const double window = 0.5 + 0.5 * std::cos (kPi * x / (double) (kSide + 1));
            kernel[phase - 1][k + kSide - 1] = sinc * window;
        }
    double peak = 0.0;
    for (size_t n = 0; n < numFrames; ++n)
    {
        peak = std::max (peak, (double) std::abs (samples[n]));
        for (int phase = 0; phase < 3; ++phase)
        {
            double v = 0.0;
            for (int k = -kSide + 1; k <= kSide; ++k)
            {
                const long long i = (long long) n + k;
                if (i >= 0 && i < (long long) numFrames)
                    v += kernel[phase][k + kSide - 1] * samples[i];
            }
            peak = std::max (peak, std::abs (v));
        }
    }
    return peak > 0.0 ? 20.0 * std::log10 (peak) : -120.0;
}

LoudnessReport measureLoudness (const float* left, const float* right, size_t numFrames, double sampleRate)
{
    LoudnessReport r;
    if (numFrames == 0 || sampleRate <= 0.0)
        return r;
    Biquad shelfL, hpL;
    kWeighting (sampleRate, shelfL, hpL);
    Biquad shelfR = shelfL, hpR = hpL;
    std::vector<double> prefix (numFrames + 1, 0.0); // running sum of the K-weighted power of both channels
    float peak = 0.0f;
    for (size_t i = 0; i < numFrames; ++i)
    {
        const double l = hpL.process (shelfL.process (left[i]));
        const double rr = hpR.process (shelfR.process (right[i]));
        prefix[i + 1] = prefix[i] + l * l + rr * rr;
        peak = std::max ({ peak, std::abs (left[i]), std::abs (right[i]) });
    }
    r.samplePeakDb = peak > 0.0f ? 20.0 * std::log10 ((double) peak) : -120.0;
    r.truePeakDb = std::max (truePeakDb (left, numFrames), truePeakDb (right, numFrames));

    double threshold = 0.0;
    const auto momentary = blockPowers (prefix, (size_t) std::llround (0.4 * sampleRate), (size_t) std::llround (0.1 * sampleRate));
    r.integratedLufs = gatedLoudness (momentary, -10.0, threshold);

    const auto shortTerm = blockPowers (prefix, (size_t) std::llround (3.0 * sampleRate), (size_t) std::llround (1.0 * sampleRate));
    gatedLoudness (shortTerm, -20.0, threshold);
    std::vector<double> kept;
    for (const double p : shortTerm)
        if (lufsOf (p) > -70.0 && lufsOf (p) > threshold)
            kept.push_back (lufsOf (p));
    if (kept.size() >= 2)
    {
        std::sort (kept.begin(), kept.end());
        const auto at = [&kept] (double q) { return kept[(size_t) std::llround (q * (double) (kept.size() - 1))]; };
        r.rangeLu = at (0.95) - at (0.10);
    }
    return r;
}
} // namespace daw::dsp
