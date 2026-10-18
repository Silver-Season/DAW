// Chorus ("chorus", docs/spec/06-effects-mixing.md, task 6.2): up to four modulated taps of one delay line per channel,
// each tap with its own LFO phase. The two channels differ by a phase offset of the LFO (Width), so a mono source
// comes out wide.
#include "ModDelay.h"

#include "daw/fx/BasicEffect.h"

namespace daw::fx
{
namespace
{
class ChorusEffect final : public BasicEffect
{
public:
    enum Param
    {
        pVoices,
        pRate,
        pDepth,
        pDelay,
        pWidth,
        pFeedback
    };

    ChorusEffect()
        : BasicEffect ({ { "voices", "Voices", "", 0.0f, 3.0f, 1.0f, false, { "1", "2", "3", "4" } },
                         { "rate", "Rate", "Hz", 0.05f, 5.0f, 0.8f, true, {} },
                         { "depth", "Depth", "%", 0.0f, 100.0f, 50.0f, false, {} },
                         { "delay", "Delay", "ms", 2.0f, 30.0f, 12.0f, false, {} },
                         { "width", "Width", "%", 0.0f, 100.0f, 100.0f, false, {} },
                         { "feedback", "Feedback", "%", 0.0f, 90.0f, 0.0f, false, {} } },
                       50.0f)
    {
        setVoices (2);
    }

    std::string_view type() const override { return "chorus"; }

    double tailSeconds() const override
    {
        const double fb = value (pFeedback) * 0.01;
        const double loops = fb > 0.001 ? std::min (200.0, std::log (1.0e-5) / std::log (fb)) : 0.0;
        return (1.0 + loops) * (maxDelayMs (value (pDelay)) * 0.001) + 0.05;
    }

protected:
    void prepareWet (double sampleRate, int) override
    {
        const int maxSamples = (int) std::ceil (maxDelayMs (30.0f) * 0.001 * sampleRate) + 4;
        for (auto& line : lines)
            line.allocate (maxSamples);
        for (auto* s : { &smRate, &smDepth, &smDelay, &smWidth, &smFeedback })
            s->prepare (sampleRate);
    }

    void resetWet() override
    {
        for (auto& line : lines)
            line.clear();
        clock.reset();
        readTargets();
        for (auto* s : { &smRate, &smDepth, &smDelay, &smWidth, &smFeedback })
            s->snap();
    }

    void processWet (float* const* io, int numChannels, int numSamples, const float* const*) override
    {
        readTargets();
        const int channels = std::min (numChannels, 2);
        const int n = voices;
        const float invN = 1.0f / (float) n;
        // The taps of a chorus are uncorrelated, so their sum is scaled by 1/sqrt (N) to keep the level; the feedback
        // takes the plain average so that the loop gain stays below 1 even when all taps coincide.
        const float wetGain = std::sqrt ((float) n);
        const float samplesPerMs = (float) (rate * 0.001);
        float widthCos = 1.0f, widthSin = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float hz = smRate.next(), depth = smDepth.next(), delayMs = smDelay.next(), width = smWidth.next(),
                        fb = smFeedback.next();
            if ((i & mod::kControlMask) == 0)
            {
                // Channel 1 runs behind channel 0 by up to half the spacing of the taps. A whole spacing (or any multiple
                // of it) would put the right taps exactly where the left ones are and give no width at all.
                const float w = std::numbers::pi_v<float> * width * invN;
                widthCos = std::cos (w);
                widthSin = std::sin (w);
            }
            clock.advance ((double) hz / rate);
            const float a = (float) (mod::kTwoPi * clock.phase());
            const float sn = std::sin (a), cs = std::cos (a);

            const float base = delayMs * samplesPerMs;
            // The sweep is limited in milliseconds, not in percent: a long delay swept by half its length would bend
            // the pitch far more than a chorus should.
            const float excursion = depth * std::min (0.5f * delayMs, 8.0f) * samplesPerMs;

            for (int c = 0; c < channels; ++c)
            {
                // sin (a + k) from sin a and cos a: one sin/cos per sample serves every tap of both channels.
                const float s0 = c == 0 ? sn : sn * widthCos + cs * widthSin;
                const float c0 = c == 0 ? cs : cs * widthCos - sn * widthSin;
                auto& line = lines[(size_t) c];
                float acc = 0.0f;
                for (int v = 0; v < n; ++v)
                    acc += line.read (base + excursion * (s0 * voiceCos[(size_t) v] + c0 * voiceSin[(size_t) v]));
                const float average = acc * invN;
                line.write (mod::flush (io[c][i] + fb * average));
                io[c][i] = average * wetGain;
            }
        }
    }

private:
    static double maxDelayMs (float delayMs) { return delayMs + std::min (0.5 * delayMs, 8.0); }

    void setVoices (int count)
    {
        voices = std::clamp (count, 1, 4);
        for (int v = 0; v < 4; ++v)
        {
            const double a = mod::kTwoPi * v / voices;
            voiceCos[(size_t) v] = (float) std::cos (a);
            voiceSin[(size_t) v] = (float) std::sin (a);
        }
    }

    void readTargets()
    {
        const int wanted = 1 + (int) value (pVoices);
        if (wanted != voices)
            setVoices (wanted);
        smRate.setTarget (value (pRate));
        smDepth.setTarget (value (pDepth) * 0.01f);
        smDelay.setTarget (value (pDelay));
        smWidth.setTarget (value (pWidth) * 0.01f);
        smFeedback.setTarget (value (pFeedback) * 0.01f);
    }

    std::array<mod::DelayLine, 2> lines;
    mod::ModClock clock;
    Smoother smRate, smDepth, smDelay, smWidth, smFeedback;
    int voices = 2;
    std::array<float, 4> voiceCos {}, voiceSin {};
};
} // namespace

std::unique_ptr<Effect> makeChorus() { return std::make_unique<ChorusEffect>(); }
} // namespace daw::fx
