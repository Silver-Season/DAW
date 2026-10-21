// Drive ("drive"): six waveshapers behind an 8x oversampler (docs/spec/06-effects-mixing.md, task 6.2).
//
// Aliasing. Oversampling alone cannot reach -80 dB for a 15 kHz sine at +24 dB drive: the 13th harmonic of the clipped wave
// folds straight back to 3 kHz at a 192 kHz rate and is only ~25 dB down. So the shapers also use second-order
// antiderivative anti-aliasing (ADAA2; Bilbao et al. 2017, Parker et al. 2019): instead of f(x[n]) the output is the mean of
// f(x(t)) under a triangular kernel two samples wide, with x(t) the straight line between samples. That puts zeros of the
// spectrum at multiples of the oversampled rate, exactly where the harmonics fold back to low frequencies. At 4x the
// straight-line assumption leaves a floor around -72 dB at 0 dBFS in; at 8x the worst bin is below -85 dB, which is why the
// oversampler is 8x and not the 4x of the spec text.
#include "Oversampler.h"
#include "daw/fx/BasicEffect.h"

#include <array>
#include <cassert>
#include <functional>

namespace daw::fx
{
namespace
{
enum Mode
{
    modeSoft,
    modeTape,
    modeTube,
    modeHard,
    modeFold,
    modeCrush,
    modeCount
};

// The shapers see at most this much: every saturating curve is constant long before it, and the ADAA arithmetic keeps its
// precision on numbers of a few thousand rather than a million.
constexpr double kMaxInput = 64.0;

// The shaper's input limit. Written with the comparisons this way round so that a NaN lands on the lower bound instead of
// reaching a table index (a NaN cast to int is undefined behaviour and, as an index, a read from anywhere).
inline double limitInput (double v)
{
    constexpr double lowest = -kMaxInput, highest = kMaxInput - 1.0e-3; // the cell index must stay below the last cell
    return v > lowest ? (v < highest ? v : highest) : lowest;
}

// A shaper f with its first and second antiderivatives F1 (F1(0) = 0) and F2 (F2(0) = 0), tabulated cell by cell as quintic
// polynomials (a Hermite fit to F2, F1 and f at the cell ends, so F2 is C2 and the pieces do not show). F1 and f are read
// back as derivatives of the same polynomials. Either the shaper saturates (the table spans the whole +-kMaxInput range the
// shaper can be given, so the audio loop never needs a case for "beyond"; the exact continuation exists for the odd call
// outside it) or it is periodic with zero mean (the wavefolder), where F2 = periodic part + drift * x.
class CellTable
{
public:
    static constexpr int kPerUnit = 16;
    static constexpr double kStep = 1.0 / kPerUnit;

    // Corners of f (hard clip, fold) sit on grid nodes, so every cell is a smooth piece and the fit is exact there.
    void build (const std::function<double (double)>& f, bool periodicShape, double extent)
    {
        periodic = periodicShape;
        lo = periodic ? -1.0 : -extent;
        period = periodic ? extent : 2.0 * extent;
        cells = (int) std::lround (period * kPerUnit);
        // The periodic read-back picks the cell with a bit mask, and kMaxInput units must be a whole number of periods.
        assert (! periodic || ((cells & (cells - 1)) == 0 && (int) (kMaxInput * kPerUnit) % cells == 0));
        const int nodes = cells + 1;

        std::vector<double> f1 (nodes, 0.0), f2 (nodes, 0.0);
        // 4-point Gauss-Legendre on a cell: exact for the cubics and the piecewise-linear shapers used here, and far below
        // rounding for the smooth ones.
        static constexpr double gx[4] { -0.8611363115940526, -0.3399810435848563, 0.3399810435848563, 0.8611363115940526 };
        static constexpr double gw[4] { 0.3478548451374538, 0.6521451548625461, 0.6521451548625461, 0.3478548451374538 };
        auto integrate = [&] (auto&& g, double a, double b)
        {
            double sum = 0.0;
            for (int q = 0; q < 4; ++q)
                sum += gw[q] * g (0.5 * (a + b) + 0.5 * (b - a) * gx[q]);
            return 0.5 * (b - a) * sum;
        };
        const int zero = (int) std::lround (-lo * kPerUnit);
        for (int dir = -1; dir <= 1; dir += 2)
        {
            for (int i = zero; i + dir >= 0 && i + dir < nodes; i += dir)
            {
                const int j = i + dir, lowNode = std::min (i, j);
                const double a = nodeX (lowNode), b = nodeX (lowNode + 1), sign = dir > 0 ? 1.0 : -1.0;
                f1[(size_t) j] = f1[(size_t) i] + sign * integrate (f, a, b);
                const double base = f1[(size_t) lowNode];
                f2[(size_t) j] = f2[(size_t) i] + sign * integrate ([&] (double x) { return base + integrate (f, a, x); }, a, b);
            }
        }
        // F2 over one period grows by 4 * mean(F1) if F1 does not average out to zero.
        drift = periodic ? (f2[(size_t) nodes - 1] - f2[0]) / period : 0.0;

        coef.assign ((size_t) cells * 6, 0.0);
        const double h = kStep;
        for (int c = 0; c < cells; ++c)
        {
            const double x0 = nodeX (c), x1 = nodeX (c + 1);
            const double p0 = f2[(size_t) c] - drift * x0, p1 = f2[(size_t) c + 1] - drift * x1;
            const double d0 = f1[(size_t) c] - drift, d1 = f1[(size_t) c + 1] - drift;
            const double s0 = f (x0), s1 = f (x1);
            double* a = &coef[(size_t) c * 6];
            a[0] = p0;
            a[1] = h * d0;
            a[2] = 0.5 * h * h * s0;
            a[3] = 10.0 * (p1 - p0) - 6.0 * h * d0 - 4.0 * h * d1 - 1.5 * h * h * s0 + 0.5 * h * h * s1;
            a[4] = -15.0 * (p1 - p0) + 8.0 * h * d0 + 7.0 * h * d1 + 1.5 * h * h * s0 - h * h * s1;
            a[5] = 6.0 * (p1 - p0) - 3.0 * h * d0 - 3.0 * h * d1 - 0.5 * h * h * s0 + 0.5 * h * h * s1;
        }
        fPos = f (-lo);
        fNeg = f (lo);
        end1Pos = f1[(size_t) nodes - 1];
        end2Pos = f2[(size_t) nodes - 1];
        end1Neg = f1[0];
        end2Neg = f2[0];
    }

    bool isPeriodic() const { return periodic; }
    const double* coefficients() const { return coef.data(); }
    double lower() const { return lo; }

    // The polynomial of the cell that u (in cells from the lower end, 0 <= u < cells) falls in.
    static double evaluateInside (const double* coef, double u)
    {
        const int i = (int) u;
        return horner (coef + (size_t) i * 6, u - i);
    }

    // The input is limited to +-kMaxInput, so shifting by that many units makes it positive, and since the number of cells is
    // a power of two the cell is the low bits of the index: no floor, no division.
    double F2periodic (double x) const
    {
        const double u = (x - lo) * kPerUnit + kMaxInput * kPerUnit;
        const int iu = (int) u;
        return horner (&coef[(size_t) (iu & (cells - 1)) * 6], u - iu) + drift * x;
    }

    // Any x: the general read-back, with the closed-form continuation beyond a saturating table. Used by the special cases of
    // the ADAA formula, not by the main loop.
    double F2 (double x) const
    {
        if (periodic)
            return F2periodic (x);
        if (x >= -lo)
        {
            const double d = x + lo;
            return end2Pos + end1Pos * d + 0.5 * fPos * d * d;
        }
        if (x <= lo)
        {
            const double d = x - lo;
            return end2Neg + end1Neg * d + 0.5 * fNeg * d * d;
        }
        const double u = (x - lo) * kPerUnit;
        return horner (&coef[(size_t) std::min (cells - 1, (int) u) * 6], u - std::min (cells - 1, (int) u));
    }

    double F1 (double x) const
    {
        if (! periodic)
        {
            if (x >= -lo)
                return end1Pos + fPos * (x + lo);
            if (x <= lo)
                return end1Neg + fNeg * (x - lo);
        }
        const double u = periodic ? wrap (x) : (x - lo) * kPerUnit;
        const int i = std::min (cells - 1, (int) u);
        const double* a = &coef[(size_t) i * 6];
        const double t = u - i;
        return (a[1] + t * (2.0 * a[2] + t * (3.0 * a[3] + t * (4.0 * a[4] + 5.0 * t * a[5])))) * kPerUnit + drift;
    }

    double f (double x) const
    {
        if (! periodic)
        {
            if (x >= -lo)
                return fPos;
            if (x <= lo)
                return fNeg;
        }
        const double u = periodic ? wrap (x) : (x - lo) * kPerUnit;
        const int i = std::min (cells - 1, (int) u);
        const double* a = &coef[(size_t) i * 6];
        const double t = u - i;
        return (2.0 * a[2] + t * (6.0 * a[3] + t * (12.0 * a[4] + 20.0 * t * a[5]))) * (double) kPerUnit * kPerUnit;
    }

private:
    static double nodeX (int i, double origin) { return origin + i * kStep; }
    double nodeX (int i) const { return nodeX (i, lo); }

    // Position in cells inside the period, 0 <= u < cells.
    double wrap (double x) const
    {
        double u = (x - lo) * kPerUnit;
        const double total = (double) cells;
        u -= total * std::floor (u / total);
        return u;
    }

    static double horner (const double* a, double t)
    {
        // Estrin's scheme: the same polynomial with a shorter dependency chain.
        const double t2 = t * t;
        return (a[0] + a[1] * t) + t2 * ((a[2] + a[3] * t) + t2 * (a[4] + a[5] * t));
    }

    bool periodic = false;
    double lo = -16.0, period = 32.0, drift = 0.0;
    int cells = 0;
    std::vector<double> coef;
    double fPos = 1.0, fNeg = -1.0, end1Pos = 0.0, end2Pos = 0.0, end1Neg = 0.0, end2Neg = 0.0;
};

// Tape: a gentler knee than tanh, from two tanh curves of different steepness; slope 1 at the origin, saturates at +-1.
constexpr double kTapeA = 0.6, kTapeB = 0.4, kTapeSteep = 4.0 / 3.0, kTapeSoft = 0.5;
double tapeF (double x) { return kTapeA * std::tanh (kTapeSteep * x) + kTapeB * std::tanh (kTapeSoft * x); }

// Tube: the positive half saturates like tanh, the negative half earlier and lower, so that even harmonics appear.
// Slope 1 at the origin and continuous curvature there.
constexpr double kTubeNeg = 0.7;
double tubeF (double x) { return x >= 0.0 ? std::tanh (x) : kTubeNeg * std::tanh (x / kTubeNeg); }

// Fold: a triangle wavefolder. The input is reflected each time it passes +-1; period 4, slope 1 at the origin.
double foldF (double x)
{
    const double u = x - 4.0 * std::floor ((x + 1.0) * 0.25); // -1 <= u < 3
    return u <= 1.0 ? u : 2.0 - u;
}

constexpr int kShaperTables = 5;

const CellTable& tableFor (int mode)
{
    static const std::array<CellTable, kShaperTables> tables = []
    {
        std::array<CellTable, kShaperTables> t;
        t[modeSoft].build ([] (double x) { return std::tanh (x); }, false, kMaxInput);
        t[modeTape].build (tapeF, false, kMaxInput);
        t[modeTube].build (tubeF, false, kMaxInput);
        t[modeHard].build ([] (double x) { return std::clamp (x, -1.0, 1.0); }, false, kMaxInput);
        t[modeFold].build (foldF, true, 4.0);
        return t;
    }();
    return tables[(size_t) std::clamp (mode, 0, kShaperTables - 1)];
}

// ADAA2 for one sample with every special case spelled out: the nearly-equal neighbours that the block loop leaves to this.
double adaa2Careful (const CellTable& table, double x0, double x1, double x2)
{
    auto mean = [&] (double a, double b)
    {
        return std::abs (a - b) > 1.0e-7 ? (table.F2 (a) - table.F2 (b)) / (a - b) : table.F1 (0.5 * (a + b));
    };
    if (std::abs (x0 - x2) > 1.0e-5)
        return 2.0 * (mean (x0, x1) - mean (x1, x2)) / (x0 - x2);
    // x0 == x2: the limit, 2 (F2(b) - F2(a) - F1(a) (b - a)) / (b - a)^2 with a = x0 and b = x1; and when b == a as well the
    // signal stands still and f of it is the answer.
    const double db = x1 - x0;
    if (std::abs (db) > 1.0e-4)
        return 2.0 * (table.F2 (x1) - table.F2 (x0) - table.F1 (x0) * db) / (db * db);
    return table.f (0.5 * (x0 + x1));
}

constexpr int kCrushLevels = 8; // Crush mode: steps of 1/8, a 4-bit staircase

double crushF (double x) { return std::clamp (std::round (x * kCrushLevels) / kCrushLevels, -1.0, 1.0); }

class DriveEffect final : public BasicEffect
{
public:
    enum Param { pMode, pDrive, pTone, pBias, pInput };

    explicit DriveEffect (bool distortion = false)
        : BasicEffect ({ { "mode", "Mode", "", 0.0f, (float) (modeCount - 1), distortion ? 3.0f : 0.0f, false, { "Soft", "Tape", "Tube", "Hard", "Fold", "Crush" } },
                         { "drive", "Drive", "dB", 0.0f, 36.0f, distortion ? 18.0f : 6.0f, false, {} },
                         { "tone", "Tone", "%", -100.0f, 100.0f, 0.0f, false, {} },
                         { "bias", "Bias", "%", -100.0f, 100.0f, 0.0f, false, {} },
                         { "input", "Input", "dB", -24.0f, 24.0f, 0.0f, false, {} } }),
          isDistortion (distortion)
    {
        tableFor (0); // build the tables here, not on first use in the audio thread
    }

    std::string_view type() const override { return isDistortion ? "distortion" : "drive"; }
    int latencySamples() const override { return kLatency; }
    double tailSeconds() const override { return 0.1; } // the DC blocker settles in a few tens of milliseconds

protected:
    int maxLatencySamples() const override { return kLatency; }

    void prepareWet (double sampleRate, int maxBlock) override
    {
        for (auto& c : channel)
            c.os.prepare (maxBlock);
        for (auto* b : { &pre, &post[0], &post[1], &gainBuf, &biasBuf, &compBuf })
            b->assign ((size_t) maxBlock, 0.0f);
        biasOffset.assign ((size_t) maxBlock, 0.0);
        osBuf.assign ((size_t) maxBlock * Oversampler8::kFactor, 0.0f);
        const size_t high = (size_t) maxBlock * Oversampler8::kFactor;
        xs.assign (high + 2, 0.0);
        f2s.assign (high + 2, 0.0);
        for (auto* s : { &gainSmooth, &biasSmooth, &toneSmooth, &compSmooth })
            s->prepare (sampleRate);
        dcCoef = 1.0 - 2.0 * std::numbers::pi * 5.0 / sampleRate;
        refreshTargets();
    }

    void resetWet() override
    {
        refreshTargets();
        for (auto* s : { &gainSmooth, &biasSmooth, &toneSmooth, &compSmooth })
            s->snap();
        for (auto& c : channel)
        {
            c.os.reset();
            // The shaper starts at rest, which with a bias is a constant input, not zero.
            c.x1 = c.x2 = (double) biasSmooth.current;
            c.delayed.fill (0.0f);
            c.dcIn = c.dcOut = 0.0;
            c.low.reset();
            c.high.reset();
        }
        updateTilt();
        tiltMoving = false;
        tiltActive = toneSmooth.current != 0.0f;
        toneTick = 0;
    }

    void paramChanged (int index, float) override
    {
        if (index <= pInput)
            refreshTargets();
    }

    void processWet (float* const* io, int numChannels, int numSamples, const float* const*) override
    {
        const int n = numSamples;
        // The smoothed gains are shared by the channels; computing them once keeps the two sides locked together.
        for (int i = 0; i < n; ++i)
        {
            gainBuf[(size_t) i] = gainSmooth.next();
            biasBuf[(size_t) i] = biasSmooth.next();
            compBuf[(size_t) i] = compSmooth.next();
        }
        const int mode = (int) value (pMode);
        // What the shaper makes of the bias alone is a constant, not a signal: taking it out here is what keeps silence silent
        // with Bias turned up (the DC blocker would otherwise take a few hundred milliseconds to do it).
        if (biasSmooth.smoothing() || mode != offsetMode || biasBuf[0] != offsetBias)
        {
            for (int i = 0; i < n; ++i)
                biasOffset[(size_t) i] = curve (mode, (double) biasBuf[(size_t) i]);
            offsetMode = mode;
            offsetBias = biasBuf[(size_t) n - 1];
            offsetValue = biasOffset[(size_t) n - 1];
        }
        else
            std::fill (biasOffset.begin(), biasOffset.begin() + n, offsetValue);

        for (int c = 0; c < numChannels; ++c)
        {
            auto& ch = channel[(size_t) c];
            for (int i = 0; i < n; ++i)
                pre[(size_t) i] = io[c][i] * gainBuf[(size_t) i];
            ch.os.upsample (pre.data(), n, osBuf.data());
            if (mode == modeCrush)
                runCrush (ch, n * Oversampler8::kFactor);
            else
                runShaper (tableFor (mode), ch, n * Oversampler8::kFactor);
            ch.os.downsample (osBuf.data(), n, post[(size_t) c].data());
        }

        // DC blocker, tone and level compensation. The tone smoother moves one step per sample and the filters follow it every
        // 16 samples counted from the start of the stream, so that the result does not depend on how it is cut into blocks.
        for (int start = 0; start < n;)
        {
            const int len = std::min (16 - toneTick, n - start);
            if (numChannels == 2 && ! tiltActive)
            {
                // The DC blocker is a recursion, so one channel at a time it waits on itself; two side by side overlap.
                auto &l = channel[0], &r = channel[1];
                double lIn = l.dcIn, lOut = l.dcOut, rIn = r.dcIn, rOut = r.dcOut;
                for (int i = start; i < start + len; ++i)
                {
                    const double xl = post[0][(size_t) i], xr = post[1][(size_t) i];
                    lOut = xl - lIn + dcCoef * lOut;
                    rOut = xr - rIn + dcCoef * rOut;
                    lIn = xl;
                    rIn = xr;
                    io[0][i] = (float) lOut * compBuf[(size_t) i];
                    io[1][i] = (float) rOut * compBuf[(size_t) i];
                }
                l.dcIn = lIn; l.dcOut = lOut; r.dcIn = rIn; r.dcOut = rOut;
            }
            else
            {
                for (int c = 0; c < numChannels; ++c)
                {
                    auto& ch = channel[(size_t) c];
                    for (int i = start; i < start + len; ++i)
                    {
                        const double x = post[(size_t) c][(size_t) i];
                        const double dc = x - ch.dcIn + dcCoef * ch.dcOut;
                        ch.dcIn = x;
                        ch.dcOut = dc;
                        const float v = tiltActive ? ch.high.tick (ch.low.tick ((float) dc)) : (float) dc;
                        io[c][i] = v * compBuf[(size_t) i];
                    }
                }
            }
            start += len;

            for (int k = 0; k < len; ++k)
                toneSmooth.next();
            toneTick += len;
            if (toneTick == 16)
            {
                toneTick = 0;
                const bool moving = toneSmooth.smoothing();
                if (moving || tiltMoving)
                    updateTilt();
                tiltMoving = moving;
                // At exactly 0 the shelves are identities; leaving them out saves two biquads per sample (their state is
                // cleared so that a later move starts from silence, which is what an identity filter would hold anyway).
                const bool wasActive = tiltActive;
                tiltActive = moving || toneSmooth.current != 0.0f;
                if (wasActive && ! tiltActive)
                    for (auto& ch : channel)
                    {
                        ch.low.reset();
                        ch.high.reset();
                    }
            }
        }
    }

private:
    // Delay of the oversampler and the shaper in samples of the 8x rate: the filters, one for the ADAA and the rest to make a
    // whole number of base-rate samples, so that Mix < 100 % lines up with the dry path.
    static constexpr int kAlign = (Oversampler8::kFactor - (Oversampler8::kDelayHigh + 1) % Oversampler8::kFactor) % Oversampler8::kFactor;
    static constexpr int kLatency = (Oversampler8::kDelayHigh + 1 + kAlign) / Oversampler8::kFactor;
    static_assert ((Oversampler8::kDelayHigh + 1 + kAlign) % Oversampler8::kFactor == 0, "the latency must be a whole number of samples");

    struct Channel
    {
        Oversampler8 os;
        double x1 = 0.0, x2 = 0.0;              // the last two inputs of the shaper (ADAA history)
        std::array<float, kAlign + 1> delayed {}; // the last outputs of the shaper, waiting for the alignment delay
        double dcIn = 0.0, dcOut = 0.0;
        Biquad low, high;
    };

    // x[j + 2] = the shaper's input for sample j (osBuf plus bias, limited to +-kMaxInput) and f2[j + 2] = F2 of it. The
    // saturating tables span the whole limited range, so a lookup is a bare index and a polynomial, whatever the signal does.
    void lookup (const CellTable& table, int count, double* x, double* f2) const
    {
        const float* in = osBuf.data();
        const float* bias = biasBuf.data();
        const int groups = count / Oversampler8::kFactor;
        if (table.isPeriodic())
        {
            for (int i = 0; i < groups; ++i)
            {
                const double b = bias[i];
                for (int k = 0; k < Oversampler8::kFactor; ++k)
                {
                    const int j = i * Oversampler8::kFactor + k;
                    const double v = limitInput ((double) in[j] + b);
                    x[j + 2] = v;
                    f2[j + 2] = table.F2periodic (v);
                }
            }
            return;
        }
        const double* coef = table.coefficients();
        const double lo = table.lower();
        for (int i = 0; i < groups; ++i)
        {
            const double b = bias[i];
            for (int k = 0; k < Oversampler8::kFactor; ++k)
            {
                const int j = i * Oversampler8::kFactor + k;
                const double v = limitInput ((double) in[j] + b);
                x[j + 2] = v;
                f2[j + 2] = CellTable::evaluateInside (coef, (v - lo) * CellTable::kPerUnit);
            }
        }
    }

    // ADAA2 over `count` samples of osBuf: first the table lookups, then one division per sample.
    void runShaper (const CellTable& table, Channel& ch, int count)
    {
        double* x = xs.data();
        double* f2 = f2s.data();
        x[0] = ch.x2;
        x[1] = ch.x1;
        f2[0] = table.F2 (x[0]);
        f2[1] = table.F2 (x[1]);
        lookup (table, count, x, f2);

        // y(k) belongs to the time of x[k + 1]. With D(a, b) = (F2(a) - F2(b)) / (a - b) the formula is
        // 2 (D(x2, x1) - D(x1, x0)) / (x2 - x0); written over a common denominator it needs one division instead of three.
        // The main loop takes the formula as it is; the rare samples whose neighbours are too close for it (a signal standing
        // still, a peak) are counted and done again afterwards by adaa2Careful().
        auto regular = [&] (int k)
        {
            const double a = x[k + 2] - x[k + 1], b = x[k + 1] - x[k];
            return std::abs (a) > 1.0e-7 && std::abs (b) > 1.0e-7 && std::abs (a + b) > 1.0e-5;
        };
        auto exact = [&] (int k)
        {
            if (regular (k))
            {
                const double a = x[k + 2] - x[k + 1], b = x[k + 1] - x[k];
                return 2.0 * ((f2[k + 2] - f2[k + 1]) * b - (f2[k + 1] - f2[k]) * a) / (a * b * (a + b));
            }
            return adaa2Careful (table, x[k + 2], x[k + 1], x[k]);
        };
        float* out = osBuf.data();
        const double* offset = biasOffset.data();
        for (int i = 0; i < kAlign; ++i)
            out[i] = ch.delayed[(size_t) i];
        const int body = std::max (0, count - kAlign);
        int irregular = 0;
        for (int g = 0; g * Oversampler8::kFactor < body; ++g)
        {
            const int from = g * Oversampler8::kFactor, to = std::min (body, from + Oversampler8::kFactor);
            const double off = offset[g];
            for (int k = from; k < to; ++k)
            {
                const double a = x[k + 2] - x[k + 1], b = x[k + 1] - x[k];
                const double num = (f2[k + 2] - f2[k + 1]) * b - (f2[k + 1] - f2[k]) * a;
                const bool ok = std::abs (a) > 1.0e-7 && std::abs (b) > 1.0e-7 && std::abs (a + b) > 1.0e-5;
                irregular += ok ? 0 : 1;
                out[k + kAlign] = (float) (2.0 * num / (ok ? a * b * (a + b) : 1.0) - off);
            }
        }
        if (irregular > 0)
            for (int k = 0; k < body; ++k)
                if (! regular (k))
                    out[k + kAlign] = (float) (exact (k) - offset[k / Oversampler8::kFactor]);
        for (int k = body; k < count; ++k)
            ch.delayed[(size_t) (k - body)] = (float) (exact (k) - offset[k / Oversampler8::kFactor]);
        ch.x1 = x[count + 1];
        ch.x2 = x[count];
    }

    // The staircase has no useful antiderivative and is meant to be rough, so it is just evaluated (and delayed like the
    // others, so that switching the mode does not change the latency).
    void runCrush (Channel& ch, int count)
    {
        double* x = xs.data();
        x[0] = ch.x2;
        x[1] = ch.x1;
        for (int j = 0; j < count; ++j)
            x[j + 2] = limitInput ((double) osBuf[(size_t) j] + (double) biasBuf[(size_t) (j / Oversampler8::kFactor)]);
        emit (ch, count, x, [&] (int k) { return crushF (x[k + 1]); });
    }

    // Writes y(0 .. count) into osBuf delayed by kAlign samples (which the lookup has already consumed), the oldest ones from
    // the previous block, and keeps the last kAlign for the next. x holds the shaper inputs, of which the last two are history.
    template <class F>
    void emit (Channel& ch, int count, const double* x, F&& y)
    {
        float* out = osBuf.data();
        const double* offset = biasOffset.data();
        for (int i = 0; i < kAlign; ++i)
            out[i] = ch.delayed[(size_t) i];
        const int body = std::max (0, count - kAlign);
        for (int k = 0; k < body; ++k)
            out[k + kAlign] = (float) (y (k) - offset[k / Oversampler8::kFactor]);
        for (int k = body; k < count; ++k)
            ch.delayed[(size_t) (k - body)] = (float) (y (k) - offset[k / Oversampler8::kFactor]);
        ch.x1 = x[count + 1];
        ch.x2 = x[count];
    }

    // The plain curve, for the level compensation.
    static double curve (int mode, double x)
    {
        switch (mode)
        {
            case modeCrush: return crushF (x);
            default: return tableFor (mode).f (x); // the same function the shaper's special cases fall back on
        }
    }

    // Level compensation: the gain that makes a -12 dBFS sine come out at the RMS it went in with, for the current mode,
    // drive and bias. Only ever a cut (a saturator that is quieter than its input is left alone), so that no setting gets
    // louder than the dry sound, and not more than 26 dB, so that a hard clip at 36 dB does not turn the output down to nothing.
    double compensation() const
    {
        const int mode = (int) value (pMode);
        const double amplitude = 0.25 * std::pow (10.0, (double) value (pDrive) / 20.0);
        const double bias = (double) value (pBias) * 0.005;
        constexpr int steps = 64;
        double mean = 0.0, sq = 0.0;
        for (int i = 0; i < steps; ++i)
        {
            const double y = curve (mode, amplitude * std::sin (2.0 * std::numbers::pi * i / steps) + bias);
            mean += y;
            sq += y * y;
        }
        mean /= steps;
        const double rmsOut = std::sqrt (std::max (1.0e-12, sq / steps - mean * mean)); // the DC blocker removes the mean
        const double rmsIn = 0.25 / std::sqrt (2.0);
        return std::clamp (rmsIn / rmsOut, 0.05, 1.0);
    }

    void refreshTargets()
    {
        gainSmooth.setTarget (dbToGain (value (pDrive) + value (pInput)));
        biasSmooth.setTarget (value (pBias) * 0.005f); // +-100 % is half of the clipping level
        toneSmooth.setTarget (value (pTone));
        compSmooth.setTarget ((float) compensation());
    }

    // A tilt around 1 kHz: lows down and highs up by up to 8 dB for positive Tone, the other way round for negative.
    void updateTilt()
    {
        const double t = (double) toneSmooth.current * 0.08;
        channel[0].low.set (Biquad::Type::lowShelf, 1000.0, 0.7, -t, rate);
        channel[0].high.set (Biquad::Type::highShelf, 1000.0, 0.7, t, rate);
        copyCoefficients (channel[0].low, channel[1].low);
        copyCoefficients (channel[0].high, channel[1].high);
    }

    static void copyCoefficients (const Biquad& from, Biquad& to)
    {
        to.b0 = from.b0; to.b1 = from.b1; to.b2 = from.b2; to.a1 = from.a1; to.a2 = from.a2;
    }

    const bool isDistortion;
    std::array<Channel, 2> channel;
    std::vector<float> pre, osBuf, gainBuf, biasBuf, compBuf;
    std::array<std::vector<float>, 2> post;
    std::vector<double> xs, f2s, biasOffset;
    Smoother gainSmooth, biasSmooth, toneSmooth, compSmooth;
    double dcCoef = 0.999;
    bool tiltMoving = false, tiltActive = false;
    int toneTick = 0;
    int offsetMode = -1;
    float offsetBias = 0.0f;
    double offsetValue = 0.0;
};
} // namespace

std::unique_ptr<Effect> makeDrive() { return std::make_unique<DriveEffect>(); }
std::unique_ptr<Effect> makeDistortion() { return std::make_unique<DriveEffect> (true); }
} // namespace daw::fx
