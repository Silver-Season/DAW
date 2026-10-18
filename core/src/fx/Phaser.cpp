// Phaser ("phaser", docs/spec/06-effects-mixing.md, task 6.2): a cascade of 2-12 first-order all-passes whose corner
// frequency follows a sine LFO around `center` (depth in octaves), with feedback of either sign from the end of the
// cascade to its input. Mixed with the dry signal the all-passes make one notch per pair of stages. The right
// channel's LFO runs `phase` degrees behind the left one.
#include "ModDelay.h"

#include "daw/fx/BasicEffect.h"

namespace daw::fx
{
namespace
{
class PhaserEffect final : public BasicEffect
{
public:
    enum Param
    {
        pStages,
        pSync,
        pRate,
        pDiv,
        pDepth,
        pCenter,
        pFeedback,
        pPhase
    };

    static constexpr int kMaxStages = 12;
    // Full depth sweeps +-2.5 octaves around the centre.
    static constexpr float kOctaves = 2.5f;

    PhaserEffect()
        : BasicEffect ({ { "stages", "Stages", "", 0.0f, 5.0f, 2.0f, false, { "2", "4", "6", "8", "10", "12" } },
                         { "sync", "Sync", "", 0.0f, 1.0f, 0.0f, false, { "Off", "On" } },
                         { "rate", "Rate", "Hz", 0.02f, 10.0f, 0.5f, true, {} },
                         { "div", "Division", "", 0.0f, 5.0f, 3.0f, false, { "1/4", "1/2", "1/1", "2/1", "4/1", "8/1" } },
                         { "depth", "Depth", "%", 0.0f, 100.0f, 70.0f, false, {} },
                         { "center", "Center", "Hz", 100.0f, 8000.0f, 800.0f, true, {} },
                         { "feedback", "Feedback", "%", -95.0f, 95.0f, 40.0f, false, {} },
                         { "phase", "Phase", "deg", 0.0f, 180.0f, 90.0f, false, {} } },
                       50.0f)
    {
    }

    std::string_view type() const override { return "phaser"; }

    double tailSeconds() const override
    {
        // The slowest all-pass (lowest point of the sweep) holds a signal for about 1 / (pi f) seconds; the feedback
        // sends it round the cascade again and again.
        const double lowest = std::max (20.0, value (pCenter) * std::exp2 (-(double) kOctaves * value (pDepth) * 0.01));
        const double perStage = 1.0 / (std::numbers::pi * lowest);
        const double stages = 2.0 * (1.0 + value (pStages));
        const double fb = std::abs (value (pFeedback)) * 0.01;
        const double loops = fb > 0.001 ? std::min (400.0, std::log (1.0e-5) / std::log (fb)) : 0.0;
        return std::min (60.0, (1.0 + loops) * 6.0 * stages * perStage) + 0.05;
    }

    void setTransport (const TransportInfo& t) override { clock.setTransport (t); }

protected:
    void prepareWet (double sampleRate, int) override
    {
        clock.prepare (sampleRate);
        for (auto* s : { &smRate, &smDepth, &smCenter, &smFeedback, &smPhase })
            s->prepare (sampleRate);
    }

    void resetWet() override
    {
        for (auto& ch : state)
            ch.fill (0.0f);
        feedbackSample.fill (0.0f);
        coef.fill (0.0f);
        clock.reset();
        readTargets();
        for (auto* s : { &smRate, &smDepth, &smCenter, &smFeedback, &smPhase })
            s->snap();
    }

    void processWet (float* const* io, int numChannels, int numSamples, const float* const*) override
    {
        readTargets();
        const int channels = std::min (numChannels, 2);
        const int stages = 2 * (1 + std::clamp ((int) value (pStages), 0, 5));
        const bool sync = value (pSync) > 0.5f;
        const double period = mod::kSlowDivisions[(size_t) std::clamp ((int) value (pDiv), 0, 5)];
        clock.begin (sync, period);
        const double syncStep = clock.syncIncrement (period);
        const float maxHz = (float) (0.45 * rate);
        const float piOverFs = (float) (std::numbers::pi / rate);
        float offCos = 1.0f, offSin = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float hz = smRate.next(), fb = smFeedback.next(), phaseDeg = smPhase.next();
            const float depth = smDepth.next(), centre = smCenter.next();

            if ((i & mod::kControlMask) == 0)
            {
                // The all-pass coefficients only need a refresh every few samples (the sweep is slow), which is when
                // the expensive exp2 and tan are paid for.
                const float o = phaseDeg * (std::numbers::pi_v<float> / 180.0f);
                offCos = std::cos (o);
                offSin = std::sin (o);
                const float a = (float) (mod::kTwoPi * clock.phase());
                const float sn = std::sin (a), cs = std::cos (a);
                for (int c = 0; c < channels; ++c)
                {
                    const float lfo = c == 0 ? sn : sn * offCos + cs * offSin;
                    const float f = std::clamp (centre * std::exp2 (kOctaves * depth * lfo), 20.0f, maxHz);
                    const float t = std::tan (piOverFs * f);
                    coef[(size_t) c] = (t - 1.0f) / (t + 1.0f);
                }
            }

            for (int c = 0; c < channels; ++c)
            {
                const float a = coef[(size_t) c];
                auto& z = state[(size_t) c];
                float x = io[c][i] + fb * feedbackSample[(size_t) c];
                for (int s = 0; s < stages; ++s)
                {
                    // y = a x + z, z = x - a y: H (z) = (a + 1/z) / (1 + a/z), unity gain at every frequency.
                    const float y = a * x + z[(size_t) s];
                    z[(size_t) s] = x - a * y;
                    x = y;
                }
                x = mod::flush (x);
                feedbackSample[(size_t) c] = x;
                io[c][i] = x;
            }
            clock.advance (sync ? syncStep : (double) hz / rate);
        }
    }

private:
    void readTargets()
    {
        smRate.setTarget (value (pRate));
        smDepth.setTarget (value (pDepth) * 0.01f);
        smCenter.setTarget (value (pCenter));
        smFeedback.setTarget (value (pFeedback) * 0.01f);
        smPhase.setTarget (value (pPhase));
    }

    std::array<std::array<float, kMaxStages>, 2> state {};
    std::array<float, 2> feedbackSample {}, coef {};
    mod::ModClock clock;
    Smoother smRate, smDepth, smCenter, smFeedback, smPhase;
};
} // namespace

std::unique_ptr<Effect> makePhaser() { return std::make_unique<PhaserEffect>(); }
} // namespace daw::fx
