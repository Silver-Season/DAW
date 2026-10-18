// The eight-band EQ ("eq8"), docs/spec/06-effects-mixing.md, task 6.2.
//
// Eight bands of RBJ biquads (6 dB slopes are bilinear one-poles, 24 and 48 dB slopes are Butterworth cascades whose
// sharpest stage takes the band's Q as resonance). Frequency, gain and Q glide (20 ms, frequency and Q on a log scale) and
// the coefficients follow them every kStep samples, so automation sweeps without zipper noise. Switching a band on or off
// or to another type or channel would make the filter jump, so each band has two slots: the new configuration starts in
// the free slot and the band crossfades from the old one to it in kFadeMs. A change of mode crossfades every band to off,
// switches the encoding while nothing is in, and crossfades the bands back.
//
// Mode: Stereo runs every band on both channels; Left/Right and Mid/Side let each band choose its channel (b<n>ch: Both,
// Left or Mid, Right or Side). Mid/Side encodes M = (L + R) / 2, S = (L - R) / 2 around the bands.
//
// response() draws the curve from the same coefficient design the audio uses, for the parameters' target values.
#include "Dynamics.h"

#include "daw/fx/BasicEffect.h"

#include <array>
#include <numbers>
#include <string>

namespace daw::fx
{
namespace
{
enum Kind
{
    lowCut6,
    lowCut12,
    lowCut24,
    lowCut48,
    lowShelf,
    bell,
    notch,
    highShelf,
    highCut6,
    highCut12,
    highCut24,
    highCut48
};

enum Field
{
    fOn,
    fType,
    fFreq,
    fGain,
    fQ,
    fChannel,
    kFields
};

enum Mode
{
    stereo,
    leftRight,
    midSide
};

constexpr int kBands = 8;
constexpr int kModeParam = kBands * kFields;
constexpr int kStep = 16;        // samples between coefficient updates while a value glides
constexpr double kFadeMs = 10.0; // fade of a band that switches on, off or to another type
constexpr int kMaxSections = 4;

constexpr int paramOf (int band, Field f) { return band * kFields + (int) f; }

std::vector<ParamInfo> eqParams()
{
    const std::vector<std::string> types { "Low cut 6", "Low cut 12", "Low cut 24", "Low cut 48", "Low shelf", "Bell",
                                           "Notch", "High shelf", "High cut 6", "High cut 12", "High cut 24", "High cut 48" };
    struct Default
    {
        bool on;
        Kind kind;
        float freq;
    };
    const Default defaults[kBands] { { false, lowCut24, 30.0f }, { true, lowShelf, 100.0f }, { true, bell, 250.0f },
                                     { true, bell, 800.0f },     { true, bell, 2500.0f },    { true, bell, 6000.0f },
                                     { true, highShelf, 10000.0f }, { false, highCut24, 18000.0f } };
    std::vector<ParamInfo> ps;
    for (int b = 0; b < kBands; ++b)
    {
        const std::string id = "b" + std::to_string (b + 1), name = "Band " + std::to_string (b + 1);
        const auto& d = defaults[b];
        ps.push_back ({ id + "on", name + " on", "", 0.0f, 1.0f, d.on ? 1.0f : 0.0f, false, dyn::offOn() });
        ps.push_back ({ id + "type", name + " type", "", 0.0f, (float) types.size() - 1.0f, (float) d.kind, false, types });
        ps.push_back ({ id + "freq", name + " frequency", "Hz", 20.0f, 20000.0f, d.freq, true, {} });
        ps.push_back ({ id + "gain", name + " gain", "dB", -24.0f, 24.0f, 0.0f, false, {} });
        ps.push_back ({ id + "q", name + " Q", "", 0.1f, 18.0f, 0.71f, true, {} });
        ps.push_back ({ id + "ch", name + " channel", "", 0.0f, 2.0f, 0.0f, false, { "Both", "Left/Mid", "Right/Side" } });
    }
    ps.push_back ({ "mode", "Mode", "", 0.0f, 2.0f, 0.0f, false, { "Stereo", "Left/Right", "Mid/Side" } });
    return ps;
}

struct Coefs
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    double magnitudeDb (double freq, double sampleRate) const
    {
        const double w = 2.0 * std::numbers::pi * freq / sampleRate;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
        const auto h = (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
        return 20.0 * std::log10 (std::max (1.0e-12, std::abs (h)));
    }
};

struct Design
{
    std::array<Coefs, kMaxSections> sections {};
    int count = 0;
};

Coefs rbj (Biquad::Type t, double freq, double q, double gainDb, double sampleRate)
{
    Biquad b;
    b.set (t, freq, q, gainDb, sampleRate);
    return { b.b0, b.b1, b.b2, b.a1, b.a2 };
}

// Bilinear one-pole (6 dB per octave), the cutoff prewarped so it lands where asked.
Coefs onePole (bool highPass, double freq, double sampleRate)
{
    const double k = std::tan (std::numbers::pi * std::clamp (freq, 1.0, sampleRate * 0.49) / sampleRate);
    const double a1 = (k - 1.0) / (k + 1.0);
    if (highPass)
        return { 1.0 / (1.0 + k), -1.0 / (1.0 + k), 0.0, a1, 0.0 };
    return { k / (1.0 + k), k / (1.0 + k), 0.0, a1, 0.0 };
}

Design design (int kind, double freq, double gainDb, double q, double sampleRate)
{
    // Butterworth stage Qs, 1 / (2 cos ((2k - 1) pi / 2N)), for N = 4 and N = 8; the sharpest stage gets the band's Q
    // relative to Butterworth (0.71 is flat).
    static constexpr double butter4[] { 0.54119610, 1.30656296 };
    static constexpr double butter8[] { 0.50979558, 0.60134489, 0.89997622, 2.56291545 };
    const double resonance = q * std::numbers::sqrt2;
    Design d;
    const auto cascade = [&] (Biquad::Type t, const double* qs, int n)
    {
        for (int i = 0; i < n; ++i)
            d.sections[(size_t) i] = rbj (t, freq, i == n - 1 ? qs[i] * resonance : qs[i], 0.0, sampleRate);
        d.count = n;
    };
    switch (kind)
    {
        case lowCut6: d.sections[0] = onePole (true, freq, sampleRate); d.count = 1; break;
        case lowCut12: d.sections[0] = rbj (Biquad::Type::highPass, freq, q, 0.0, sampleRate); d.count = 1; break;
        case lowCut24: cascade (Biquad::Type::highPass, butter4, 2); break;
        case lowCut48: cascade (Biquad::Type::highPass, butter8, 4); break;
        case lowShelf: d.sections[0] = rbj (Biquad::Type::lowShelf, freq, q, gainDb, sampleRate); d.count = 1; break;
        case bell: d.sections[0] = rbj (Biquad::Type::peak, freq, q, gainDb, sampleRate); d.count = 1; break;
        case notch: d.sections[0] = rbj (Biquad::Type::notch, freq, q, 0.0, sampleRate); d.count = 1; break;
        case highShelf: d.sections[0] = rbj (Biquad::Type::highShelf, freq, q, gainDb, sampleRate); d.count = 1; break;
        case highCut6: d.sections[0] = onePole (false, freq, sampleRate); d.count = 1; break;
        case highCut12: d.sections[0] = rbj (Biquad::Type::lowPass, freq, q, 0.0, sampleRate); d.count = 1; break;
        case highCut24: cascade (Biquad::Type::lowPass, butter4, 2); break;
        case highCut48: cascade (Biquad::Type::lowPass, butter8, 4); break;
        default: break;
    }
    return d;
}

// Shelves and bells at 0 dB pass the signal unchanged: such a band costs nothing.
bool hasGain (int kind) { return kind == lowShelf || kind == bell || kind == highShelf; }

class Eq8Effect final : public BasicEffect
{
public:
    Eq8Effect() : BasicEffect (eqParams()) {}

    std::string_view type() const override { return "eq8"; }

    bool response (const float* frequencies, float* dbOut, int count) const override
    {
        std::fill (dbOut, dbOut + count, 0.0f);
        for (int b = 0; b < kBands; ++b)
        {
            if (value (paramOf (b, fOn)) < 0.5f)
                continue;
            const int kind = (int) value (paramOf (b, fType));
            const float gainDb = value (paramOf (b, fGain));
            if (hasGain (kind) && gainDb == 0.0f)
                continue;
            const auto d = design (kind, value (paramOf (b, fFreq)), gainDb, value (paramOf (b, fQ)), rate);
            for (int i = 0; i < count; ++i)
                for (int s = 0; s < d.count; ++s)
                    dbOut[i] += (float) d.sections[(size_t) s].magnitudeDb (frequencies[i], rate);
        }
        return true;
    }

protected:
    void prepareWet (double sampleRate, int) override
    {
        glideCoef = dyn::timeCoef (20.0, sampleRate);
        stepGlide = dyn::blockCoef (glideCoef, kStep);
        fadeStep = (float) (1.0 / (kFadeMs * 0.001 * sampleRate));
        for (int i = 0; i < kModeParam; ++i)
            paramChanged (i, value (i));
    }

    void resetWet() override
    {
        activeMode = (int) value (kModeParam);
        for (int b = 0; b < kBands; ++b)
        {
            auto& band = bands[(size_t) b];
            band.snap();
            band.current = 0;
            band.slots[0].configure (value (paramOf (b, fOn)) > 0.5f, (int) value (paramOf (b, fType)), requestedLanes (b));
            band.slots[1].configure (false, bell, 3);
            band.fade = 1.0f;
            band.dirty = true;
        }
    }

    void paramChanged (int index, float v) override
    {
        if (index >= kModeParam)
            return;
        auto& band = bands[(size_t) (index / kFields)];
        switch ((Field) (index % kFields))
        {
            case fFreq: band.logFreqTarget = std::log (v); break;
            case fGain: band.gainTarget = v; break;
            case fQ: band.logQTarget = std::log (v); break;
            default: break;
        }
    }

    void processWet (float* const* io, int numChannels, int numSamples, const float* const*) override
    {
        const int requestedMode = (int) value (kModeParam);
        const int available = numChannels > 1 ? 3 : 1;
        for (int start = 0; start < numSamples; start += kStep)
        {
            const int n = std::min (kStep, numSamples - start);
            float* lane[2] { io[0] + start, io[numChannels - 1] + start };

            if (requestedMode != activeMode)
            {
                bool allOut = true;
                for (const auto& band : bands)
                    allOut = allOut && ! band.slots[(size_t) band.current].on && band.fade >= 1.0f;
                if (allOut)
                    activeMode = requestedMode;
            }

            const bool midSideNow = activeMode == midSide && numChannels > 1;
            if (midSideNow)
                for (int i = 0; i < n; ++i)
                {
                    const float l = lane[0][i], r = lane[1][i];
                    lane[0][i] = 0.5f * (l + r);
                    lane[1][i] = 0.5f * (l - r);
                }

            for (int b = 0; b < kBands; ++b)
                processBand (b, lane, n, available, requestedMode);

            if (midSideNow)
                for (int i = 0; i < n; ++i)
                {
                    const float m = lane[0][i], s = lane[1][i];
                    lane[0][i] = m + s;
                    lane[1][i] = m - s;
                }
        }
    }

private:
    // One configuration of a band: on or off, type, channels, with its own coefficients and filter state.
    struct Slot
    {
        bool on = false;
        int kind = bell, lanes = 3;
        Design coefs;
        double state[2][kMaxSections][2] {};

        void configure (bool isOn, int newKind, int newLanes)
        {
            on = isOn;
            kind = newKind;
            lanes = newLanes;
            clear();
        }

        bool matches (bool isOn, int otherKind, int otherLanes) const
        {
            return on == isOn && (! on || (kind == otherKind && lanes == otherLanes));
        }

        void clear()
        {
            for (auto& lane : state)
                for (auto& st : lane)
                    st[0] = st[1] = 0.0;
        }

        // In place on one channel, unless the slot is off, not on that channel or a shelf or bell at 0 dB.
        void run (int c, float* x, int n, bool flat)
        {
            if (! on || (lanes & (1 << c)) == 0)
                return;
            if (flat)
            {
                clear(); // a fresh state is consistent with passing the signal when the gain moves again
                return;
            }
            for (int s = 0; s < coefs.count; ++s)
            {
                const auto& f = coefs.sections[(size_t) s];
                double z1 = state[c][s][0], z2 = state[c][s][1];
                for (int i = 0; i < n; ++i)
                {
                    const double in = x[i];
                    const double y = f.b0 * in + z1;
                    z1 = f.b1 * in - f.a1 * y + z2;
                    z2 = f.b2 * in - f.a2 * y;
                    x[i] = (float) y;
                }
                // Flush the state before it decays into denormals after the sound stopped (slow on x86).
                state[c][s][0] = std::abs (z1) < 1.0e-30 ? 0.0 : z1;
                state[c][s][1] = std::abs (z2) < 1.0e-30 ? 0.0 : z2;
            }
        }
    };

    struct Band
    {
        std::array<Slot, 2> slots;
        int current = 0;    // the slot that plays; the other one is fading out while fade < 1
        float fade = 1.0f;
        float logFreq = 0, gain = 0, logQ = 0; // gliding values
        float logFreqTarget = 0, gainTarget = 0, logQTarget = 0;
        bool dirty = true;  // coefficients must be designed again

        void snap()
        {
            logFreq = logFreqTarget;
            gain = gainTarget;
            logQ = logQTarget;
        }
    };

    int requestedLanes (int b) const
    {
        if ((int) value (kModeParam) == stereo)
            return 3;
        const int ch = (int) value (paramOf (b, fChannel));
        return ch == 1 ? 1 : ch == 2 ? 2 : 3;
    }

    static bool glide (float& current, float target, float k, float epsilon)
    {
        if (current == target)
            return false;
        current += (target - current) * k;
        if (std::abs (target - current) < epsilon)
            current = target;
        return true;
    }

    void processBand (int b, float* const* lane, int n, int available, int requestedMode)
    {
        auto& band = bands[(size_t) b];
        const bool on = value (paramOf (b, fOn)) > 0.5f && requestedMode == activeMode;
        const int kind = (int) value (paramOf (b, fType));
        const int lanes = requestedLanes (b);

        if (band.fade >= 1.0f && ! band.slots[(size_t) band.current].matches (on, kind, lanes))
        {
            if (! band.slots[(size_t) band.current].on)
                band.snap(); // coming in from off: nothing to glide from
            band.current ^= 1;
            band.slots[(size_t) band.current].configure (on, kind, lanes);
            band.fade = 0.0f;
            band.dirty = true;
        }
        auto& now = band.slots[(size_t) band.current];
        auto& before = band.slots[(size_t) band.current ^ 1];
        const bool fading = band.fade < 1.0f;
        if (! now.on && ! fading)
        {
            band.snap();
            return;
        }

        const float k = n == kStep ? stepGlide : dyn::blockCoef (glideCoef, n);
        bool changed = band.dirty;
        changed = glide (band.logFreq, band.logFreqTarget, k, 1.0e-4f) || changed;
        changed = glide (band.gain, band.gainTarget, k, 1.0e-3f) || changed;
        changed = glide (band.logQ, band.logQTarget, k, 1.0e-4f) || changed;
        if (changed)
        {
            const double f = std::exp (band.logFreq), q = std::exp (band.logQ);
            if (now.on)
                now.coefs = design (now.kind, f, band.gain, q, rate);
            if (fading && before.on)
                before.coefs = design (before.kind, f, band.gain, q, rate);
            band.dirty = false;
        }
        const bool flat = band.gain == 0.0f && band.gainTarget == 0.0f;

        const float from = band.fade, to = std::min (1.0f, from + fadeStep * (float) n);
        band.fade = to;
        for (int c = 0; c < 2; ++c)
        {
            if ((available & (1 << c)) == 0)
                continue;
            float* x = lane[c];
            if (! fading)
            {
                now.run (c, x, n, flat && hasGain (now.kind));
                continue;
            }
            float old[kStep];
            std::copy (x, x + n, old);
            now.run (c, x, n, flat && hasGain (now.kind));
            before.run (c, old, n, flat && hasGain (before.kind));
            for (int i = 0; i < n; ++i)
            {
                const float a = from + (to - from) * (float) (i + 1) / (float) n;
                x[i] = old[i] + (x[i] - old[i]) * a;
            }
        }
    }

    std::array<Band, kBands> bands;
    int activeMode = stereo;
    float glideCoef = 1.0f, stepGlide = 1.0f, fadeStep = 1.0f;
};
} // namespace

std::unique_ptr<Effect> makeEq8() { return std::make_unique<Eq8Effect>(); }
} // namespace daw::fx
