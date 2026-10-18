// Limiter ("limiter"), docs/spec/06-effects-mixing.md, task 6.2.
//
// Lookahead brickwall limiter. For every sample the detector works out the gain r that would bring it to the ceiling. The
// gain applied is the moving average over W samples of the moving minimum over W samples of r (with a release glide in
// between): every average then only contains values at or below the r of the sample W - 1 steps back, so delaying the audio
// by that much guarantees the output never passes the ceiling, and the gain ramps down linearly ahead of a peak instead of
// jumping. The moving minimum is a monotonic queue in a preallocated ring (O(1) amortised per sample).
//
// True peak: the detector sees the signal 4x oversampled by a polyphase windowed-sinc interpolator, so peaks between the
// samples (the ones a DAC or a lossy encoder produces) are caught too. The interpolator looks kTpCentre samples ahead, which
// the window gives up: W = lookahead - kTpCentre + 1, the latency stays the lookahead.
//
// Readouts: 0 = gain reduction in dB (positive, the largest of the last block), 1 = input level in dBFS (peak, fast fall).
#include "Dynamics.h"

#include "daw/fx/BasicEffect.h"

#include <array>
#include <atomic>
#include <numbers>

namespace daw::fx
{
namespace
{
enum Param
{
    pCeiling,
    pGain,
    pRelease,
    pAutoRelease,
    pLookahead,
    pTruePeak
};

constexpr double kMaxLookaheadMs = 5.0;
constexpr int kTpTaps = 16;     // per phase of the 4x interpolator
constexpr int kTpCentre = kTpTaps / 2;
// Auto release: the longer and deeper the limiting, the slower the release (up to kAutoSlowdown times), so dense material
// does not distort while single peaks still recover quickly.
constexpr double kSustainMs = 300.0;
constexpr float kAutoSlowdown = 4.0f;

std::vector<ParamInfo> limiterParams()
{
    return {
        { "ceiling", "Ceiling", "dB", -12.0f, -0.1f, -0.3f, false, {} },
        { "gain", "Gain", "dB", 0.0f, 24.0f, 0.0f, false, {} },
        { "release", "Release", "ms", 1.0f, 1000.0f, 50.0f, true, {} },
        { "autoRelease", "Auto release", "", 0.0f, 1.0f, 1.0f, false, dyn::offOn() },
        { "lookahead", "Lookahead", "ms", 1.0f, (float) kMaxLookaheadMs, 2.0f, false, {} },
        { "truePeak", "True peak", "", 0.0f, 1.0f, 1.0f, false, dyn::offOn() },
    };
}

// Phases 1..3 of a 4x interpolator: kernel[p - 1][k] weighs history[k] (oldest first) for the point p / 4 of a sample after
// history[kTpCentre - 1]. Kaiser-windowed sinc, each phase normalised to unit gain at DC.
struct TruePeakKernel
{
    TruePeakKernel()
    {
        const auto bessel0 = [] (double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 30; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
            }
            return sum;
        };
        constexpr double beta = 8.0, half = kTpCentre;
        for (int p = 1; p < 4; ++p)
        {
            double sum = 0.0;
            for (int k = 0; k < kTpTaps; ++k)
            {
                const double t = (double) (kTpCentre - 1 - k) + p / 4.0;
                const double r = t / half;
                const double window = std::abs (r) >= 1.0 ? 0.0 : bessel0 (beta * std::sqrt (1.0 - r * r)) / bessel0 (beta);
                const double sinc = std::sin (std::numbers::pi * t) / (std::numbers::pi * t);
                weights[(size_t) p - 1][(size_t) k] = sinc * window;
                sum += sinc * window;
            }
            for (auto& w : weights[(size_t) p - 1])
                w /= sum;
        }
    }

    std::array<std::array<double, kTpTaps>, 3> weights {};
};

const TruePeakKernel& truePeakKernel()
{
    static const TruePeakKernel kernel;
    return kernel;
}

class LimiterEffect final : public BasicEffect
{
public:
    LimiterEffect() : BasicEffect (limiterParams()), kernel (truePeakKernel()) {}

    std::string_view type() const override { return "limiter"; }
    int latencySamples() const override { return lookahead; }

    float readout (int index) const override
    {
        if (index == 0)
            return reductionDb.load (std::memory_order_relaxed);
        if (index == 1)
            return meter.db.load (std::memory_order_relaxed);
        return 0.0f;
    }

protected:
    int maxLatencySamples() const override { return (int) std::ceil (kMaxLookaheadMs * 0.001 * rate); }

    void prepareWet (double sampleRate, int) override
    {
        delay.prepare (maxLatencySamples());
        const size_t cap = (size_t) maxLatencySamples() + 2;
        queueTime.assign (cap, 0);
        queueValue.assign (cap, 1.0f);
        box.assign (cap, 1.0f);
        meter.prepare (sampleRate);
        ceilingSmooth.prepare (sampleRate);
        gainSmooth.prepare (sampleRate);
        sustainCoef = dyn::timeCoef (kSustainMs, sampleRate);
        for (int i = 0; i <= pTruePeak; ++i)
            paramChanged (i, value (i));
    }

    void resetWet() override
    {
        delay.reset();
        meter.reset();
        ceilingSmooth.snap();
        gainSmooth.snap();
        for (auto& h : history)
            h.fill (0.0f);
        previousSegment = 0.0f;
        envelope = 1.0f;
        sustain = 0.0f;
        now = 0;
        restartWindow (1.0f);
        reductionDb.store (0.0f, std::memory_order_relaxed);
    }

    void paramChanged (int index, float v) override
    {
        switch (index)
        {
            case pCeiling: ceilingSmooth.setTarget (dbToGain (v)); break;
            case pGain: gainSmooth.setTarget (dbToGain (v)); break;
            case pRelease: releaseCoef = dyn::timeCoef (v, rate); break;
            case pAutoRelease: break;
            case pLookahead:
                lookahead = std::clamp ((int) std::lround (v * 0.001 * rate), 1, delay.lines[0].empty() ? 1 : delay.maxDelay());
                break;
            default: break;
        }
    }

    void processWet (float* const* io, int numChannels, int numSamples, const float* const*) override
    {
        const int last = numChannels - 1;
        const bool truePeak = value (pTruePeak) > 0.5f;
        const int la = lookahead;
        const int wanted = std::max (1, truePeak ? la - kTpCentre + 1 : la + 1);
        if (wanted != window || truePeak != windowTruePeak)
        {
            window = wanted;
            windowTruePeak = truePeak;
            restartWindow (envelope);
        }
        const bool autoRelease = value (pAutoRelease) > 0.5f;
        const float slowReleaseCoef = autoRelease ? 1.0f - std::pow (1.0f - releaseCoef, 1.0f / kAutoSlowdown) : releaseCoef;

        float inputPeak = 0.0f, minGain = 1.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            const float ceiling = ceilingSmooth.next(), drive = gainSmooth.next();
            float x[2] { io[0][i], io[last][i] };
            inputPeak = std::max ({ inputPeak, std::abs (x[0]), std::abs (x[1]) });
            x[0] *= drive;
            x[1] *= drive;

            float level = 0.0f;
            if (truePeak)
            {
                // The peak of the stretch [c, c + 1) around the centre c of the history, then of the two stretches next
                // to sample c: the output around c depends on both.
                float segment = 0.0f;
                for (int c = 0; c < numChannels; ++c)
                {
                    auto& h = history[(size_t) c];
                    std::copy (h.begin() + 1, h.end(), h.begin());
                    h[kTpTaps - 1] = x[c];
                    segment = std::max (segment, std::abs (h[kTpCentre - 1]));
                    for (const auto& phase : kernel.weights)
                    {
                        double y = 0.0;
                        for (int k = 0; k < kTpTaps; ++k)
                            y += phase[(size_t) k] * h[(size_t) k];
                        segment = std::max (segment, (float) std::abs (y));
                    }
                }
                level = std::max (segment, previousSegment);
                previousSegment = segment;
            }
            else
                level = std::max (std::abs (x[0]), std::abs (x[1]));

            const float r = level > ceiling ? ceiling / level : 1.0f;
            const float floorOfWindow = pushMinimum (r);
            if (autoRelease)
                sustain += ((1.0f - envelope) - sustain) * sustainCoef;
            if (floorOfWindow < envelope)
                envelope = floorOfWindow;
            else
            {
                // Half a gain (6 dB) of sustained reduction gives the slowest release.
                const float coef = autoRelease ? releaseCoef + (slowReleaseCoef - releaseCoef) * std::min (1.0f, sustain * 2.0f)
                                               : releaseCoef;
                envelope += (floorOfWindow - envelope) * coef;
            }

            const float gain = pushAverage (envelope);
            minGain = std::min (minGain, gain);

            for (int c = 0; c < numChannels; ++c)
            {
                // The clamp only catches rounding: the window above already keeps the samples under the ceiling.
                const float y = delay.push (c, x[c], la) * gain;
                io[c][i] = std::clamp (y, -ceiling, ceiling);
            }
            delay.advance();
            ++now;
        }
        meter.block (inputPeak, numSamples);
        reductionDb.store (-gainToDb (minGain), std::memory_order_relaxed);
    }

private:
    // Empties the moving minimum and fills the moving average with `g`, after the window length changed.
    void restartWindow (float g)
    {
        queueHead = queueCount = 0;
        std::fill (box.begin(), box.end(), g);
        boxSum = (double) g * window;
        boxPos = 0;
    }

    float pushMinimum (float r)
    {
        const int cap = (int) queueValue.size();
        while (queueCount > 0)
        {
            const int back = (queueHead + queueCount - 1) % cap;
            if (queueValue[(size_t) back] < r)
                break;
            --queueCount;
        }
        const int slot = (queueHead + queueCount) % cap;
        queueTime[(size_t) slot] = now;
        queueValue[(size_t) slot] = r;
        ++queueCount;
        while (queueTime[(size_t) queueHead] <= now - window)
        {
            queueHead = (queueHead + 1) % cap;
            --queueCount;
        }
        return queueValue[(size_t) queueHead];
    }

    float pushAverage (float g)
    {
        boxSum += (double) g - box[(size_t) boxPos];
        box[(size_t) boxPos] = g;
        if (++boxPos >= window)
        {
            // Start the running sum afresh once per window, so rounding cannot build up over hours.
            boxPos = 0;
            boxSum = 0.0;
            for (int k = 0; k < window; ++k)
                boxSum += box[(size_t) k];
        }
        return (float) (boxSum / window);
    }

    const TruePeakKernel& kernel;
    dyn::DelayLine delay;
    dyn::LevelMeter meter;
    Smoother ceilingSmooth, gainSmooth;
    std::array<std::array<float, kTpTaps>, 2> history {};
    float previousSegment = 0.0f;
    float releaseCoef = 1.0f, sustainCoef = 1.0f, envelope = 1.0f, sustain = 0.0f;
    std::vector<long long> queueTime;
    std::vector<float> queueValue, box;
    int queueHead = 0, queueCount = 0, boxPos = 0;
    double boxSum = 1.0;
    int window = 1, lookahead = 1;
    bool windowTruePeak = true;
    long long now = 0;
    std::atomic<float> reductionDb { 0.0f };
};
} // namespace

std::unique_ptr<Effect> makeLimiter() { return std::make_unique<LimiterEffect>(); }
} // namespace daw::fx
