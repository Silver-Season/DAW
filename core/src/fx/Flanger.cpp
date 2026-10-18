// Flanger ("flanger", docs/spec/06-effects-mixing.md, task 6.2): a short delay (0.1-10 ms) swept by a sine LFO, with
// feedback of either sign. The right channel's LFO runs `phase` degrees behind the left one. The rate is free or
// locked to the song.
#include "ModDelay.h"

#include "daw/fx/BasicEffect.h"

namespace daw::fx
{
namespace
{
class FlangerEffect final : public BasicEffect
{
public:
    enum Param
    {
        pSync,
        pRate,
        pDiv,
        pDepth,
        pDelay,
        pFeedback,
        pPhase
    };

    // The delay is swept by +-kSweep of its own length at full depth.
    static constexpr float kSweep = 0.8f;

    FlangerEffect()
        : BasicEffect ({ { "sync", "Sync", "", 0.0f, 1.0f, 0.0f, false, { "Off", "On" } },
                         { "rate", "Rate", "Hz", 0.02f, 10.0f, 0.3f, true, {} },
                         { "div", "Division", "", 0.0f, 5.0f, 3.0f, false, { "1/4", "1/2", "1/1", "2/1", "4/1", "8/1" } },
                         { "depth", "Depth", "%", 0.0f, 100.0f, 70.0f, false, {} },
                         { "delay", "Delay", "ms", 0.1f, 10.0f, 1.5f, true, {} },
                         { "feedback", "Feedback", "%", -95.0f, 95.0f, 50.0f, false, {} },
                         { "phase", "Phase", "deg", 0.0f, 180.0f, 90.0f, false, {} } },
                       50.0f)
    {
    }

    std::string_view type() const override { return "flanger"; }

    double tailSeconds() const override
    {
        const double fb = std::abs (value (pFeedback)) * 0.01;
        const double loops = fb > 0.001 ? std::min (400.0, std::log (1.0e-5) / std::log (fb)) : 0.0;
        return (1.0 + loops) * (value (pDelay) * (1.0 + kSweep) * 0.001) + 0.05;
    }

    void setTransport (const TransportInfo& t) override { clock.setTransport (t); }

protected:
    void prepareWet (double sampleRate, int) override
    {
        // The longest delay is 10 ms swept up by kSweep.
        const int maxSamples = (int) std::ceil (10.0 * (1.0 + kSweep) * 0.001 * sampleRate) + 4;
        for (auto& line : lines)
            line.allocate (maxSamples);
        clock.prepare (sampleRate);
        for (auto* s : { &smRate, &smDepth, &smDelay, &smFeedback, &smPhase })
            s->prepare (sampleRate);
    }

    void resetWet() override
    {
        for (auto& line : lines)
            line.clear();
        clock.reset();
        readTargets();
        for (auto* s : { &smRate, &smDepth, &smDelay, &smFeedback, &smPhase })
            s->snap();
    }

    void processWet (float* const* io, int numChannels, int numSamples, const float* const*) override
    {
        readTargets();
        const int channels = std::min (numChannels, 2);
        const bool sync = value (pSync) > 0.5f;
        const double period = mod::kSlowDivisions[(size_t) std::clamp ((int) value (pDiv), 0, 5)];
        clock.begin (sync, period);
        const double syncStep = clock.syncIncrement (period);
        const float samplesPerMs = (float) (rate * 0.001);
        float offCos = 1.0f, offSin = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float hz = smRate.next(), depth = smDepth.next(), delayMs = smDelay.next(), fb = smFeedback.next(),
                        phaseDeg = smPhase.next();
            if ((i & mod::kControlMask) == 0)
            {
                const float o = phaseDeg * (std::numbers::pi_v<float> / 180.0f);
                offCos = std::cos (o);
                offSin = std::sin (o);
            }
            const float a = (float) (mod::kTwoPi * clock.phase());
            const float sn = std::sin (a), cs = std::cos (a);

            const float base = delayMs * samplesPerMs;
            const float sweep = base * kSweep * depth;

            for (int c = 0; c < channels; ++c)
            {
                // The right channel is the left LFO rotated by the stereo phase.
                const float lfo = c == 0 ? sn : sn * offCos + cs * offSin;
                auto& line = lines[(size_t) c];
                const float wet = line.read (base + sweep * lfo);
                line.write (mod::flush (io[c][i] + fb * wet));
                io[c][i] = wet;
            }
            clock.advance (sync ? syncStep : (double) hz / rate);
        }
    }

private:
    void readTargets()
    {
        smRate.setTarget (value (pRate));
        smDepth.setTarget (value (pDepth) * 0.01f);
        smDelay.setTarget (value (pDelay));
        smFeedback.setTarget (value (pFeedback) * 0.01f);
        smPhase.setTarget (value (pPhase));
    }

    std::array<mod::DelayLine, 2> lines;
    mod::ModClock clock;
    Smoother smRate, smDepth, smDelay, smFeedback, smPhase;
};
} // namespace

std::unique_ptr<Effect> makeFlanger() { return std::make_unique<FlangerEffect>(); }
} // namespace daw::fx
