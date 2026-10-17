// An 8x oversampler built from three cascaded linear-phase half-band FIR stages (private to the effects in core/src/fx).
//
// FIR instead of IIR half-bands: the delay is an exact integer, so the Mix-0 null test and the dry path of BasicEffect stay
// sample-exact, and the phase is linear (no smearing of the fundamental against the dry signal at Mix < 100 %).
//
//   base -> up A -> 2x -> up B -> 4x -> up C -> 8x -> (nonlinearity) -> down C -> 4x -> down B -> 2x -> down A -> base
//
// The first stage does the real work (a 79-tap filter with 8 kHz of transition band between 20 and 28 kHz at 48 kHz); the
// others only have to separate the audio band from images far above it, so they are short. The loops run tap by tap over a
// whole block, which the compiler turns into SIMD; they are written for that, not for being clever.
#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

// The block loops read and write different buffers; saying so spares the vectoriser its run-time overlap checks.
#if defined (__GNUC__) || defined (__clang__) || defined (_MSC_VER)
 #define DAW_FX_RESTRICT __restrict
#else
 #define DAW_FX_RESTRICT
#endif

namespace daw::fx
{

// Zero-order modified Bessel function of the first kind, for the Kaiser window.
inline double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    const double q = x * x * 0.25;
    for (int k = 1; k < 60; ++k)
    {
        term *= q / ((double) k * (double) k);
        sum += term;
        if (term < 1.0e-18 * sum)
            break;
    }
    return sum;
}

// Taps of a half-band low-pass: h[0] = 0.5 and non-zero only at odd offsets k = 1, 3, ..., M (M odd, so the filter has
// 2M + 1 taps and a delay of M samples). Kaiser window, normalised so that the DC gain is exactly 1.
struct HalfBandKernel
{
    void design (int halfLength, double beta)
    {
        M = halfLength;
        std::vector<double> h ((size_t) (M + 1) / 2, 0.0);
        const double i0 = besselI0 (beta);
        double sum = 0.0;
        for (int k = 1; k <= M; k += 2)
        {
            const double r = (double) k / (double) (M + 1);
            const double window = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0;
            h[(size_t) (k - 1) / 2] = std::sin (std::numbers::pi * k * 0.5) / (std::numbers::pi * k) * window;
            sum += h[(size_t) (k - 1) / 2];
        }
        const double scale = 0.25 / sum; // the odd taps appear twice (k and -k) and must add up to 0.5
        odd.resize (h.size());
        for (size_t j = 0; j < h.size(); ++j)
            odd[j] = (float) (h[j] * scale);
    }

    int M = 1;
    std::vector<float> odd; // odd[j] is the tap at offset k = 2j + 1
};

// Doubles the rate: n samples in, 2n out. Per input sample the output holds the filtered ("even") phase, then the odd phase,
// which is just the delayed input because the half-band is zero at even offsets.
class HalfBandUp
{
public:
    void prepare (const HalfBandKernel& kernel, int maxIn)
    {
        k = &kernel;
        ext.assign ((size_t) (kernel.M + maxIn), 0.0f);
        acc.assign ((size_t) maxIn, 0.0f);
    }

    void reset() { std::fill (ext.begin(), ext.end(), 0.0f); }

    void process (const float* in, int n, float* out)
    {
        const int M = k->M;
        float* x = ext.data() + M; // x[-M .. -1] is the history
        std::copy (in, in + n, x);
        float* DAW_FX_RESTRICT a = acc.data();
        std::fill (a, a + n, 0.0f);
        for (int j = 0, off = 1; off <= M; ++j, off += 2)
        {
            const float h = k->odd[(size_t) j];
            const float* DAW_FX_RESTRICT p = x - (M + off) / 2;
            const float* DAW_FX_RESTRICT q = x - (M - off) / 2;
            for (int i = 0; i < n; ++i)
                a[i] += h * (p[i] + q[i]);
        }
        const float* centre = x - (M - 1) / 2;
        for (int i = 0; i < n; ++i)
        {
            out[2 * i] = 2.0f * a[i];
            out[2 * i + 1] = centre[i];
        }
        std::copy (x + n - M, x + n, ext.data());
    }

private:
    const HalfBandKernel* k = nullptr;
    std::vector<float> ext, acc;
};

// Halves the rate: 2n samples in, n out. Together with HalfBandUp the pair delays the signal by M samples of the lower rate.
class HalfBandDown
{
public:
    void prepare (const HalfBandKernel& kernel, int maxOut)
    {
        k = &kernel;
        even.assign ((size_t) (kernel.M + maxOut), 0.0f);
        odd.assign ((size_t) (kernel.M + maxOut), 0.0f);
        acc.assign ((size_t) maxOut, 0.0f);
    }

    void reset()
    {
        std::fill (even.begin(), even.end(), 0.0f);
        std::fill (odd.begin(), odd.end(), 0.0f);
    }

    void process (const float* in, int n, float* out)
    {
        const int M = k->M;
        float* xe = even.data() + M;
        float* xo = odd.data() + M;
        for (int i = 0; i < n; ++i)
        {
            xe[i] = in[2 * i];
            xo[i] = in[2 * i + 1];
        }
        // The filter is centred on the input sample 2i - M, an odd index, so the centre tap reads the odd phase and all the
        // other taps the even phase.
        float* DAW_FX_RESTRICT a = acc.data();
        const float* centre = xo - (M + 1) / 2;
        for (int i = 0; i < n; ++i)
            a[i] = 0.5f * centre[i];
        for (int j = 0, off = 1; off <= M; ++j, off += 2)
        {
            const float h = k->odd[(size_t) j];
            const float* DAW_FX_RESTRICT p = xe - (M + off) / 2;
            const float* DAW_FX_RESTRICT q = xe - (M - off) / 2;
            for (int i = 0; i < n; ++i)
                a[i] += h * (p[i] + q[i]);
        }
        std::copy (a, a + n, out);
        std::copy (xe + n - M, xe + n, even.data());
        std::copy (xo + n - M, xo + n, odd.data());
    }

private:
    const HalfBandKernel* k = nullptr;
    std::vector<float> even, odd, acc;
};

// The whole 8x chain for one channel. The caller owns the high-rate buffer, so that the nonlinearity can run in place.
class Oversampler8
{
public:
    static constexpr int kFactor = 8;
    static constexpr int kM1 = 39; // base <-> 2x: 79 taps
    static constexpr int kM2 = 13; // 2x <-> 4x: 27 taps
    static constexpr int kM3 = 9;  // 4x <-> 8x: 19 taps
    // Delay of the whole chain (up and down) in samples of the 8x rate.
    static constexpr int kDelayHigh = 8 * kM1 + 4 * kM2 + 2 * kM3;

    // Allocates for blocks of up to maxBlock samples at the base rate.
    void prepare (int maxBlock)
    {
        static const HalfBandKernel k1 = [] { HalfBandKernel k; k.design (kM1, 10.0); return k; }();
        static const HalfBandKernel k2 = [] { HalfBandKernel k; k.design (kM2, 10.0); return k; }();
        static const HalfBandKernel k3 = [] { HalfBandKernel k; k.design (kM3, 10.0); return k; }();
        upA.prepare (k1, maxBlock);
        upB.prepare (k2, 2 * maxBlock);
        upC.prepare (k3, 4 * maxBlock);
        downC.prepare (k3, 4 * maxBlock);
        downB.prepare (k2, 2 * maxBlock);
        downA.prepare (k1, maxBlock);
        t2.assign ((size_t) maxBlock * 2, 0.0f);
        t4.assign ((size_t) maxBlock * 4, 0.0f);
    }

    void reset()
    {
        upA.reset();
        upB.reset();
        upC.reset();
        downC.reset();
        downB.reset();
        downA.reset();
    }

    // in: n samples; out8x: 8n samples.
    void upsample (const float* in, int n, float* out8x)
    {
        upA.process (in, n, t2.data());
        upB.process (t2.data(), 2 * n, t4.data());
        upC.process (t4.data(), 4 * n, out8x);
    }

    // in8x: 8n samples (left untouched); out: n samples.
    void downsample (const float* in8x, int n, float* out)
    {
        downC.process (in8x, 4 * n, t4.data());
        downB.process (t4.data(), 2 * n, t2.data());
        downA.process (t2.data(), n, out);
    }

private:
    HalfBandUp upA, upB, upC;
    HalfBandDown downC, downB, downA;
    std::vector<float> t2, t4;
};

} // namespace daw::fx
