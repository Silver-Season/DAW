// Gate ("gate"), docs/spec/06-effects-mixing.md, task 6.2.
//
// A peak detector drives a small state machine: the gate opens when the key rises above the threshold and starts to close
// only after the key fell below threshold - hysteresis and the hold time ran out, so a signal hovering around the threshold
// does not chatter. The gain glides to 1 with the attack time and down to the floor with the release time.
//
// Readouts: 0 = gain reduction in dB (positive, the largest of the last block), 1 = input level in dBFS (peak, fast fall),
// 2 = 1 while the gate is open (including the hold), 0 when closed.
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
    pHysteresis,
    pAttack,
    pHold,
    pRelease,
    pFloor,
    pLookahead,
    pScHpf
};

constexpr double kMaxLookaheadMs = 5.0;
// The detector falls this fast between the peaks of the waveform: slow enough to ride over the zero crossings of a bass
// note, fast enough that the release time stays the gate's own.
constexpr double kDetectorFallMs = 10.0;

std::vector<ParamInfo> gateParams()
{
    return {
        { "threshold", "Threshold", "dB", -80.0f, 0.0f, -40.0f, false, {} },
        { "hysteresis", "Hysteresis", "dB", 0.0f, 20.0f, 4.0f, false, {} },
        { "attack", "Attack", "ms", 0.01f, 50.0f, 0.5f, true, {} },
        { "hold", "Hold", "ms", 0.0f, 500.0f, 20.0f, false, {} },
        { "release", "Release", "ms", 5.0f, 2000.0f, 100.0f, true, {} },
        { "floor", "Floor", "dB", -80.0f, 0.0f, -80.0f, false, {} },
        { "lookahead", "Lookahead", "ms", 0.0f, (float) kMaxLookaheadMs, 0.0f, false, {} },
        { "scHpf", "Sidechain HPF", "Hz", 20.0f, 500.0f, 20.0f, true, {} },
    };
}

class GateEffect final : public BasicEffect
{
public:
    GateEffect() : BasicEffect (gateParams()) {}

    std::string_view type() const override { return "gate"; }
    bool acceptsSidechain() const override { return true; }
    int latencySamples() const override { return lookahead; }

    float readout (int index) const override
    {
        if (index == 0)
            return reductionDb.load (std::memory_order_relaxed);
        if (index == 1)
            return meter.db.load (std::memory_order_relaxed);
        if (index == 2)
            return openFlag.load (std::memory_order_relaxed);
        return 0.0f;
    }

protected:
    int maxLatencySamples() const override { return (int) std::ceil (kMaxLookaheadMs * 0.001 * rate); }

    void prepareWet (double sampleRate, int) override
    {
        delay.prepare (maxLatencySamples());
        keyFilter.prepare (sampleRate);
        meter.prepare (sampleRate);
        thresholdSmooth.prepare (sampleRate);
        closeSmooth.prepare (sampleRate);
        floorSmooth.prepare (sampleRate);
        detectorFall = 1.0f - dyn::timeCoef (kDetectorFallMs, sampleRate);
        for (int i = 0; i <= pScHpf; ++i)
            paramChanged (i, value (i));
    }

    void resetWet() override
    {
        delay.reset();
        keyFilter.snap();
        meter.reset();
        thresholdSmooth.snap();
        closeSmooth.snap();
        floorSmooth.snap();
        envelope = 0.0f;
        open = false;
        holdLeft = 0;
        gain = dbToGain (floorSmooth.current);
        reductionDb.store (-gainToDb (gain), std::memory_order_relaxed);
        openFlag.store (0.0f, std::memory_order_relaxed);
    }

    void paramChanged (int index, float v) override
    {
        switch (index)
        {
            case pThreshold:
            case pHysteresis:
                thresholdSmooth.setTarget (value (pThreshold));
                closeSmooth.setTarget (value (pThreshold) - value (pHysteresis));
                break;
            case pAttack: attackCoef = dyn::timeCoef (v, rate); break;
            case pHold: holdSamples = (int) std::lround (v * 0.001 * rate); break;
            case pRelease: releaseCoef = dyn::timeCoef (v, rate); break;
            case pFloor: floorSmooth.setTarget (v); break;
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
        const int la = lookahead;
        keyFilter.advance (numSamples);

        // The thresholds and the floor move once per block (in dB, then to linear): a level comparison does not click,
        // and the floor's glide is slow next to a block.
        for (auto* s : { &thresholdSmooth, &closeSmooth, &floorSmooth })
            dyn::advance (*s, numSamples);
        const float openLevel = dbToGain (thresholdSmooth.current);
        const float closeLevel = dbToGain (closeSmooth.current);
        const float floorGain = dbToGain (floorSmooth.current);

        float inputPeak = 0.0f, minGain = 1.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            const float kl = std::abs (keyFilter.process (0, key[0][i]));
            const float kr = last > 0 ? std::abs (keyFilter.process (1, key[last][i])) : kl;
            envelope = std::max (std::max (kl, kr), envelope * detectorFall);

            if (envelope >= openLevel || (open && envelope >= closeLevel))
            {
                open = true;
                holdLeft = holdSamples;
            }
            else if (open)
            {
                if (holdLeft > 0)
                    --holdLeft;
                else
                    open = false;
            }

            const float target = open ? 1.0f : floorGain;
            gain += (target - gain) * (target > gain ? attackCoef : releaseCoef);
            minGain = std::min (minGain, gain);

            inputPeak = std::max (inputPeak, std::abs (io[0][i]));
            io[0][i] = delay.push (0, io[0][i], la) * gain;
            if (last > 0)
            {
                inputPeak = std::max (inputPeak, std::abs (io[last][i]));
                io[last][i] = delay.push (1, io[last][i], la) * gain;
            }
            delay.advance();
        }
        meter.block (inputPeak, numSamples);
        reductionDb.store (-gainToDb (minGain), std::memory_order_relaxed);
        openFlag.store (open ? 1.0f : 0.0f, std::memory_order_relaxed);
    }

private:
    dyn::DelayLine delay;
    dyn::KeyFilter keyFilter;
    dyn::LevelMeter meter;
    Smoother thresholdSmooth, closeSmooth, floorSmooth;
    float attackCoef = 1.0f, releaseCoef = 1.0f, detectorFall = 0.99f;
    float envelope = 0.0f, gain = 0.0f;
    bool open = false;
    int holdSamples = 0, holdLeft = 0, lookahead = 0;
    std::atomic<float> reductionDb { 0.0f }, openFlag { 0.0f };
};
} // namespace

std::unique_ptr<Effect> makeGate() { return std::make_unique<GateEffect>(); }
} // namespace daw::fx
