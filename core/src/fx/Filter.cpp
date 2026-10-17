// Filter ("filter"): the synth's state-variable filter plus a ladder, with an LFO and an envelope follower on the cutoff
// (docs/spec/06-effects-mixing.md, task 6.2).
#include "daw/dsp/Filter.h"
#include "daw/fx/BasicEffect.h"

#include <cstdint>

namespace daw::fx
{
namespace
{
enum FilterKind
{
    kindLp12,
    kindLp24,
    kindHp12,
    kindHp24,
    kindBp12,
    kindNotch,
    kindLadder
};

enum LfoShape
{
    shapeSine,
    shapeTriangle,
    shapeSaw,
    shapeSquare,
    shapeSampleAndHold
};

// Length of one LFO cycle in quarter notes for each choice of lfoDiv: 1/16 ... 4/1.
constexpr double kLog2Min = 4.321928094887362; // log2 (20 Hz)
constexpr double kDivisionBeats[] { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };

// tanh as a Pade approximation that reaches +-1 exactly at +-3 with zero slope: smooth, monotonic, within 2 % of the real
// thing, and several times cheaper (the filter runs it up to four times per sample).
inline double softClip (double x)
{
    const double c = std::clamp (x, -3.0, 3.0);
    const double c2 = c * c;
    return c * (27.0 + c2) / (27.0 + 9.0 * c2);
}

// sin (2 pi p) for p in [0, 1) from a ninth-order series on the folded argument (error below 1e-6): an LFO has no use for
// the last digits of std::sin.
inline double lfoSine (double p)
{
    double t = p < 0.5 ? p : p - 1.0;             // -0.5 .. 0.5
    t = t > 0.25 ? 0.5 - t : (t < -0.25 ? -0.5 - t : t); // sin (2 pi t) is symmetric about the quarter cycles
    const double u = 2.0 * std::numbers::pi * t, u2 = u * u;
    return u * (1.0 + u2 * (-1.0 / 6.0 + u2 * (1.0 / 120.0 + u2 * (-1.0 / 5040.0 + u2 * (1.0 / 362880.0)))));
}

// A uniform number in [-1, 1) that depends only on `key`: the sample-and-hold value of a synced cycle, which has to be the
// same whenever the song is at the same place.
double hashedValue (int64_t key)
{
    uint64_t z = (uint64_t) key + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return (double) (z >> 11) * (2.0 / 9007199254740992.0) - 1.0;
}

// Four one-pole stages in a feedback loop (Moog style), zero-delay: the feedback is solved for in closed form from the
// stage states, and the loop's tanh is applied to the resulting input of the first stage.
struct Ladder
{
    void reset() { s[0] = s[1] = s[2] = s[3] = 0.0; }

    // G = g / (1 + g) for the warped cutoff g; k in [0, 4) is the feedback gain.
    float process (float in, double G, double k)
    {
        const double G2 = G * G, G4 = G2 * G2;
        const double sigma = (1.0 - G) * (G2 * G * s[0] + G2 * s[1] + G * s[2] + s[3]);
        const double y4 = (G4 * in + sigma) / (1.0 + k * G4);
        const double u = softClip (in - k * y4);
        double x = u;
        for (double& state : s)
        {
            const double y = G * x + (1.0 - G) * state;
            state = 2.0 * y - state;
            x = y;
        }
        return (float) x;
    }

    double s[4] {};
};

class FilterEffect final : public BasicEffect
{
public:
    enum Param { pType, pFreq, pResonance, pDrive, pLfoShape, pLfoSync, pLfoRate, pLfoDiv, pLfoDepth, pEnvDepth, pEnvAttack, pEnvRelease };
    // readout() indices, for the device's display and for the tests.
    enum Readout { readCutoff, readLfo, readEnvelope };

    FilterEffect()
        : BasicEffect ({ { "type", "Type", "", 0.0f, 6.0f, 0.0f, false, { "LP 12", "LP 24", "HP 12", "HP 24", "BP 12", "Notch", "Ladder" } },
                         { "freq", "Frequency", "Hz", 20.0f, 20000.0f, 1000.0f, true, {} },
                         { "resonance", "Resonance", "%", 0.0f, 100.0f, 20.0f, false, {} },
                         { "drive", "Drive", "dB", 0.0f, 24.0f, 0.0f, false, {} },
                         { "lfoShape", "LFO Shape", "", 0.0f, 4.0f, 0.0f, false, { "Sine", "Triangle", "Saw", "Square", "S&H" } },
                         { "lfoSync", "LFO Sync", "", 0.0f, 1.0f, 0.0f, false, { "Off", "On" } },
                         { "lfoRate", "LFO Rate", "Hz", 0.01f, 20.0f, 1.0f, true, {} },
                         { "lfoDiv", "LFO Division", "", 0.0f, 6.0f, 2.0f, false, { "1/16", "1/8", "1/4", "1/2", "1/1", "2/1", "4/1" } },
                         { "lfoDepth", "LFO Depth", "%", 0.0f, 100.0f, 0.0f, false, {} },
                         { "envDepth", "Envelope Depth", "%", -100.0f, 100.0f, 0.0f, false, {} },
                         { "envAttack", "Envelope Attack", "ms", 0.1f, 100.0f, 5.0f, true, {} },
                         { "envRelease", "Envelope Release", "ms", 5.0f, 1000.0f, 100.0f, true, {} } })
    {
        refreshTargets();
        snapSmoothers();
    }

    std::string_view type() const override { return "filter"; }

    double tailSeconds() const override
    {
        // The ringing of a resonant filter: -80 dB takes about 9.2 time constants of 2 Q / (2 pi f).
        const double r = (double) value (pResonance) * 0.01;
        const double q = value (pType) == (float) kindLadder ? 1.0 / std::max (0.02, 1.0 - 0.25 * 3.95 * r) : 1.0 / (2.0 - 1.96 * r);
        return std::clamp (9.2 * q / (std::numbers::pi * std::max (20.0, (double) value (pFreq))), 0.05, 8.0);
    }

    void setTransport (const TransportInfo& t) override
    {
        transport = t;
        transportFresh = true;
    }

    float readout (int index) const override
    {
        switch (index)
        {
            case readCutoff: return lastCutoff;
            case readLfo: return lastLfo;
            case readEnvelope: return lastEnvelope;
            default: return 0.0f;
        }
    }

protected:
    void prepareWet (double sampleRate, int) override
    {
        for (auto* s : { &freqSmooth, &resSmooth, &driveSmooth, &driveGainSmooth, &lfoDepthSmooth, &envDepthSmooth })
            s->prepare (sampleRate);
        modSmooth.prepare (sampleRate, 1.5); // fast enough to follow an LFO, slow enough to take the corners off a square
        updateEnvelopeCoefficients();
        refreshTargets();
    }

    void resetWet() override
    {
        for (int c = 0; c < 2; ++c)
        {
            svf[c].reset();
            ladder[c].reset();
        }
        lfoPhase = 0.0;
        ppqPosition = 0.0;
        heldRandom = 0.0;
        randomState = 0x2545F491u;
        envelope = 0.0;
        modSmooth.current = 0.0f;
        modSmooth.target = 0.0f;
        lastLfo = 0.0f;
        lastEnvelope = 0.0f;
        coefficientsValid = false;
        refreshTargets();
        snapSmoothers();
    }

    void paramChanged (int index, float) override
    {
        refreshTargets();
        if (index == pEnvAttack || index == pEnvRelease)
            updateEnvelopeCoefficients();
    }

    void processWet (float* const* io, int numChannels, int numSamples, const float* const*) override
    {
        const int kind = (int) value (pType);
        const int shape = (int) value (pLfoShape);
        const bool sync = value (pLfoSync) >= 0.5f;
        const double cycleBeats = kDivisionBeats[std::clamp ((int) value (pLfoDiv), 0, 6)];
        const double ppqPerSample = transport.bpm / (60.0 * rate);
        const double freeIncrement = (double) value (pLfoRate) / rate;
        const double logMax = std::log2 (0.45 * rate);

        // A synced LFO takes its phase from the song position, so that it is the same at every start. The position is used
        // once per setTransport(); the samples after that count on from it.
        if (transportFresh)
        {
            if (transport.playing)
                ppqPosition = transport.ppq;
            transportFresh = false;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            const double lfo = nextLfo (shape, sync, cycleBeats, ppqPerSample, freeIncrement);

            // The envelope follower listens to the loudest channel of the input.
            double level = std::abs ((double) io[0][i]);
            if (numChannels > 1)
                level = std::max (level, (double) std::abs (io[1][i]));
            if (! (level < 1.0e6)) // NaN or Inf must not get into the follower: it would never come out again
                level = 0.0;
            envelope += (level - envelope) * (level > envelope ? envAttackCoef : envReleaseCoef);

            const float envDepth = envDepthSmooth.next();
            const float lfoDepth = lfoDepthSmooth.next();
            double env = 0.0;
            if (envDepth != 0.0f || envDepthSmooth.smoothing())
                env = std::clamp ((20.0 * std::log10 (envelope + 1.0e-9) + 48.0) / 48.0, 0.0, 1.0); // 48 dB range
            modSmooth.setTarget ((float) (4.0 * ((double) lfoDepth * 0.01 * lfo + (double) envDepth * 0.01 * env)));
            const double mod = modSmooth.next();

            const double logFreq = (double) freqSmooth.next();
            const double logCutoff = std::clamp (logFreq + mod, kLog2Min, logMax);
            const double cutoff = std::exp2 (logCutoff);
            const float resonance = resSmooth.next();
            const float driveAmount = driveSmooth.next(), gain = driveGainSmooth.next();
            const float driveScale = 1.0f / std::sqrt (gain);
            lastCutoff = (float) cutoff;
            lastLfo = (float) lfo;
            lastEnvelope = (float) env;

            updateCoefficients (kind, cutoff, logCutoff, resonance);

            for (int c = 0; c < numChannels; ++c)
            {
                float x = io[c][i];
                if (driveAmount > 0.0f)
                    x = saturate (x, gain, driveAmount, driveScale);
                if (kind == kindLadder)
                    io[c][i] = ladder[c].process (x, ladderG, ladderK) * ladderMakeup;
                else
                    io[c][i] = svf[c].process (x);
            }
        }
    }

private:
    // The LFO at the current sample, in [-1, 1]; advances the phase.
    double nextLfo (int shape, bool sync, double cycleBeats, double ppqPerSample, double freeIncrement)
    {
        double phase;
        int64_t cycle = 0;
        if (sync)
        {
            const double position = ppqPosition / cycleBeats;
            const double whole = std::floor (position);
            phase = position - whole;
            cycle = (int64_t) whole;
            ppqPosition += ppqPerSample;
        }
        else
        {
            phase = lfoPhase;
            lfoPhase += freeIncrement;
            if (lfoPhase >= 1.0)
            {
                lfoPhase -= std::floor (lfoPhase);
                heldRandom = nextRandom();
            }
        }
        switch (shape)
        {
            case shapeSine: return lfoSine (phase);
            case shapeTriangle: return 1.0 - 4.0 * std::abs (phase - 0.5);
            case shapeSaw: return 2.0 * phase - 1.0;
            case shapeSquare: return phase < 0.5 ? 1.0 : -1.0;
            default: return sync ? hashedValue (cycle) : heldRandom;
        }
    }

    double nextRandom()
    {
        randomState ^= randomState << 13;
        randomState ^= randomState >> 17;
        randomState ^= randomState << 5;
        return (double) (randomState >> 8) * (2.0 / 16777216.0) - 1.0;
    }

    // Soft saturation ahead of the filter. Slope sqrt (gain) at the origin and a ceiling of 1 / sqrt (gain), so that a
    // signal around -12 dBFS comes out at about the level it went in at whatever the drive; `amount` blends it in over the
    // first few dB, which makes 0 dB exactly transparent.
    static float saturate (float x, float gain, float amount, float scale)
    {
        const float shaped = (float) softClip ((double) gain * x) * scale;
        return x + amount * (shaped - x);
    }

    void updateCoefficients (int kind, double cutoff, double logCutoff, float resonance)
    {
        const bool unchanged = coefficientsValid && kind == lastKind && std::abs (logCutoff - lastLogCutoff) < 1.0e-4
                               && std::abs (resonance - lastResonance) < 1.0e-5f;
        if (unchanged)
            return;
        // While the cutoff moves, every second sample is enough (it never moves far in 20 microseconds); the counter runs
        // over the whole stream, so the result does not depend on the block size.
        if (coefficientsValid && kind == lastKind && (++updateTick & 1) != 0)
            return;
        coefficientsValid = true;
        lastKind = kind;
        lastLogCutoff = logCutoff;
        lastResonance = resonance;
        if (kind == kindLadder)
        {
            const double g = std::tan (std::numbers::pi * cutoff / rate);
            ladderG = g / (1.0 + g);
            ladderK = (double) resonance * 3.95;
            ladderMakeup = (float) (1.0 + 0.7 * ladderK);
            return;
        }
        static constexpr dsp::FilterType types[] { dsp::FilterType::LowPass12, dsp::FilterType::LowPass24, dsp::FilterType::HighPass12,
                                                   dsp::FilterType::HighPass24, dsp::FilterType::BandPass12, dsp::FilterType::Notch };
        for (auto& f : svf)
            f.set (types[std::clamp (kind, 0, 5)], (float) cutoff, resonance, 0.0f, (float) rate);
    }

    void updateEnvelopeCoefficients()
    {
        envAttackCoef = 1.0 - std::exp (-1.0 / (std::max (0.01, (double) value (pEnvAttack)) * 0.001 * rate));
        envReleaseCoef = 1.0 - std::exp (-1.0 / (std::max (0.01, (double) value (pEnvRelease)) * 0.001 * rate));
    }

    void refreshTargets()
    {
        freqSmooth.setTarget ((float) std::log2 ((double) value (pFreq)));
        resSmooth.setTarget (value (pResonance) * 0.01f);
        driveSmooth.setTarget (std::min (1.0f, value (pDrive) / 3.0f));
        driveGainSmooth.setTarget (dbToGain (value (pDrive)));
        lfoDepthSmooth.setTarget (value (pLfoDepth));
        envDepthSmooth.setTarget (value (pEnvDepth));
    }

    void snapSmoothers()
    {
        for (auto* s : { &freqSmooth, &resSmooth, &driveSmooth, &driveGainSmooth, &lfoDepthSmooth, &envDepthSmooth })
            s->snap();
    }

    dsp::SynthFilter svf[2];
    Ladder ladder[2];
    double ladderG = 0.1, ladderK = 0.0;
    float ladderMakeup = 1.0f;

    Smoother freqSmooth, resSmooth, driveSmooth, driveGainSmooth, lfoDepthSmooth, envDepthSmooth, modSmooth;
    TransportInfo transport;
    bool transportFresh = false;

    double lfoPhase = 0.0, ppqPosition = 0.0, heldRandom = 0.0;
    uint32_t randomState = 0x2545F491u;
    double envelope = 0.0, envAttackCoef = 0.01, envReleaseCoef = 0.001;

    bool coefficientsValid = false;
    unsigned updateTick = 0;
    int lastKind = -1;
    double lastLogCutoff = 0.0;
    float lastResonance = 0.0f;
    float lastCutoff = 1000.0f, lastLfo = 0.0f, lastEnvelope = 0.0f;
};
} // namespace

std::unique_ptr<Effect> makeFilter() { return std::make_unique<FilterEffect>(); }
} // namespace daw::fx
