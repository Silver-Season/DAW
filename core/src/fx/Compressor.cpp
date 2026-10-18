// Compressor ("comp"), docs/spec/06-effects-mixing.md, task 6.2.
//
// Feed-forward design with the detector in the log domain (Giannoulis, Massberg, Reiss, "Digital Dynamic Range Compressor
// Design", 2012): the key level in dB goes through the static curve with a soft knee, and the gain reduction it asks for is
// smoothed by a "smooth decoupled" peak detector, so attack and release have separate coefficients and the attack time is
// exactly the 63 % time of the gain reduction.
//
// Readouts: 0 = gain reduction in dB (positive, the largest of the last block), 1 = input level in dBFS (peak, fast fall).
#include "Dynamics.h"

#include "daw/fx/BasicEffect.h"

#include <atomic>

namespace daw::fx
{
namespace
{
enum Param
{
    pThreshold,
    pRatio,
    pAttack,
    pRelease,
    pAutoRelease,
    pKnee,
    pMakeup,
    pAutoMakeup,
    pLookahead,
    pDetector,
    pScHpf,
    pScListen
};

constexpr double kMaxLookaheadMs = 10.0;
constexpr double kRmsWindowMs = 10.0;
// Auto release: transients let go after kFastReleaseMs, sustained compression builds up a slow envelope that lets go after
// kSlowReleaseMs, so dense material does not pump.
constexpr double kFastReleaseMs = 40.0, kSlowAttackMs = 400.0, kSlowReleaseMs = 1500.0;

std::vector<ParamInfo> compressorParams()
{
    return {
        { "threshold", "Threshold", "dB", -60.0f, 0.0f, -18.0f, false, {} },
        { "ratio", "Ratio", ":1", 1.0f, 100.0f, 4.0f, true, {} }, // 100 is shown as infinity
        { "attack", "Attack", "ms", 0.05f, 200.0f, 10.0f, true, {} },
        { "release", "Release", "ms", 5.0f, 2000.0f, 120.0f, true, {} },
        { "autoRelease", "Auto release", "", 0.0f, 1.0f, 0.0f, false, dyn::offOn() },
        { "knee", "Knee", "dB", 0.0f, 24.0f, 6.0f, false, {} },
        { "makeup", "Makeup", "dB", 0.0f, 24.0f, 0.0f, false, {} },
        { "autoMakeup", "Auto makeup", "", 0.0f, 1.0f, 0.0f, false, dyn::offOn() },
        { "lookahead", "Lookahead", "ms", 0.0f, (float) kMaxLookaheadMs, 0.0f, false, {} },
        { "detector", "Detector", "", 0.0f, 1.0f, 0.0f, false, { "Peak", "RMS" } },
        { "scHpf", "Sidechain HPF", "Hz", 20.0f, 500.0f, 20.0f, true, {} },
        { "scListen", "Sidechain listen", "", 0.0f, 1.0f, 0.0f, false, dyn::offOn() },
    };
}

// Gain reduction in dB (positive) of the static curve for a level `x`: threshold `t`, slope `s` = 1 - 1/ratio, knee `w`.
inline float staticReduction (float x, float t, float s, float w)
{
    const float over = x - t;
    if (2.0f * over <= -w)
        return 0.0f;
    if (2.0f * over < w)
    {
        const float d = over + 0.5f * w;
        return s * d * d / (2.0f * w);
    }
    return s * over;
}

class CompressorEffect final : public BasicEffect
{
public:
    CompressorEffect() : BasicEffect (compressorParams()) {}

    std::string_view type() const override { return "comp"; }
    bool acceptsSidechain() const override { return true; }
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
        keyFilter.prepare (sampleRate);
        meter.prepare (sampleRate);
        for (auto* s : { &thresholdSmooth, &slopeSmooth, &kneeSmooth, &makeupSmooth, &autoMakeupSmooth })
            s->prepare (sampleRate);
        rmsCoef = dyn::timeCoef (kRmsWindowMs, sampleRate);
        fastReleaseCoef = dyn::timeCoef (kFastReleaseMs, sampleRate);
        slowAttackCoef = dyn::timeCoef (kSlowAttackMs, sampleRate);
        slowReleaseCoef = dyn::timeCoef (kSlowReleaseMs, sampleRate);
        for (int i = 0; i <= pScListen; ++i)
            paramChanged (i, value (i));
    }

    void resetWet() override
    {
        delay.reset();
        keyFilter.snap();
        meter.reset();
        for (auto* s : { &thresholdSmooth, &slopeSmooth, &kneeSmooth, &makeupSmooth, &autoMakeupSmooth })
            s->snap();
        meanSquare = 0.0f;
        held = smoothed = slow = 0.0f;
        reductionDb.store (0.0f, std::memory_order_relaxed);
    }

    void paramChanged (int index, float v) override
    {
        switch (index)
        {
            case pThreshold: thresholdSmooth.setTarget (v); break;
            case pRatio: slopeSmooth.setTarget (v >= 99.99f ? 1.0f : 1.0f - 1.0f / v); break;
            case pAttack: attackCoef = dyn::timeCoef (v, rate); break;
            case pRelease: releaseCoef = dyn::timeCoef (v, rate); break;
            case pKnee: kneeSmooth.setTarget (v); break;
            case pMakeup: makeupSmooth.setTarget (v); break;
            case pAutoMakeup: autoMakeupSmooth.setTarget (v > 0.5f ? 1.0f : 0.0f); break;
            case pLookahead:
                lookahead = std::clamp ((int) std::lround (v * 0.001 * rate), 0, delay.lines[0].empty() ? 0 : delay.maxDelay());
                break;
            case pScHpf: keyFilter.setFreq (v); break;
            default: break;
        }
    }

    void processWet (float* const* io, int numChannels, int numSamples, const float* const* sidechain) override
    {
        const int last = numChannels - 1;
        const float* const* key = sidechain != nullptr ? sidechain : io;
        const bool rms = value (pDetector) > 0.5f;
        const bool listen = value (pScListen) > 0.5f;
        const bool autoRelease = value (pAutoRelease) > 0.5f;
        const int la = lookahead;
        keyFilter.advance (numSamples);

        float inputPeak = 0.0f, maxReduction = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            const float t = thresholdSmooth.next(), s = slopeSmooth.next(), w = kneeSmooth.next();
            const float makeupDb = makeupSmooth.next() + autoMakeupSmooth.next() * 0.5f * -t * s;

            const float kl = keyFilter.process (0, key[0][i]);
            const float kr = last > 0 ? keyFilter.process (1, key[last][i]) : kl;
            float levelDb;
            if (rms)
            {
                meanSquare += (0.5f * (kl * kl + kr * kr) - meanSquare) * rmsCoef;
                levelDb = meanSquare <= 1.0e-12f ? -120.0f : 10.0f * std::log10 (meanSquare);
            }
            else
                levelDb = gainToDb (std::max (std::abs (kl), std::abs (kr)));

            const float wanted = staticReduction (levelDb, t, s, w);
            if (autoRelease)
            {
                held = std::max (wanted, held + (wanted - held) * fastReleaseCoef);
                slow += (held - slow) * (held > slow ? slowAttackCoef : slowReleaseCoef);
                const float peakHeld = std::max (held, slow);
                smoothed += (peakHeld - smoothed) * attackCoef;
            }
            else
            {
                held = std::max (wanted, held + (wanted - held) * releaseCoef);
                smoothed += (held - smoothed) * attackCoef;
            }
            maxReduction = std::max (maxReduction, smoothed);
            const float gain = dbToGain (makeupDb - smoothed);

            inputPeak = std::max (inputPeak, std::abs (io[0][i]));
            if (last > 0)
                inputPeak = std::max (inputPeak, std::abs (io[last][i]));

            if (listen)
            {
                io[0][i] = delay.push (0, kl, la);
                if (last > 0)
                    io[last][i] = delay.push (1, kr, la);
            }
            else
            {
                io[0][i] = delay.push (0, io[0][i], la) * gain;
                if (last > 0)
                    io[last][i] = delay.push (1, io[last][i], la) * gain;
            }
            delay.advance();
        }
        meter.block (inputPeak, numSamples);
        reductionDb.store (maxReduction, std::memory_order_relaxed);
    }

private:
    dyn::DelayLine delay;
    dyn::KeyFilter keyFilter;
    dyn::LevelMeter meter;
    Smoother thresholdSmooth, slopeSmooth, kneeSmooth, makeupSmooth, autoMakeupSmooth;
    float attackCoef = 1.0f, releaseCoef = 1.0f, rmsCoef = 1.0f;
    float fastReleaseCoef = 1.0f, slowAttackCoef = 1.0f, slowReleaseCoef = 1.0f;
    float meanSquare = 0.0f;
    float held = 0.0f, smoothed = 0.0f, slow = 0.0f; // gain reduction in dB at the stages of the detector
    int lookahead = 0;
    std::atomic<float> reductionDb { 0.0f };
};
} // namespace

std::unique_ptr<Effect> makeCompressor() { return std::make_unique<CompressorEffect>(); }
} // namespace daw::fx
