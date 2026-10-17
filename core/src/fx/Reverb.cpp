// Reverb ("reverb"): an 8-line feedback delay network (docs/spec/06-effects-mixing.md, task 6.2).
//
// Signal path of the wet side: pre-delay -> low cut -> 4 input all-passes per channel (diffusion) -> FDN. The FDN has 8
// modulated delay lines of mutually prime lengths, mixed by an orthogonal 8x8 Hadamard matrix (lossless, so all decay
// comes from the per-line gains). Each line's gain is set from the RT60 for its length, and a first-order high shelf
// in the loop makes the highs above the damping frequency die faster. The stereo output sums taps inside the lines.
// Freeze sets the loop gain to exactly 1 and mutes the input.
#include "daw/fx/BasicEffect.h"

#include "DelayLine.h"

#include <atomic>

namespace daw::fx
{
namespace
{
enum Param
{
    pSize,
    pDecay,
    pPredelay,
    pPredelaySync,
    pWidth,
    pDamping,
    pLowCut,
    pDiffusion,
    pFreeze,
    pModulation
};

constexpr int kLines = 8;
// Line lengths at size 100 %, spread over 30..100 ms; prepare() turns each into the nearest prime number of samples so
// that no two lines share a common period (the source of metallic ringing).
constexpr double kLineMs[kLines] = { 31.3, 36.7, 41.9, 48.1, 55.3, 64.7, 77.9, 97.1 };
constexpr double kMinScale = 0.3; // size 0 %: the lines are 30 % as long
// Input all-passes per channel, slightly different on the right so that the two sides decorrelate.
constexpr double kDiffuserMs[2][4] = { { 4.77, 3.59, 12.73, 9.31 }, { 5.03, 3.83, 12.11, 9.89 } };
constexpr double kMaxDiffusion = 0.75;
// Slow LFOs, one per line, at unrelated rates: they smear the modes of the network instead of letting them ring.
constexpr double kModHz[kLines] = { 0.11, 0.17, 0.23, 0.31, 0.37, 0.43, 0.53, 0.61 };
constexpr double kMaxModMs = 2.0;
constexpr double kMaxPredelaySeconds = 2.0;
// Above the damping frequency the reverb decays this much faster.
constexpr double kHighDecayRatio = 0.25;
constexpr double kTailCap = 30.0;
constexpr int kControlBlock = 32;

// Pre-delay sync choices in quarter notes (index 0 is Off).
constexpr double kPredelayBeats[] = { 0.0, 1.0 / 16.0, 1.0 / 8.0, 1.0 / 4.0, 1.0 / 2.0 };

// Which side feeds each line and with what sign.
constexpr float kInSign[kLines] = { 1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f };
// Each side's output sums one tap inside every line, at these fractions of the line's length. Reading the line ends
// would let every pass through a line repeat at that line's length (an audible flutter, ~0.35 of autocorrelation);
// taps at unrelated points spread those repeats over 64 different lags.
constexpr double kTapL[kLines] = { 0.37, 0.61, 0.83, 0.29, 0.71, 0.47, 0.89, 0.53 };
constexpr double kTapR[kLines] = { 0.67, 0.31, 0.43, 0.79, 0.23, 0.87, 0.57, 0.41 };
// Output signs: orthogonal to each other (a mono input comes out decorrelated), and with a flat-as-possible Walsh
// spectrum, so that neither lines up with a row of the Hadamard matrix.
constexpr float kOutL[kLines] = { 1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 1.0f, -1.0f };
constexpr float kOutR[kLines] = { 1.0f, 1.0f, 1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f };
constexpr float kInGain = 0.5f, kOutGain = 0.3f;

std::vector<ParamInfo> makeParams()
{
    const std::vector<std::string> offOn { "Off", "On" };
    return {
        { "size", "Size", "%", 0.0f, 100.0f, 60.0f, false, {} },
        { "decay", "Decay", "s", 0.2f, 20.0f, 2.5f, true, {} },
        { "predelay", "Pre-Delay", "ms", 0.0f, 250.0f, 10.0f, false, {} },
        { "predelaySync", "Pre-Delay Sync", "", 0.0f, 4.0f, 0.0f, false, { "Off", "1/64", "1/32", "1/16", "1/8" } },
        { "width", "Width", "%", 0.0f, 100.0f, 100.0f, false, {} },
        { "damping", "Damping", "Hz", 1000.0f, 20000.0f, 8000.0f, true, {} },
        { "lowCut", "Low Cut", "Hz", 20.0f, 1000.0f, 80.0f, true, {} },
        { "diffusion", "Diffusion", "%", 0.0f, 100.0f, 70.0f, false, {} },
        { "freeze", "Freeze", "", 0.0f, 1.0f, 0.0f, false, offOn },
        { "modulation", "Modulation", "%", 0.0f, 100.0f, 20.0f, false, {} },
    };
}

bool isPrime (int n)
{
    if (n < 2)
        return false;
    for (int d = 2; d * d <= n; ++d)
        if (n % d == 0)
            return false;
    return true;
}

int nearestPrime (int n)
{
    for (int step = 0;; ++step)
    {
        if (isPrime (n + step))
            return n + step;
        if (isPrime (n - step))
            return n - step;
    }
}

// In-place fast Walsh-Hadamard transform of 8 values, scaled to be orthonormal.
inline void hadamard8 (float* x)
{
    for (int h = 1; h < kLines; h <<= 1)
        for (int i = 0; i < kLines; i += h << 1)
            for (int j = i; j < i + h; ++j)
            {
                const float a = x[j], b = x[j + h];
                x[j] = a + b;
                x[j + h] = a - b;
            }
    constexpr float norm = 0.35355339059327373f; // 1 / sqrt (8)
    for (int i = 0; i < kLines; ++i)
        x[i] *= norm;
}

// A control value that glides towards its target once per control block, and lands on it exactly.
struct Glide
{
    void snap() { current = target; }
    void advance (double coef)
    {
        if (std::abs (target - current) <= 1.0e-5 * (1.0 + std::abs (target)))
            current = target;
        else
            current += (target - current) * coef;
    }
    double current = 0.0, target = 0.0;
};

class ReverbEffect final : public BasicEffect
{
public:
    ReverbEffect() : BasicEffect (makeParams(), 30.0f) {}

    std::string_view type() const override { return "reverb"; }

    void setTransport (const TransportInfo& t) override
    {
        if (t.bpm > 0.0)
            bpm.store (t.bpm, std::memory_order_relaxed);
    }

    double tailSeconds() const override
    {
        if (value (pFreeze) > 0.5f)
            return kTailCap;
        return std::min (kTailCap, (double) value (pDecay) + predelaySeconds());
    }

protected:
    void prepareWet (double sampleRate, int /*maxBlock*/) override
    {
        const double modMax = kMaxModMs * 0.001 * sampleRate;
        for (int j = 0; j < kLines; ++j)
        {
            baseLength[(size_t) j] = (double) nearestPrime ((int) std::lround (kLineMs[j] * 0.001 * sampleRate));
            lines[(size_t) j].allocate ((int) std::ceil (baseLength[(size_t) j] + modMax) + 4);
            const double w = 2.0 * std::numbers::pi * kModHz[j] / sampleRate;
            lfoRotC[(size_t) j] = (float) std::cos (w);
            lfoRotS[(size_t) j] = (float) std::sin (w);
        }
        for (int c = 0; c < 2; ++c)
        {
            predelay[(size_t) c].allocate ((int) std::ceil (kMaxPredelaySeconds * sampleRate) + 4);
            for (int k = 0; k < 4; ++k)
            {
                const int n = std::max (1, nearestPrime ((int) std::lround (kDiffuserMs[c][k] * 0.001 * sampleRate)));
                diffuserLength[(size_t) c][(size_t) k] = n;
                diffusers[(size_t) c][(size_t) k].allocate (n + 1);
            }
        }
        inputGain.prepare (sampleRate);
        width.prepare (sampleRate);
        fastCoef = 1.0 - std::exp (-(double) kControlBlock / (0.02 * sampleRate));
        slowCoef = 1.0 - std::exp (-(double) kControlBlock / (0.15 * sampleRate));
    }

    void resetWet() override
    {
        for (auto& l : lines)
            l.clear();
        for (auto& p : predelay)
            p.clear();
        for (auto& side : diffusers)
            for (auto& d : side)
                d.clear();
        for (auto& f : lowCut)
            f.reset();
        shelfState.fill (0.0f);
        for (int j = 0; j < kLines; ++j)
        {
            // Spread the LFO phases so that the lines never move together.
            const double phase = 2.0 * std::numbers::pi * j / kLines;
            lfoC[(size_t) j] = (float) std::cos (phase);
            lfoS[(size_t) j] = (float) std::sin (phase);
        }
        // Start from the lengths of the size knob: a reset while frozen holds those (rounded), not stale ones.
        const double scale = sizeScale();
        for (int j = 0; j < kLines; ++j)
            length[(size_t) j].current = baseLength[(size_t) j] * scale;
        setTargets();
        for (auto& l : length)
            l.snap();
        for (auto* g : { &freezeAmount, &modDepth, &predelayTime, &dampingLog, &lowCutLog, &diffusion })
            g->snap();
        inputGain.snap();
        width.snap();
        updateLowCut();
        updateLoopGains();
    }

    void processWet (float* const* io, int numChannels, int numSamples, const float* const* /*sidechain*/) override
    {
        float* outL = io[0];
        float* outR = numChannels > 1 ? io[1] : io[0];
        for (int start = 0; start < numSamples; start += kControlBlock)
        {
            const int end = std::min (numSamples, start + kControlBlock);
            const int n = end - start;

            // Line lengths and the pre-delay ramp linearly inside the control block, so a size change glides.
            std::array<double, kLines> lenStart, lenStep;
            for (int j = 0; j < kLines; ++j)
                lenStart[(size_t) j] = length[(size_t) j].current;
            const double preStart = predelayTime.current;
            updateControls();
            for (int j = 0; j < kLines; ++j)
                lenStep[(size_t) j] = (length[(size_t) j].current - lenStart[(size_t) j]) / n;
            const double preStep = (predelayTime.current - preStart) / n;
            const float depth = (float) modDepth.current;
            const float g = (float) diffusion.current;

            for (int i = start; i < end; ++i)
            {
                const int k = i - start;
                const float xL = outL[i], xR = outR[i];

                const double pre = preStart + preStep * k;
                float side[2] { predelay[0].read (pre), predelay[1].read (pre) };
                predelay[0].push (xL);
                predelay[1].push (xR);

                for (int c = 0; c < 2; ++c)
                {
                    float s = lowCut[(size_t) c].tick (side[c]);
                    for (int a = 0; a < 4; ++a)
                    {
                        auto& ap = diffusers[(size_t) c][(size_t) a];
                        const float delayed = ap.tap (diffuserLength[(size_t) c][(size_t) a]);
                        const float v = s + g * delayed;
                        ap.push (flushTiny (v));
                        s = delayed - g * v;
                    }
                    side[c] = s;
                }

                float o[kLines], f[kLines];
                float wetL = 0.0f, wetR = 0.0f;
                for (int j = 0; j < kLines; ++j)
                {
                    const auto jj = (size_t) j;
                    const double len = lenStart[jj] + lenStep[jj] * k;
                    o[j] = lines[jj].read (len + depth * lfoS[jj]);
                    wetL += kOutL[j] * lines[jj].readLinear (len * kTapL[j]);
                    wetR += kOutR[j] * lines[jj].readLinear (len * kTapR[j]);
                    // First-order high shelf: gainHigh at Nyquist, gainLow at DC, split by a TPT one-pole low-pass.
                    const float v = (o[j] - shelfState[jj]) * shelfCoef;
                    const float low = v + shelfState[jj];
                    shelfState[jj] = flushTiny (low + v);
                    f[j] = gainHigh[jj] * o[j] + (gainLow[jj] - gainHigh[jj]) * low;

                    const float c = lfoC[jj], s = lfoS[jj];
                    lfoC[jj] = c * lfoRotC[jj] - s * lfoRotS[jj];
                    lfoS[jj] = s * lfoRotC[jj] + c * lfoRotS[jj];
                }
                hadamard8 (f);

                const float in = stepExact (inputGain) * kInGain;
                for (int j = 0; j < kLines; ++j)
                    lines[(size_t) j].push (flushTiny (f[j] + in * kInSign[j] * side[j & 1]));

                const float mid = 0.5f * (wetL + wetR) * kOutGain;
                const float sideOut = 0.5f * (wetL - wetR) * kOutGain * stepExact (width);
                if (numChannels > 1)
                {
                    outL[i] = mid + sideOut;
                    outR[i] = mid - sideOut;
                }
                else
                {
                    outL[i] = mid;
                }
            }

            // The rotating LFO phasors drift off the unit circle by rounding; pull them back once per block.
            for (int j = 0; j < kLines; ++j)
            {
                const float r = 1.5f - 0.5f * (lfoC[(size_t) j] * lfoC[(size_t) j] + lfoS[(size_t) j] * lfoS[(size_t) j]);
                lfoC[(size_t) j] *= r;
                lfoS[(size_t) j] *= r;
            }
        }
    }

private:
    double predelaySeconds() const
    {
        const int sync = std::clamp ((int) value (pPredelaySync), 0, 4);
        const double sec = sync > 0 ? kPredelayBeats[sync] * 60.0 / bpm.load (std::memory_order_relaxed) : value (pPredelay) * 0.001;
        return std::clamp (sec, 0.0, kMaxPredelaySeconds);
    }

    double sizeScale() const { return kMinScale + (1.0 - kMinScale) * value (pSize) / 100.0; }

    void setTargets()
    {
        const bool frozen = value (pFreeze) > 0.5f;
        const double scale = sizeScale();
        for (int j = 0; j < kLines; ++j)
        {
            auto& l = length[(size_t) j];
            // Frozen, each line holds a whole number of samples and stops moving: then the interpolator is exact and
            // the loop is lossless, so the sound holds for as long as freeze is on.
            l.target = frozen ? std::round (l.current) : baseLength[(size_t) j] * scale;
        }
        freezeAmount.target = frozen ? 1.0 : 0.0;
        modDepth.target = frozen ? 0.0 : value (pModulation) / 100.0 * kMaxModMs * 0.001 * rate;
        predelayTime.target = predelaySeconds() * rate;
        dampingLog.target = std::log ((double) value (pDamping));
        lowCutLog.target = std::log ((double) value (pLowCut));
        diffusion.target = kMaxDiffusion * value (pDiffusion) / 100.0;
        inputGain.setTarget (frozen ? 0.0f : 1.0f);
        width.setTarget (value (pWidth) / 100.0f);
    }

    void updateControls()
    {
        setTargets();
        for (auto& l : length)
            l.advance (slowCoef);
        predelayTime.advance (slowCoef);
        for (auto* g : { &freezeAmount, &modDepth, &dampingLog, &diffusion })
            g->advance (fastCoef);
        const double lowCutBefore = lowCutLog.current;
        lowCutLog.advance (fastCoef);
        if (lowCutLog.current != lowCutBefore)
            updateLowCut();
        updateLoopGains();
    }

    void updateLowCut()
    {
        lowCut[0].set (Biquad::Type::highPass, std::exp (lowCutLog.current), 0.7071, 0.0, rate);
        lowCut[1].b0 = lowCut[0].b0;
        lowCut[1].b1 = lowCut[0].b1;
        lowCut[1].b2 = lowCut[0].b2;
        lowCut[1].a1 = lowCut[0].a1;
        lowCut[1].a2 = lowCut[0].a2;
    }

    // Each line loses -60 dB per RT60 in proportion to its length, so every path through the network decays at the
    // same rate: g = 10^(-3 * length / RT60).
    void updateLoopGains()
    {
        const double rt60 = std::max (0.05, (double) value (pDecay));
        const double fz = freezeAmount.current;
        for (int j = 0; j < kLines; ++j)
        {
            const double sec = length[(size_t) j].current / rate;
            double low = std::pow (10.0, -3.0 * sec / rt60);
            double high = std::pow (10.0, -3.0 * sec / (rt60 * kHighDecayRatio));
            low += (1.0 - low) * fz;
            high += (1.0 - high) * fz;
            gainLow[(size_t) j] = fz >= 1.0 ? 1.0f : (float) low;
            gainHigh[(size_t) j] = fz >= 1.0 ? 1.0f : (float) high;
        }
        const double fc = std::min (std::exp (dampingLog.current), 0.45 * rate);
        const double gt = std::tan (std::numbers::pi * fc / rate);
        shelfCoef = (float) (gt / (1.0 + gt));
    }

    std::array<DelayLine, kLines> lines;
    std::array<double, kLines> baseLength {};
    std::array<Glide, kLines> length;
    std::array<float, kLines> gainLow {}, gainHigh {}, shelfState {};
    std::array<float, kLines> lfoC {}, lfoS {}, lfoRotC {}, lfoRotS {};
    float shelfCoef = 0.5f;

    std::array<DelayLine, 2> predelay;
    std::array<Biquad, 2> lowCut;
    std::array<std::array<DelayLine, 4>, 2> diffusers;
    std::array<std::array<int, 4>, 2> diffuserLength {};

    Glide freezeAmount, modDepth, predelayTime, dampingLog, lowCutLog, diffusion;
    Smoother inputGain, width;
    double fastCoef = 1.0, slowCoef = 1.0;

    std::atomic<double> bpm { 120.0 };
};
} // namespace

std::unique_ptr<Effect> makeReverb() { return std::make_unique<ReverbEffect>(); }
} // namespace daw::fx
