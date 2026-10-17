#include "daw/dsp/SincInterpolator.h"

#include <cmath>
#include <numbers>

namespace daw::dsp
{
namespace
{
double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 60; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

double sinc (double x)
{
    if (std::abs (x) < 1e-12)
        return 1.0;
    const double px = std::numbers::pi * x;
    return std::sin (px) / px;
}
} // namespace

SincInterpolator::SincInterpolator()
{
    constexpr double beta = 8.0;
    constexpr int half = kTaps / 2;
    const double norm = besselI0 (beta);
    table.resize ((size_t) (kPhases + 1) * kTaps);

    for (int p = 0; p <= kPhases; ++p)
    {
        const double frac = (double) p / kPhases;
        double row[kTaps], sum = 0.0;
        for (int j = 0; j < kTaps; ++j)
        {
            const double d = frac - (double) (j - (half - 1)); // distance from the tap at offset j - 7
            const double r = d / half;
            const double w = std::abs (r) < 1.0 ? besselI0 (beta * std::sqrt (1.0 - r * r)) / norm : 0.0;
            row[j] = sinc (d) * w;
            sum += row[j];
        }
        // Unity gain at DC for every phase, otherwise the level would wobble with the fractional position.
        for (int j = 0; j < kTaps; ++j)
            table[(size_t) p * kTaps + (size_t) j] = (float) (row[j] / sum);
    }
}

float SincInterpolator::valueAt (const float* data, int64_t length, double pos) const noexcept
{
    const double nearest = std::floor (pos + 0.5);
    if (std::abs (pos - nearest) < 1e-9)
    {
        const auto i = (int64_t) nearest;
        return (i >= 0 && i < length) ? data[i] : 0.0f;
    }

    constexpr int half = kTaps / 2;
    const double fl = std::floor (pos);
    const auto base = (int64_t) fl - (half - 1);
    const double fp = (pos - fl) * kPhases;
    int p = (int) fp;
    if (p >= kPhases)
        p = kPhases - 1;
    const float t = (float) (fp - p);

    const float* a = table.data() + (size_t) p * kTaps;
    const float* b = a + kTaps;

    float acc = 0.0f;
    if (base >= 0 && base + kTaps <= length)
    {
        for (int j = 0; j < kTaps; ++j)
            acc += data[base + j] * (a[j] + t * (b[j] - a[j]));
    }
    else
    {
        for (int j = 0; j < kTaps; ++j)
        {
            const int64_t i = base + j;
            if (i >= 0 && i < length)
                acc += data[i] * (a[j] + t * (b[j] - a[j]));
        }
    }
    return acc;
}

const SincInterpolator& sharedSincInterpolator()
{
    static const SincInterpolator instance;
    return instance;
}
} // namespace daw::dsp
