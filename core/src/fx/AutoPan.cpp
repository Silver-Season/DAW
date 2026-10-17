// Auto pan and tremolo ("autopan", docs/spec/06-effects-mixing.md, task 6.2): one LFO per channel moves the gain of
// that channel. `phase` is the offset between the two channels' LFOs: 0 degrees makes both gains identical (a tremolo),
// 180 degrees makes them opposite (a pan). With sync the LFO phase comes from the position of the song, so the sweep
// lines up at every start.
#include "ModDelay.h"

#include "daw/fx/BasicEffect.h"

#include <cstdint>

namespace daw::fx
{
namespace
{
class AutoPanEffect final : public BasicEffect
{
public:
    enum Param
    {
        pSync,
        pRate,
        pDiv,
        pDepth,
        pShape,
        pPhase,
        pOffset
    };
    enum Shape
    {
        sine,
        triangle,
        saw,
        square,
        sampleHold
    };

    AutoPanEffect()
        : BasicEffect ({ { "sync", "Sync", "", 0.0f, 1.0f, 1.0f, false, { "Off", "On" } },
                         { "rate", "Rate", "Hz", 0.05f, 20.0f, 2.0f, true, {} },
                         { "div", "Division", "", 0.0f, 6.0f, 2.0f, false, { "1/32", "1/16", "1/8", "1/4", "1/2", "1/1", "2/1" } },
                         { "depth", "Depth", "%", 0.0f, 100.0f, 100.0f, false, {} },
                         { "shape", "Shape", "", 0.0f, 4.0f, 0.0f, false, { "Sine", "Triangle", "Saw", "Square", "S&H" } },
                         { "phase", "Phase", "deg", 0.0f, 180.0f, 180.0f, false, {} },
                         { "offset", "Offset", "deg", 0.0f, 360.0f, 0.0f, false, {} } },
                       100.0f)
    {
    }

    std::string_view type() const override { return "autopan"; }

    void setTransport (const TransportInfo& t) override { clock.setTransport (t); }

protected:
    void prepareWet (double sampleRate, int) override
    {
        clock.prepare (sampleRate);
        for (auto* s : { &smRate, &smDepth, &smPhase })
            s->prepare (sampleRate);
        // 1.5 ms: long enough to take the click out of an edge, short enough to leave it a clearly audible step.
        edgeCoef = (float) (1.0 - std::exp (-1.0 / (0.0015 * sampleRate)));
    }

    void resetWet() override
    {
        clock.reset();
        readTargets();
        for (auto* s : { &smRate, &smDepth, &smPhase })
            s->snap();
        snapEdges = true;
    }

    void processWet (float* const* io, int numChannels, int numSamples, const float* const*) override
    {
        readTargets();
        const int channels = std::min (numChannels, 2);
        const bool sync = value (pSync) > 0.5f;
        const double period = mod::kFastDivisions[(size_t) std::clamp ((int) value (pDiv), 0, 6)];
        const auto shape = (Shape) std::clamp ((int) value (pShape), 0, 4);
        const double offsetCycles = value (pOffset) / 360.0;
        if (clock.begin (sync, period))
            snapEdges = true; // after a locate the smoothing must not glide in from the old position
        const double syncStep = clock.syncIncrement (period);
        const bool edgy = shape == saw || shape == square || shape == sampleHold;

        float offCos = 1.0f, offSin = 0.0f, offCycles = 0.0f, depthNorm = 1.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float hz = smRate.next(), depth = smDepth.next(), phaseDeg = smPhase.next();
            if ((i & mod::kControlMask) == 0)
            {
                const float o = phaseDeg * (std::numbers::pi_v<float> / 180.0f);
                offCos = std::cos (o);
                offSin = std::sin (o);
                offCycles = phaseDeg / 360.0f;
                // Scales the gains so that the loudest moment of each channel is always 0 dB: depth 0 is transparent,
                // and at depth 100 a hard pan has 0 dB on its side and a tremolo never rises above the input.
                depthNorm = 1.0f / std::cos (std::numbers::pi_v<float> * 0.25f * (1.0f - depth));
            }

            // Left LFO, then the right one `phase` degrees later; for S&H the right channel gets the same random
            // value scaled by cos (phase), which is the same (0) or the opposite (180) of the left one.
            const double cyc = clock.cycles + offsetCycles;
            const float p = (float) (cyc - std::floor (cyc));
            float left = 0.0f, right = 0.0f;
            switch (shape)
            {
                case sine:
                {
                    const float a = std::numbers::pi_v<float> * 2.0f * p;
                    const float sn = std::sin (a);
                    left = sn;
                    right = sn * offCos + std::cos (a) * offSin;
                    break;
                }
                case sampleHold:
                    left = heldValue ((long long) std::floor (cyc));
                    right = left * offCos;
                    break;
                default:
                    left = wave (shape, p);
                    right = wave (shape, std::fmod (p + offCycles, 1.0f));
                    break;
            }

            if (edgy)
            {
                if (snapEdges)
                {
                    edgeLeft = left;
                    edgeRight = right;
                    snapEdges = false;
                }
                edgeLeft += (left - edgeLeft) * edgeCoef;
                edgeRight += (right - edgeRight) * edgeCoef;
                left = edgeLeft;
                right = edgeRight;
            }
            else
                snapEdges = false;

            clock.advance (sync ? syncStep : (double) hz / rate);

            const float k = std::numbers::pi_v<float> * 0.25f * depth;
            io[0][i] *= std::cos (std::numbers::pi_v<float> * 0.25f + k * left) * depthNorm;
            if (channels > 1)
                io[1][i] *= std::cos (std::numbers::pi_v<float> * 0.25f + k * right) * depthNorm;
        }
    }

private:
    // Triangle and saw start at 0 and rise, like the sine, so that all shapes cross the middle at phase 0.
    static float wave (Shape shape, float p)
    {
        switch (shape)
        {
            case triangle: return p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f);
            case saw: return 2.0f * (p < 0.5f ? p + 0.5f : p - 0.5f) - 1.0f;
            case square: return p < 0.5f ? 1.0f : -1.0f;
            default: return 0.0f;
        }
    }

    // A value in -1..1 that depends only on the number of the cycle, so that the same song position always gives the
    // same step (splitmix64).
    static float heldValue (long long cycle)
    {
        std::uint64_t z = (std::uint64_t) cycle * 0x9E3779B97F4A7C15ull + 0x2545F4914F6CDD1Dull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return (float) (z >> 40) * (2.0f / 16777216.0f) - 1.0f;
    }

    void readTargets()
    {
        smRate.setTarget (value (pRate));
        smDepth.setTarget (value (pDepth) * 0.01f);
        smPhase.setTarget (value (pPhase));
    }

    mod::ModClock clock;
    Smoother smRate, smDepth, smPhase;
    float edgeCoef = 0.01f, edgeLeft = 0.0f, edgeRight = 0.0f;
    bool snapEdges = true;
};
} // namespace

std::unique_ptr<Effect> makeAutoPan() { return std::make_unique<AutoPanEffect>(); }
} // namespace daw::fx
