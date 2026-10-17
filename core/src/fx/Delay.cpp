// Delay ("delay"): a stereo echo with tempo sync, ping-pong, a filter and a soft clip in the feedback loop, modulation,
// freeze and ducking (docs/spec/06-effects-mixing.md, task 6.2).
#include "daw/fx/BasicEffect.h"

#include "DelayLine.h"

#include <atomic>

namespace daw::fx
{
namespace
{
enum Param
{
    pSync,
    pTimeL,
    pTimeR,
    pDivL,
    pDivR,
    pLink,
    pFeedback,
    pPingPong,
    pHp,
    pLp,
    pModRate,
    pModDepth,
    pFreeze,
    pDucking
};

// Note lengths of the sync divisions in quarter notes, in the order of the choices (shortest first).
constexpr double kDivBeats[] = { 1.0 / 8.0, 1.0 / 6.0, 3.0 / 16.0, 1.0 / 4.0, 1.0 / 3.0, 3.0 / 8.0, 1.0 / 2.0, 2.0 / 3.0,
                                 3.0 / 4.0, 1.0,       4.0 / 3.0,  3.0 / 2.0, 2.0,       8.0 / 3.0, 3.0,       4.0 };
constexpr int kNumDivs = (int) (sizeof (kDivBeats) / sizeof (kDivBeats[0]));

// A whole note at 30 BPM; slower tempos clamp the synced time here.
constexpr double kMaxDelaySeconds = 8.0;
constexpr double kMaxModMs = 4.0;
constexpr double kCrossfadeSeconds = 0.05;
constexpr int kControlBlock = 32;
constexpr double kTailCap = 30.0;

std::vector<ParamInfo> makeParams()
{
    const std::vector<std::string> offOn { "Off", "On" };
    const std::vector<std::string> divs { "1/32", "1/16T", "1/32.", "1/16", "1/8T", "1/16.", "1/8", "1/4T",
                                          "1/8.", "1/4",   "1/2T",  "1/4.", "1/2",  "1/1T",  "1/2.", "1/1" };
    const float lastDiv = (float) (divs.size() - 1);
    return {
        { "sync", "Sync", "", 0.0f, 1.0f, 1.0f, false, offOn },
        { "timeL", "Time L", "ms", 1.0f, 2000.0f, 375.0f, true, {} },
        { "timeR", "Time R", "ms", 1.0f, 2000.0f, 375.0f, true, {} },
        { "divL", "Division L", "", 0.0f, lastDiv, 9.0f, false, divs },
        { "divR", "Division R", "", 0.0f, lastDiv, 9.0f, false, divs },
        { "link", "Link", "", 0.0f, 1.0f, 1.0f, false, offOn },
        { "feedback", "Feedback", "%", 0.0f, 100.0f, 35.0f, false, {} },
        { "pingPong", "Ping-Pong", "", 0.0f, 1.0f, 0.0f, false, offOn },
        { "hp", "High-Pass", "Hz", 20.0f, 2000.0f, 100.0f, true, {} },
        { "lp", "Low-Pass", "Hz", 500.0f, 20000.0f, 8000.0f, true, {} },
        { "modRate", "Mod Rate", "Hz", 0.05f, 10.0f, 0.5f, true, {} },
        { "modDepth", "Mod Depth", "%", 0.0f, 100.0f, 0.0f, false, {} },
        { "freeze", "Freeze", "", 0.0f, 1.0f, 0.0f, false, offOn },
        { "ducking", "Ducking", "%", 0.0f, 100.0f, 0.0f, false, {} },
    };
}

// Linear up to 1, then bends smoothly (matching slope) towards 1.5, so that a feedback of 100 % with the filters open
// saturates instead of running away.
inline float softClip (float x)
{
    const float a = std::abs (x);
    if (a <= 1.0f)
        return x;
    return std::copysign (1.0f + 0.5f * std::tanh (2.0f * (a - 1.0f)), x);
}

class DelayEffect final : public BasicEffect
{
public:
    DelayEffect() : BasicEffect (makeParams(), 30.0f) {}

    std::string_view type() const override { return "delay"; }

    void setTransport (const TransportInfo& t) override
    {
        if (t.bpm > 0.0)
            bpm.store (t.bpm, std::memory_order_relaxed);
    }

    double tailSeconds() const override
    {
        if (value (pFreeze) > 0.5f)
            return kTailCap;
        const double longest = std::max (delaySeconds (0), delaySeconds (1)) + kMaxModMs * 0.001 * value (pModDepth) / 100.0;
        const double fb = value (pFeedback) / 100.0;
        if (fb >= 0.999)
            return kTailCap;
        // Each echo is `fb` times the previous one: -60 dB after 3 / -log10 (fb) more of them.
        const double repeats = fb > 1.0e-3 ? 3.0 / -std::log10 (fb) : 0.0;
        return std::min (kTailCap, longest * (1.0 + repeats));
    }

protected:
    void prepareWet (double sampleRate, int /*maxBlock*/) override
    {
        const int maxDelay = (int) std::ceil ((kMaxDelaySeconds + kMaxModMs * 0.001) * sampleRate) + 4;
        for (auto& l : lines)
            l.allocate (maxDelay);
        for (auto* s : { &feedback, &inputGain, &freezeAmount, &pingPong, &modDepth })
            s->prepare (sampleRate);
        controlCoef = 1.0 - std::exp (-(double) kControlBlock / (0.02 * sampleRate));
        fadeStep = 1.0 / (kCrossfadeSeconds * sampleRate);
        envAttack = (float) (1.0 - std::exp (-1.0 / (0.002 * sampleRate)));
        envRelease = (float) (1.0 - std::exp (-1.0 / (0.25 * sampleRate)));
    }

    void resetWet() override
    {
        for (auto& l : lines)
            l.clear();
        headA = headB = targetDelays();
        fading = false;
        fadePos = 0.0;
        setTargets();
        for (auto* s : { &feedback, &inputGain, &freezeAmount, &pingPong, &modDepth })
            s->snap();
        hpLog = std::log ((double) value (pHp));
        lpLog = std::log ((double) value (pLp));
        updateFilters();
        for (auto* f : { &hp[0], &hp[1], &lp[0], &lp[1] })
            f->reset();
        env = 0.0f;
        lfoPhase = 0.0;
    }

    void processWet (float* const* io, int numChannels, int numSamples, const float* const* /*sidechain*/) override
    {
        float* outL = io[0];
        float* outR = numChannels > 1 ? io[1] : io[0];
        for (int start = 0; start < numSamples; start += kControlBlock)
        {
            const int end = std::min (numSamples, start + kControlBlock);
            updateControls();
            for (int i = start; i < end; ++i)
            {
                const float xL = outL[i], xR = outR[i];

                // Ducking: a fast-attack, slow-release follower of the input; full ducking from -12 dBFS up.
                const float level = std::max (std::abs (xL), std::abs (xR));
                env += (level - env) * (level > env ? envAttack : envRelease);
                const float duck = 1.0f - duckAmount * std::min (1.0f, env * 4.0f);

                // Modulation only adds to the delay, so that with depth 0 the time is exact.
                const float depth = stepExact (modDepth);
                double mod[2] { 0.0, 0.0 };
                if (depth > 0.0f)
                {
                    mod[0] = depth * (0.5 + 0.5 * std::sin (lfoPhase));
                    mod[1] = depth * (0.5 + 0.5 * std::cos (lfoPhase)); // 90 degrees apart: a wider image
                }
                lfoPhase += lfoStep;
                if (lfoPhase > 2.0 * std::numbers::pi)
                    lfoPhase -= 2.0 * std::numbers::pi;

                // A new time is reached by fading from the old read head to a new one, not by moving the head (which
                // would bend the pitch of everything in the line).
                float y[2];
                const float w = fading ? (float) (fadePos * fadePos * (3.0 - 2.0 * fadePos)) : 0.0f;
                for (int c = 0; c < 2; ++c)
                {
                    y[c] = lines[(size_t) c].read (headA[(size_t) c] + mod[c]);
                    if (fading)
                        y[c] += (lines[(size_t) c].read (headB[(size_t) c] + mod[c]) - y[c]) * w;
                }
                if (fading)
                {
                    fadePos += fadeStep;
                    if (fadePos >= 1.0)
                    {
                        headA = headB;
                        fading = false;
                    }
                }

                // Ping-pong: the input (summed to mono) enters the left line only, and each line feeds the other one,
                // so the echoes alternate sides. Blended so that switching it does not click.
                const float pp = stepExact (pingPong), fb = stepExact (feedback);
                const float in = stepExact (inputGain), fz = stepExact (freezeAmount);
                const float mono = 0.5f * (xL + xR);
                const float inL = (1.0f - pp) * xL + pp * mono, inR = (1.0f - pp) * xR;
                const float backL = (1.0f - pp) * y[0] + pp * y[1], backR = (1.0f - pp) * y[1] + pp * y[0];
                float wL = softClip (lp[0].tick (hp[0].tick (in * inL + fb * backL)));
                float wR = softClip (lp[1].tick (hp[1].tick (in * inR + fb * backR)));
                // Frozen, the loop recirculates the echoes untouched: gain 1, no filter, no input.
                wL = fz >= 1.0f ? backL : wL + (backL - wL) * fz;
                wR = fz >= 1.0f ? backR : wR + (backR - wR) * fz;
                lines[0].push (flushTiny (wL));
                lines[1].push (flushTiny (wR));

                if (numChannels > 1)
                {
                    outL[i] = y[0] * duck;
                    outR[i] = y[1] * duck;
                }
                else
                {
                    outL[i] = 0.5f * (y[0] + y[1]) * duck;
                }
            }
        }
    }

private:
    double delaySeconds (int channel) const
    {
        const int c = value (pLink) > 0.5f ? 0 : channel;
        double sec;
        if (value (pSync) > 0.5f)
        {
            const int div = std::clamp ((int) value (c == 0 ? pDivL : pDivR), 0, kNumDivs - 1);
            sec = kDivBeats[div] * 60.0 / bpm.load (std::memory_order_relaxed);
        }
        else
        {
            sec = value (c == 0 ? pTimeL : pTimeR) * 0.001;
        }
        return std::clamp (sec, 0.001, kMaxDelaySeconds);
    }

    std::array<double, 2> targetDelays() const { return { delaySeconds (0) * rate, delaySeconds (1) * rate }; }

    void setTargets()
    {
        const bool frozen = value (pFreeze) > 0.5f;
        feedback.setTarget (value (pFeedback) / 100.0f);
        inputGain.setTarget (frozen ? 0.0f : 1.0f);
        freezeAmount.setTarget (frozen ? 1.0f : 0.0f);
        pingPong.setTarget (value (pPingPong) > 0.5f ? 1.0f : 0.0f);
        modDepth.setTarget ((float) (value (pModDepth) / 100.0 * kMaxModMs * 0.001 * rate));
        lfoStep = 2.0 * std::numbers::pi * value (pModRate) / rate;
        duckAmount = value (pDucking) / 100.0f;
    }

    void updateFilters()
    {
        hp[0].set (Biquad::Type::highPass, std::exp (hpLog), 0.7071, 0.0, rate);
        lp[0].set (Biquad::Type::lowPass, std::exp (lpLog), 0.7071, 0.0, rate);
        copyCoefficients (hp[0], hp[1]);
        copyCoefficients (lp[0], lp[1]);
    }

    static void copyCoefficients (const Biquad& from, Biquad& to)
    {
        to.b0 = from.b0;
        to.b1 = from.b1;
        to.b2 = from.b2;
        to.a1 = from.a1;
        to.a2 = from.a2;
    }

    // Once per control block: knob values become smoother targets, filter corners glide, a new time starts a crossfade.
    void updateControls()
    {
        setTargets();

        const double hpTarget = std::log ((double) value (pHp)), lpTarget = std::log ((double) value (pLp));
        if (std::abs (hpTarget - hpLog) > 1.0e-6 || std::abs (lpTarget - lpLog) > 1.0e-6)
        {
            hpLog += (hpTarget - hpLog) * controlCoef;
            lpLog += (lpTarget - lpLog) * controlCoef;
            updateFilters();
        }

        if (! fading)
        {
            const auto target = targetDelays();
            if (std::abs (target[0] - headA[0]) > 1.0e-6 || std::abs (target[1] - headA[1]) > 1.0e-6)
            {
                headB = target;
                fading = true;
                fadePos = 0.0;
            }
        }
    }

    std::array<DelayLine, 2> lines;
    std::array<Biquad, 2> hp, lp;
    std::array<double, 2> headA {}, headB {}; // delay of each read head in samples, per channel
    bool fading = false;
    double fadePos = 0.0, fadeStep = 0.0;

    Smoother feedback, inputGain, freezeAmount, pingPong, modDepth;
    double hpLog = 0.0, lpLog = 0.0, controlCoef = 1.0;
    double lfoPhase = 0.0, lfoStep = 0.0;
    float env = 0.0f, envAttack = 1.0f, envRelease = 1.0f, duckAmount = 0.0f;

    std::atomic<double> bpm { 120.0 };
};
} // namespace

std::unique_ptr<Effect> makeDelay() { return std::make_unique<DelayEffect>(); }
} // namespace daw::fx
