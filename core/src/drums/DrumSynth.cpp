#include "daw/drums/DrumSynth.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>

namespace daw::drums
{

namespace
{
constexpr double kTwoPi = 2.0 * std::numbers::pi;
constexpr double kLn1000 = 6.907755278982137; // exp(-kLn1000 * t / t60) is -60 dB at t60

// ---------------------------------------------------------------------------------------------------------------
// Building blocks (double precision: this runs offline and the result is stored as float)
// ---------------------------------------------------------------------------------------------------------------

// xoshiro128** seeded through splitmix32, as the drumgen spec asks: small, fast and identical on every platform.
class Rng
{
public:
    explicit Rng (uint32_t seed)
    {
        uint32_t z = seed;
        for (auto& s : state)
        {
            z += 0x9e3779b9u;
            uint32_t x = z;
            x = (x ^ (x >> 16)) * 0x85ebca6bu;
            x = (x ^ (x >> 13)) * 0xc2b2ae35u;
            s = x ^ (x >> 16);
        }
    }

    uint32_t next() noexcept
    {
        const uint32_t result = rotl (state[1] * 5u, 7) * 9u;
        const uint32_t t = state[1] << 9;
        state[2] ^= state[0];
        state[3] ^= state[1];
        state[1] ^= state[2];
        state[0] ^= state[3];
        state[2] ^= t;
        state[3] = rotl (state[3], 11);
        return result;
    }

    // Uniform in [-1, 1).
    double white() noexcept { return (double) (next() >> 8) / 8388608.0 - 1.0; }
    double unit() noexcept { return (double) (next() >> 8) / 16777216.0; }

private:
    static uint32_t rotl (uint32_t x, int k) noexcept { return (x << k) | (x >> (32 - k)); }
    uint32_t state[4] {};
};

// TPT state variable filter (same topology as dsp::SVF, in double and parameterised by Q).
class Svf
{
public:
    Svf() = default;
    Svf (double cutoffHz, double q, double sampleRate) { set (cutoffHz, q, sampleRate); }

    void set (double cutoffHz, double q, double sampleRate) noexcept
    {
        const double fc = std::clamp (cutoffHz, 10.0, sampleRate * 0.49);
        g = std::tan (std::numbers::pi * fc / sampleRate);
        k = 1.0 / std::max (0.05, q);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    double lp (double x) noexcept
    {
        tick (x);
        return low;
    }
    double hp (double x) noexcept
    {
        tick (x);
        return high;
    }
    double bp (double x) noexcept // unity gain at the centre
    {
        tick (x);
        return band * k;
    }

private:
    void tick (double v0) noexcept
    {
        const double v3 = v0 - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        low = v2;
        band = v1;
        high = v0 - k * v1 - v2;
    }

    double g = 0.0, k = 1.4142, a1 = 0.0, a2 = 0.0, a3 = 0.0;
    double ic1 = 0.0, ic2 = 0.0;
    double low = 0.0, band = 0.0, high = 0.0;
};

inline double polyBlep (double t, double dt) noexcept
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

// Band-limited square (polyBLEP). Naive squares at 800 Hz alias audibly into a hat's band; this keeps the metal
// clean so the band-pass shapes it instead of fighting fold-over.
class Square
{
public:
    void set (double hz, double sampleRate, double startPhase) noexcept
    {
        dt = std::min (0.45, hz / sampleRate);
        phase = startPhase - std::floor (startPhase);
    }

    double next() noexcept
    {
        double v = phase < 0.5 ? 1.0 : -1.0;
        v += polyBlep (phase, dt);
        double t2 = phase + 0.5;
        if (t2 >= 1.0)
            t2 -= 1.0;
        v -= polyBlep (t2, dt);
        phase += dt;
        if (phase >= 1.0)
            phase -= 1.0;
        return v;
    }

private:
    double phase = 0.0, dt = 0.01;
};

// The six 808 cymbal/hat oscillators (Hz at multiplier 1).
constexpr double kMetalRatios[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };

class Metal
{
public:
    void set (double multiplier, double sampleRate, Rng& rng) noexcept
    {
        for (int i = 0; i < 6; ++i)
            osc[i].set (kMetalRatios[i] * multiplier, sampleRate, rng.unit());
    }

    double next() noexcept
    {
        double s = 0.0;
        for (auto& o : osc)
            s += o.next();
        return s * (1.0 / 6.0);
    }

private:
    Square osc[6];
};

inline double decayEnv (double t, double t60) noexcept { return std::exp (-kLn1000 * t / std::max (1.0e-4, t60)); }
inline double ramp (double t, double seconds) noexcept { return t >= seconds ? 1.0 : t / seconds; }

// Exponential pitch glide f(t) = end + (start - end) * e^(-t / tau), integrated into a phase.
class Sweep
{
public:
    Sweep (double startHz, double endHz, double tauSec, double sampleRate)
        : fStart (startHz), fEnd (endHz), tau (tauSec), sr (sampleRate)
    {
    }

    double next (double t) noexcept
    {
        const double v = std::sin (phase);
        phase += kTwoPi * (fEnd + (fStart - fEnd) * std::exp (-t / tau)) / sr;
        if (phase > kTwoPi)
            phase -= kTwoPi;
        return v;
    }

private:
    double fStart, fEnd, tau, sr;
    double phase = 0.0;
};

struct Buffer
{
    Buffer (double rate, double seconds, bool isStereo)
        : sr (rate), stereo (isStereo), n (std::max (16, (int) std::lround (seconds * rate))), l ((size_t) n, 0.0)
    {
        if (stereo)
            r.assign ((size_t) n, 0.0);
    }

    double time (int i) const noexcept { return (double) i / sr; }

    double sr;
    bool stereo;
    int n;
    std::vector<double> l, r;
};

double peakOf (const Buffer& b)
{
    double p = 0.0;
    for (double v : b.l)
        p = std::max (p, std::abs (v));
    for (double v : b.r)
        p = std::max (p, std::abs (v));
    return p;
}

void scale (Buffer& b, double gain)
{
    for (double& v : b.l)
        v *= gain;
    for (double& v : b.r)
        v *= gain;
}

// tanh saturation at a peak-normalised level, so `amount` means the same thing whatever the recipe's mix level.
void saturate (Buffer& b, double amount)
{
    if (amount < 0.05)
        return;
    const double p = peakOf (b);
    if (p <= 0.0)
        return;
    const double pre = amount / p, post = 1.0 / std::tanh (amount);
    for (double& v : b.l)
        v = std::tanh (v * pre) * post;
    for (double& v : b.r)
        v = std::tanh (v * pre) * post;
}

// Sample-and-hold rate reduction, bit reduction, a low-pass and a little warmth: the sound of an old sampler.
void lofi (Buffer& b, double amount)
{
    if (amount <= 0.0)
        return;
    const double p = peakOf (b);
    if (p <= 0.0)
        return;
    scale (b, 1.0 / p);
    const double targetRate = std::max (8000.0, 30000.0 - 20000.0 * amount);
    const double quant = std::pow (2.0, std::round (16.0 - 9.0 * amount) - 1.0);
    auto run = [&] (std::vector<double>& x) {
        double acc = 1.0, held = 0.0;
        const double inc = targetRate / b.sr;
        Svf lp1 (0.42 * targetRate, 0.7071, b.sr), lp2 (0.42 * targetRate, 0.7071, b.sr);
        for (double& v : x)
        {
            acc += inc;
            if (acc >= 1.0)
            {
                acc -= 1.0;
                held = std::round (v * quant) / quant;
            }
            v = std::tanh (1.4 * lp2.lp (lp1.lp (held))) / std::tanh (1.4);
        }
    };
    run (b.l);
    if (b.stereo)
        run (b.r);
}

// Small Schroeder-Moorer room: four damped combs and two allpasses per channel, high-passed input so kicks do not
// get muddy. Only adds a tail; the dry signal stays as it is.
void room (Buffer& b, double amount)
{
    if (amount <= 0.0)
        return;
    const double t60 = 0.22 + 0.5 * amount;
    const double wet = 0.12 + 0.3 * amount;
    const int tail = (int) std::lround ((0.12 + 0.4 * amount) * b.sr);
    b.l.resize ((size_t) (b.n + tail), 0.0);
    if (b.stereo)
        b.r.resize ((size_t) (b.n + tail), 0.0);
    b.n += tail;

    auto run = [&] (std::vector<double>& x, double spread) {
        static constexpr double combMs[4] = { 16.3, 19.7, 22.6, 25.1 };
        static constexpr double allMs[2] = { 5.0, 1.7 };
        std::vector<double> combBuf[4], allBuf[2];
        int combLen[4], allLen[2], combPos[4] {}, allPos[2] {};
        double combFb[4], combLp[4] {};
        for (int c = 0; c < 4; ++c)
        {
            combLen[c] = std::max (1, (int) std::lround (combMs[c] * spread * 0.001 * b.sr));
            combBuf[c].assign ((size_t) combLen[c], 0.0);
            combFb[c] = std::pow (10.0, -3.0 * (combMs[c] * spread * 0.001) / t60);
        }
        for (int a = 0; a < 2; ++a)
        {
            allLen[a] = std::max (1, (int) std::lround (allMs[a] * spread * 0.001 * b.sr));
            allBuf[a].assign ((size_t) allLen[a], 0.0);
        }
        Svf inHp (180.0, 0.7071, b.sr), outLp (7000.0, 0.7071, b.sr);
        const double damp = 0.35;
        for (double& v : x)
        {
            const double in = inHp.hp (v) * 0.25;
            double sum = 0.0;
            for (int c = 0; c < 4; ++c)
            {
                double& slot = combBuf[c][(size_t) combPos[c]];
                const double out = slot;
                combLp[c] += damp * (out - combLp[c]);
                slot = in + combLp[c] * combFb[c];
                combPos[c] = (combPos[c] + 1) % combLen[c];
                sum += out;
            }
            for (int a = 0; a < 2; ++a)
            {
                double& slot = allBuf[a][(size_t) allPos[a]];
                const double delayed = slot;
                const double y = -0.7 * sum + delayed;
                slot = sum + 0.7 * y;
                allPos[a] = (allPos[a] + 1) % allLen[a];
                sum = y;
            }
            v += wet * outLp.lp (sum);
        }
    };
    run (b.l, 1.0);
    if (b.stereo)
        run (b.r, 1.093);
}

struct Finish
{
    double peakDb = -1.0;
    double fadeOutSec = 0.005;
    double room = 0.0;
    double lofi = 0.0;
};

// DC blocker, fades, exact zero mean, peak normalisation. The order matters: the mean is removed with a raised
// sine bump that is zero at both ends, so it cannot undo the fades, and the final scale keeps both properties.
SamplePtr finish (Buffer& b, const Finish& f, const SoundParams& p)
{
    lofi (b, f.lofi);
    room (b, f.room);

    auto process = [&] (std::vector<double>& x) {
        // One-pole DC blocker at 5 Hz: far below the 33 Hz of the 808 sub.
        const double rc = std::exp (-kTwoPi * 5.0 / b.sr);
        double px = 0.0, py = 0.0;
        for (double& v : x)
        {
            const double y = v - px + rc * py;
            px = v;
            py = y;
            v = y;
        }

        const int n = (int) x.size();
        const int fadeIn = std::max (2, (int) std::lround (0.00025 * b.sr));
        const int fadeOut = std::clamp ((int) std::lround (f.fadeOutSec * b.sr), 2, n / 2);
        for (int i = 0; i < fadeIn && i < n; ++i)
            x[(size_t) i] *= 0.5 - 0.5 * std::cos (std::numbers::pi * i / fadeIn);
        for (int i = 0; i < fadeOut; ++i)
            x[(size_t) (n - 1 - i)] *= 0.5 - 0.5 * std::cos (std::numbers::pi * i / fadeOut);

        double sum = 0.0, wsum = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double s = std::sin (std::numbers::pi * i / (n - 1));
            sum += x[(size_t) i];
            wsum += s * s;
        }
        const double k = sum / wsum;
        for (int i = 0; i < n; ++i)
        {
            const double s = std::sin (std::numbers::pi * i / (n - 1));
            x[(size_t) i] -= k * s * s;
        }
        x.front() = 0.0;
        x.back() = 0.0;
    };
    process (b.l);
    if (b.stereo)
        process (b.r);

    const double peak = peakOf (b);
    const double targetDb = std::min (-0.3, f.peakDb + std::min (0.0, (double) p.gainDb));
    if (peak > 0.0)
        scale (b, std::pow (10.0, targetDb / 20.0) / peak);

    auto s = std::make_shared<Sample>();
    s->rate = b.sr;
    s->channels = b.stereo ? 2 : 1;
    s->left.assign (b.l.begin(), b.l.end());
    if (b.stereo)
        s->right.assign (b.r.begin(), b.r.end());
    return s;
}

double clamp01 (float v) { return std::clamp ((double) v, 0.0, 1.0); }
double positive (float v, double lo, double hi) { return std::clamp ((double) v, lo, hi); }

struct Common
{
    double pitch, decay, tone, drive;
};

Common common (const SoundParams& p)
{
    return { positive (p.pitch, 0.125, 8.0), positive (p.decay, 0.1, 4.0), clamp01 (p.tone), clamp01 (p.drive) };
}

Finish finishFor (const SoundParams& p, double peakDb, double fadeOutSec, double roomBase = 0.0)
{
    Finish f;
    f.peakDb = peakDb;
    f.fadeOutSec = fadeOutSec;
    f.room = clamp01 (p.room) > 0.0 || roomBase > 0.0 ? std::min (1.0, roomBase + (1.0 - roomBase) * clamp01 (p.room)) : 0.0;
    f.lofi = clamp01 (p.lofi);
    return f;
}

// ---------------------------------------------------------------------------------------------------------------
// Recipes
// ---------------------------------------------------------------------------------------------------------------

SamplePtr tom (double sr, const SoundParams& p, double baseHz)
{
    const auto c = common (p);
    Buffer b (sr, 0.55 * c.decay + 0.05, false);
    Rng rng (p.seed);
    const double f0 = baseHz * c.pitch;
    Sweep body (f0, 0.7 * f0, 0.06, sr), overtone (1.58 * f0, 1.58 * 0.7 * f0, 0.05, sr);
    Svf strikeLp (2500.0 + 2000.0 * c.tone, 0.7, sr);
    for (int i = 0; i < b.n; ++i)
    {
        const double t = b.time (i);
        const double a = ramp (t, 0.0006);
        const double strike = strikeLp.lp (rng.white()) * std::exp (-t / 0.002) * 0.5 * c.tone;
        b.l[(size_t) i] = a * (body.next (t) * decayEnv (t, 0.45 * c.decay) + 0.18 * overtone.next (t) * decayEnv (t, 0.12 * c.decay))
                          + strike;
    }
    saturate (b, 1.2 + 3.0 * c.drive);
    return finish (b, finishFor (p, -1.0, 0.01), p);
}
} // namespace

SamplePtr kick808 (double sr, const SoundParams& p)
{
    const auto c = common (p);
    const double t60 = 0.85 * c.decay;
    Buffer b (sr, std::min (4.0, t60 * 1.05 + 0.03), false);
    Sweep body ((120.0 + 60.0 * c.tone) * c.pitch, 49.0 * c.pitch, 0.03, sr);
    for (int i = 0; i < b.n; ++i)
    {
        const double t = b.time (i);
        const double click = std::sin (kTwoPi * 3000.0 * t) * std::exp (-t / 0.0008) * 0.25 * c.tone;
        b.l[(size_t) i] = ramp (t, 0.001) * body.next (t) * decayEnv (t, t60) + click;
    }
    saturate (b, 1.5 + 4.0 * c.drive);
    return finish (b, finishFor (p, -1.0, 0.012), p);
}

SamplePtr kick909 (double sr, const SoundParams& p)
{
    const auto c = common (p);
    const double t60 = 0.5 * c.decay;
    Buffer b (sr, t60 * 1.05 + 0.03, false);
    Rng rng (p.seed);
    Sweep body (230.0 * c.pitch, 55.0 * c.pitch, 0.025, sr);
    Svf clickHp (1500.0 + 1500.0 * c.tone, 0.7071, sr);
    for (int i = 0; i < b.n; ++i)
    {
        const double t = b.time (i);
        const double click = clickHp.hp (rng.white()) * ramp (t, 0.0005) * std::exp (-t / 0.0025) * (0.08 + 0.3 * c.tone);
        b.l[(size_t) i] = ramp (t, 0.0008) * body.next (t) * decayEnv (t, t60) + click;
    }
    saturate (b, 2.5 + 4.0 * c.drive);
    return finish (b, finishFor (p, -1.0, 0.01), p);
}

SamplePtr kickPunchy (double sr, const SoundParams& p)
{
    const auto c = common (p);
    const double t60 = 0.42 * c.decay;
    Buffer b (sr, t60 * 1.05 + 0.03, false);
    Rng rng (p.seed);
    const double fEnd = 52.0 * c.pitch, fStart = 330.0 * c.pitch;
    Svf beater (4000.0, 0.8, sr);
    double phase = 0.0;
    for (int i = 0; i < b.n; ++i)
    {
        const double t = b.time (i);
        // Two-speed glide: the fast part is the punch, the slow part keeps a little "knock" in the body.
        const double f = fEnd + (fStart - fEnd) * (0.65 * std::exp (-t / 0.010) + 0.35 * std::exp (-t / 0.05));
        const double body = std::sin (phase) * (0.55 * std::exp (-t / 0.06) + 0.45 * decayEnv (t, t60));
        phase += kTwoPi * f / sr;
        const double click = ramp (t, 0.0004)
                             * (beater.bp (rng.white()) * std::exp (-t / 0.0015) * 0.9 * c.tone
                                + std::sin (kTwoPi * 1600.0 * t) * std::exp (-t / 0.002) * 0.2);
        b.l[(size_t) i] = ramp (t, 0.0008) * body + click;
    }
    saturate (b, 3.0 + 4.0 * c.drive);
    return finish (b, finishFor (p, -1.0, 0.01), p);
}

SamplePtr snare808 (double sr, const SoundParams& p)
{
    const auto c = common (p);
    Buffer b (sr, 0.32 * c.decay + 0.05, false);
    Rng rng (p.seed);
    Sweep body1 (180.0 * 1.08 * c.pitch, 180.0 * c.pitch, 0.01, sr), body2 (330.0 * 1.08 * c.pitch, 330.0 * c.pitch, 0.01, sr);
    Svf hp (1200.0 + 1200.0 * c.tone, 0.7071, sr), lp (9000.0 * (0.7 + 0.6 * c.tone), 0.7071, sr);
    const double noiseMix = 0.35 + 0.4 * c.tone;
    for (int i = 0; i < b.n; ++i)
    {
        const double t = b.time (i);
        const double bodies = body1.next (t) * decayEnv (t, 0.16 * c.decay) + 0.6 * body2.next (t) * decayEnv (t, 0.1 * c.decay);
        const double noise = lp.lp (hp.hp (rng.white())) * decayEnv (t, 0.24 * c.decay);
        b.l[(size_t) i] = ramp (t, 0.0003) * ((1.0 - noiseMix) * bodies + noiseMix * 1.6 * noise);
    }
    saturate (b, 0.5 + 3.0 * c.drive);
    return finish (b, finishFor (p, -1.0, 0.008), p);
}

SamplePtr snare909 (double sr, const SoundParams& p)
{
    const auto c = common (p);
    Buffer b (sr, 0.38 * c.decay + 0.05, false);
    Rng rng (p.seed);
    Sweep body (200.0 * c.pitch, 170.0 * c.pitch, 0.015, sr), body2 (340.0 * c.pitch, 320.0 * c.pitch, 0.015, sr);
    Svf bp (5000.0 * (0.8 + 0.4 * c.tone), 0.7, sr), hp (400.0, 0.7071, sr);
    const double noiseMix = 0.4 + 0.35 * c.tone;
    for (int i = 0; i < b.n; ++i)
    {
        const double t = b.time (i);
        const double bodies = (body.next (t) + 0.35 * body2.next (t)) * decayEnv (t, 0.12 * c.decay);
        const double noise = hp.hp (bp.bp (rng.white())) * (0.4 * std::exp (-t / 0.01) + 0.6 * decayEnv (t, 0.28 * c.decay));
        b.l[(size_t) i] = ramp (t, 0.0003) * ((1.0 - noiseMix) * bodies + noiseMix * 2.0 * noise);
    }
    saturate (b, 1.8 + 3.0 * c.drive);
    return finish (b, finishFor (p, -1.0, 0.008), p);
}

SamplePtr clap (double sr, const SoundParams& p)
{
    const auto c = common (p);
    Buffer b (sr, 0.42 * c.decay + 0.03, false);
    Rng rng (p.seed);
    Svf bp (1200.0 * c.pitch * (0.85 + 0.3 * c.tone), 1.5, sr), air (5000.0, 0.7071, sr);
    // Slightly uneven spacing and levels: perfectly regular bursts sound like a buzz, not like hands.
    static constexpr double onsets[4] = { 0.0, 0.0105, 0.0205, 0.031 };
    static constexpr double levels[4] = { 0.85, 1.0, 0.8, 0.9 };
    for (int i = 0; i < b.n; ++i)
    {
        const double t = b.time (i);
        double env = 0.0;
        for (int k = 0; k < 4; ++k)
            if (t >= onsets[k])
                env += levels[k] * ramp (t - onsets[k], 0.0003) * std::exp (-(t - onsets[k]) / 0.0035);
        if (t >= onsets[3])
            env += 0.75 * ramp (t - onsets[3], 0.002) * decayEnv (t - onsets[3], 0.2 * c.decay);
        const double w = rng.white();
        b.l[(size_t) i] = env * (bp.bp (w) + 0.15 * c.tone * air.hp (w));
    }
    saturate (b, 1.0 + 3.0 * c.drive);
    return finish (b, finishFor (p, -1.0, 0.01, 0.3), p);
}

SamplePtr rim (double sr, const SoundParams& p)
{
    const auto c = common (p);
    Buffer b (sr, 0.1 * c.decay + 0.03, false);
    Rng rng (p.seed);
    Svf tickHp (3000.0, 0.7071, sr), hp (250.0, 0.7071, sr);
    for (int i = 0; i < b.n; ++i)
    {
        const double t = b.time (i);
        const double modes = std::sin (kTwoPi * 1700.0 * c.pitch * t) * decayEnv (t, 0.045 * c.decay)
                             + 0.6 * std::sin (kTwoPi * 480.0 * c.pitch * t) * decayEnv (t, 0.03 * c.decay);
        const double tick = tickHp.hp (rng.white()) * std::exp (-t / 0.0007) * 1.2 * c.tone;
        b.l[(size_t) i] = hp.hp (ramp (t, 0.0002) * modes + tick);
    }
    saturate (b, 1.5 + 3.0 * c.drive);
    return finish (b, finishFor (p, -2.0, 0.006), p);
}

namespace
{
// Hats share one recipe; `length` and `t60` make the closed and the open one.
SamplePtr hat (double sr, const SoundParams& p, double length, double t60, double accent, double peakDb, double fadeOut)
{
    const auto c = common (p);
    Buffer b (sr, length, true);
    Rng rng (p.seed), rngL (p.seed * 2654435761u + 1u), rngR (p.seed * 2246822519u + 7u);
    Metal metal;
    metal.set (c.pitch, sr, rng);
    const double bright = 0.85 + 0.3 * c.tone;
    Svf bpL (10000.0 * bright, 0.9, sr), bpR (10000.0 * bright, 0.9, sr);
    Svf hpL (7000.0 * bright, 0.7071, sr), hpR (7000.0 * bright, 0.7071, sr);
    const double noiseAmt = 0.25 + 0.25 * c.tone;
    for (int i = 0; i < b.n; ++i)
    {
        const double t = b.time (i);
        const double env = ramp (t, 0.0002) * (accent * std::exp (-t / 0.008) + (1.0 - accent) * decayEnv (t, t60));
        const double m = metal.next();
        b.l[(size_t) i] = env * hpL.hp (bpL.bp (m + noiseAmt * rngL.white()));
        b.r[(size_t) i] = env * hpR.hp (bpR.bp (m + noiseAmt * rngR.white()));
    }
    saturate (b, 3.0 * c.drive);
    return finish (b, finishFor (p, peakDb, fadeOut), p);
}
} // namespace

SamplePtr closedHat (double sr, const SoundParams& p)
{
    const auto c = common (p);
    return hat (sr, p, std::min (0.38, 0.11 * c.decay + 0.02), 0.065 * c.decay, 0.2, -6.0, 0.006);
}

SamplePtr openHat (double sr, const SoundParams& p)
{
    const auto c = common (p);
    return hat (sr, p, std::min (1.2, 0.6 * c.decay), 0.5 * c.decay, 0.3, -5.0, 0.03);
}

SamplePtr crash (double sr, const SoundParams& p)
{
    const auto c = common (p);
    Buffer b (sr, 2.0 * c.decay, true);
    Rng rng (p.seed), rngL (p.seed * 2654435761u + 3u), rngR (p.seed * 2246822519u + 5u);
    // Two detuned banks give the dense, non-repeating wash a single 808 bank lacks; the right channel's second
    // bank sits a few cents higher for width.
    Metal a, bl, br;
    a.set (1.45 * c.pitch, sr, rng);
    bl.set (1.45 * 1.183 * c.pitch, sr, rng);
    br.set (1.45 * 1.183 * 1.0035 * c.pitch, sr, rng);
    const double bright = 0.85 + 0.3 * c.tone;
    Svf hiL (7500.0 * bright, 0.6, sr), hiR (7500.0 * bright, 0.6, sr), hpL (4500.0, 0.7071, sr), hpR (4500.0, 0.7071, sr);
    Svf loL (3200.0 * bright, 1.0, sr), loR (3200.0 * bright, 1.0, sr);
    for (int i = 0; i < b.n; ++i)
    {
        const double t = b.time (i);
        const double att = ramp (t, 0.0005);
        const double envHi = att * (0.5 * std::exp (-t / 0.02) + decayEnv (t, 1.9 * c.decay));
        const double envLo = att * decayEnv (t, 0.4 * c.decay);
        const double ma = a.next();
        const double inL = ma + bl.next() + 0.6 * rngL.white();
        const double inR = ma + br.next() + 0.6 * rngR.white();
        b.l[(size_t) i] = envHi * hpL.hp (hiL.bp (inL)) + 0.4 * envLo * loL.bp (inL);
        b.r[(size_t) i] = envHi * hpR.hp (hiR.bp (inR)) + 0.4 * envLo * loR.bp (inR);
    }
    saturate (b, 2.0 * c.drive);
    return finish (b, finishFor (p, -4.0, 0.15), p);
}

SamplePtr ride (double sr, const SoundParams& p)
{
    const auto c = common (p);
    Buffer b (sr, 2.5 * c.decay, true);
    Rng rng (p.seed), rngL (p.seed * 2654435761u + 11u), rngR (p.seed * 2246822519u + 13u);
    Metal ml, mr;
    ml.set (1.65 * c.pitch, sr, rng);
    mr.set (1.65 * 1.002 * c.pitch, sr, rng);
    const double bright = 0.85 + 0.3 * c.tone;
    Svf hpL (5000.0, 0.7071, sr), hpR (5000.0, 0.7071, sr), bpL (8000.0 * bright, 0.5, sr), bpR (8000.0 * bright, 0.5, sr);
    Svf pingL (6000.0, 0.7071, sr), pingR (6000.0, 0.7071, sr);
    static constexpr double bellHz[3] = { 780.0, 1050.0, 1560.0 };
    static constexpr double bellAmp[3] = { 0.12, 0.08, 0.04 };
    static constexpr double bellT60[3] = { 1.2, 0.9, 0.6 };
    for (int i = 0; i < b.n; ++i)
    {
        const double t = b.time (i);
        const double att = ramp (t, 0.0004);
        double bell = 0.0;
        for (int k = 0; k < 3; ++k)
            bell += bellAmp[k] * std::sin (kTwoPi * bellHz[k] * c.pitch * t) * decayEnv (t, bellT60[k] * c.decay);
        const double envM = 0.6 * decayEnv (t, 2.4 * c.decay);
        const double ping = std::exp (-t / 0.004) * 0.4;
        b.l[(size_t) i] = att * (envM * bpL.bp (hpL.hp (ml.next() + 0.3 * rngL.white())) + bell + ping * pingL.hp (rngL.white()));
        b.r[(size_t) i] = att * (envM * bpR.bp (hpR.hp (mr.next() + 0.3 * rngR.white())) + bell + ping * pingR.hp (rngR.white()));
    }
    saturate (b, 2.0 * c.drive);
    return finish (b, finishFor (p, -5.0, 0.15), p);
}

SamplePtr tomLow (double sr, const SoundParams& p) { return tom (sr, p, 120.0); }
SamplePtr tomMid (double sr, const SoundParams& p) { return tom (sr, p, 170.0); }
SamplePtr tomHigh (double sr, const SoundParams& p) { return tom (sr, p, 240.0); }

SamplePtr cowbell (double sr, const SoundParams& p)
{
    const auto c = common (p);
    Buffer b (sr, 0.38 * c.decay + 0.02, false);
    Square s1, s2;
    s1.set (540.0 * c.pitch, sr, 0.0);
    s2.set (800.0 * c.pitch, sr, 0.25);
    Svf bp (2600.0 * (0.85 + 0.3 * c.tone), 1.2, sr), hp (400.0, 0.7071, sr);
    for (int i = 0; i < b.n; ++i)
    {
        const double t = b.time (i);
        const double env = ramp (t, 0.0003) * (0.6 * std::exp (-t / 0.008) + 0.4 * decayEnv (t, 0.32 * c.decay));
        b.l[(size_t) i] = env * hp.hp (bp.bp (s1.next() + s2.next()));
    }
    saturate (b, 1.0 + 2.0 * c.drive);
    return finish (b, finishFor (p, -3.0, 0.01), p);
}

SamplePtr shaker (double sr, const SoundParams& p)
{
    const auto c = common (p);
    Buffer b (sr, 0.18 * c.decay + 0.03, true);
    Rng rngL (p.seed * 2654435761u + 17u), rngR (p.seed * 2246822519u + 19u);
    const double hpHz = 6000.0 * (0.8 + 0.4 * c.tone) * std::sqrt (c.pitch);
    Svf hpL (hpHz, 0.7071, sr), hpR (hpHz, 0.7071, sr), lpL (13000.0, 0.7071, sr), lpR (13000.0, 0.7071, sr);
    static constexpr double onsets[3] = { 0.0, 0.018, 0.04 };
    static constexpr double levels[3] = { 0.6, 1.0, 0.7 };
    for (int i = 0; i < b.n; ++i)
    {
        const double t = b.time (i);
        double env = 0.0;
        for (int k = 0; k < 3; ++k)
        {
            const double u = (t - onsets[k] * c.decay);
            if (u >= 0.0)
                env += levels[k] * (u < 0.004 ? u / 0.004 : std::exp (-(u - 0.004) / (0.012 * c.decay)));
        }
        b.l[(size_t) i] = env * lpL.lp (hpL.hp (rngL.white()));
        b.r[(size_t) i] = env * lpR.lp (hpR.hp (rngR.white()));
    }
    saturate (b, 2.0 * c.drive);
    return finish (b, finishFor (p, -8.0, 0.008), p);
}

SamplePtr perc (double sr, const SoundParams& p)
{
    const auto c = common (p);
    Buffer b (sr, 0.3 * c.decay + 0.03, false);
    Rng rng (p.seed);
    Svf strikeBp (2500.0, 1.0, sr);
    static constexpr double ratios[3] = { 1.0, 1.52, 2.36 };
    static constexpr double amps[3] = { 1.0, 0.35, 0.15 };
    static constexpr double t60s[3] = { 0.28, 0.12, 0.06 };
    double phases[3] {};
    const double f0 = 300.0 * c.pitch;
    for (int i = 0; i < b.n; ++i)
    {
        const double t = b.time (i);
        const double bend = 1.0 + 0.08 * std::exp (-t / 0.008);
        double v = 0.0;
        for (int k = 0; k < 3; ++k)
        {
            v += amps[k] * std::sin (phases[k]) * decayEnv (t, t60s[k] * c.decay);
            phases[k] += kTwoPi * f0 * ratios[k] * bend / sr;
        }
        const double strike = strikeBp.bp (rng.white()) * std::exp (-t / 0.0015) * 0.6 * c.tone;
        b.l[(size_t) i] = ramp (t, 0.0003) * v + strike;
    }
    saturate (b, 1.0 + 3.0 * c.drive);
    return finish (b, finishFor (p, -3.0, 0.008), p);
}

SamplePtr sub808 (double sr, const SoundParams& p)
{
    const auto c = common (p);
    Buffer b (sr, 1.5 * c.decay, false);
    const double root = kSub808RootHz * c.pitch;
    double phase = 0.0;
    for (int i = 0; i < b.n; ++i)
    {
        const double t = b.time (i);
        // A short glide from above gives the attack its knock; after 0.1 s the pitch is the root to within 0.01 %.
        const double f = root * (1.0 + 0.6 * std::exp (-t / 0.018));
        const double click = std::sin (kTwoPi * 2000.0 * t) * std::exp (-t / 0.001) * 0.1 * c.tone;
        b.l[(size_t) i] = ramp (t, 0.002) * std::sin (phase) * decayEnv (t, 2.0 * c.decay) + click;
        phase += kTwoPi * f / sr;
        if (phase > kTwoPi)
            phase -= kTwoPi;
    }
    // Symmetric saturation adds odd harmonics, so the sub is heard on small speakers, and keeps the zero
    // crossings (and so the pitch) where the sine had them.
    saturate (b, 1.8 + 4.0 * c.drive);
    return finish (b, finishFor (p, -1.0, 0.04), p);
}

SamplePtr snap (double sr, const SoundParams& p)
{
    const auto c = common (p);
    Buffer b (sr, 0.22 * c.decay + 0.03, false);
    Rng rng (p.seed);
    Svf bp (2200.0 * c.pitch * (0.85 + 0.3 * c.tone), 2.0, sr);
    static constexpr double onsets[3] = { 0.0, 0.005, 0.012 };
    static constexpr double levels[3] = { 0.7, 1.0, 0.5 };
    for (int i = 0; i < b.n; ++i)
    {
        const double t = b.time (i);
        double env = 0.0;
        for (int k = 0; k < 3; ++k)
            if (t >= onsets[k])
                env += levels[k] * ramp (t - onsets[k], 0.0002) * std::exp (-(t - onsets[k]) / 0.002);
        env += 0.25 * ramp (t, 0.001) * decayEnv (t, 0.08 * c.decay);
        const double body = 0.3 * std::sin (kTwoPi * 1300.0 * c.pitch * t) * decayEnv (t, 0.05 * c.decay) * ramp (t, 0.0003);
        b.l[(size_t) i] = env * bp.bp (rng.white()) + body;
    }
    saturate (b, 1.0 + 3.0 * c.drive);
    return finish (b, finishFor (p, -2.0, 0.008, 0.25), p);
}

SamplePtr renderSound (SoundType type, double sr, const SoundParams& p)
{
    if (! (sr >= 8000.0 && sr <= 384000.0))
        return nullptr;
    switch (type)
    {
        case SoundType::kick808: return kick808 (sr, p);
        case SoundType::kick909: return kick909 (sr, p);
        case SoundType::kickPunchy: return kickPunchy (sr, p);
        case SoundType::snare808: return snare808 (sr, p);
        case SoundType::snare909: return snare909 (sr, p);
        case SoundType::clap: return clap (sr, p);
        case SoundType::rim: return rim (sr, p);
        case SoundType::closedHat: return closedHat (sr, p);
        case SoundType::openHat: return openHat (sr, p);
        case SoundType::crash: return crash (sr, p);
        case SoundType::ride: return ride (sr, p);
        case SoundType::tomLow: return tomLow (sr, p);
        case SoundType::tomMid: return tomMid (sr, p);
        case SoundType::tomHigh: return tomHigh (sr, p);
        case SoundType::cowbell: return cowbell (sr, p);
        case SoundType::shaker: return shaker (sr, p);
        case SoundType::perc: return perc (sr, p);
        case SoundType::sub808: return sub808 (sr, p);
        case SoundType::snap: return snap (sr, p);
        case SoundType::count: break;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------------------------------------------
// Factory kits
// ---------------------------------------------------------------------------------------------------------------

namespace
{
// Chained setters keep the kit tables readable without designated initialisers.
struct P
{
    SoundParams s;
    P& pitch (float v) { s.pitch = v; return *this; }
    P& decay (float v) { s.decay = v; return *this; }
    P& tone (float v) { s.tone = v; return *this; }
    P& drive (float v) { s.drive = v; return *this; }
    P& lofi (float v) { s.lofi = v; return *this; }
    P& room (float v) { s.room = v; return *this; }
    P& gain (float v) { s.gainDb = v; return *this; }
};

struct Recipe
{
    SoundType type;
    const char* name;
    SoundParams params;
};

struct KitRecipe
{
    const char* name;
    std::array<Recipe, kNumPads> pads;
};

using T = SoundType;

const std::vector<KitRecipe>& kitRecipes()
{
    static const std::vector<KitRecipe> kits {
        { "808 Classic",
          { {
              { T::kick808, "808 Kick", P().decay (1.1f).tone (0.35f).drive (0.1f).s },
              { T::rim, "808 Rim", P().s },
              { T::snare808, "808 Snare", P().s },
              { T::clap, "808 Clap", P().room (0.2f).s },
              { T::snare808, "808 Snare Hi", P().pitch (1.12f).decay (0.7f).tone (0.7f).s },
              { T::tomLow, "808 Tom Lo", P().decay (1.2f).tone (0.3f).s },
              { T::closedHat, "808 Hat", P().s },
              { T::tomMid, "808 Tom Mid", P().decay (1.1f).tone (0.3f).s },
              { T::shaker, "808 Maracas", P().decay (0.7f).tone (0.6f).s },
              { T::tomHigh, "808 Tom Hi", P().tone (0.3f).s },
              { T::openHat, "808 Open Hat", P().s },
              { T::cowbell, "808 Cowbell", P().s },
              { T::sub808, "808 Sub C", P().s },
              { T::crash, "808 Cymbal", P().s },
              { T::perc, "808 Conga", P().s },
              { T::ride, "808 Ride", P().s },
          } } },
        { "909 Club",
          { {
              { T::kick909, "909 Kick", P().drive (0.25f).s },
              { T::rim, "909 Rim", P().pitch (1.1f).tone (0.7f).s },
              { T::snare909, "909 Snare", P().s },
              { T::clap, "909 Clap", P().pitch (1.1f).room (0.35f).s },
              { T::snare909, "909 Snare Tight", P().pitch (1.15f).decay (0.7f).tone (0.8f).s },
              { T::tomLow, "909 Tom Lo", P().pitch (1.05f).decay (0.9f).tone (0.6f).s },
              { T::closedHat, "909 Hat", P().pitch (1.25f).decay (0.9f).tone (0.7f).s },
              { T::tomMid, "909 Tom Mid", P().pitch (1.05f).decay (0.9f).tone (0.6f).s },
              { T::shaker, "Shaker", P().tone (0.7f).s },
              { T::tomHigh, "909 Tom Hi", P().pitch (1.05f).decay (0.9f).tone (0.6f).s },
              { T::openHat, "909 Open Hat", P().pitch (1.25f).tone (0.7f).s },
              { T::cowbell, "Cowbell", P().pitch (1.05f).tone (0.6f).s },
              { T::kickPunchy, "Club Kick", P().pitch (0.95f).s },
              { T::crash, "909 Crash", P().pitch (1.1f).tone (0.6f).s },
              { T::perc, "Clave", P().pitch (2.4f).decay (0.5f).tone (0.7f).s },
              { T::ride, "909 Ride", P().pitch (1.05f).tone (0.6f).s },
          } } },
        { "Trap",
          { {
              { T::kickPunchy, "Trap Kick", P().decay (0.8f).tone (0.7f).drive (0.3f).s },
              { T::rim, "Trap Rim", P().pitch (1.2f).decay (0.7f).tone (0.8f).s },
              { T::snare909, "Trap Snare", P().pitch (1.2f).decay (0.8f).tone (0.85f).drive (0.3f).s },
              { T::clap, "Trap Clap", P().pitch (1.2f).tone (0.7f).room (0.45f).s },
              { T::snap, "Snap", P().s },
              { T::tomLow, "808 Tom Lo", P().pitch (0.9f).decay (1.3f).tone (0.2f).s },
              { T::closedHat, "Trap Hat", P().pitch (1.4f).decay (0.6f).tone (0.8f).s },
              { T::tomMid, "808 Tom Mid", P().pitch (0.9f).decay (1.2f).tone (0.2f).s },
              { T::shaker, "Shaker", P().decay (0.8f).tone (0.8f).s },
              { T::tomHigh, "808 Tom Hi", P().pitch (0.9f).decay (1.1f).tone (0.2f).s },
              { T::openHat, "Trap Open Hat", P().pitch (1.4f).decay (0.8f).tone (0.8f).s },
              { T::cowbell, "Cowbell", P().pitch (1.1f).s },
              { T::sub808, "808 C1", P().drive (0.35f).s },
              { T::crash, "Crash", P().tone (0.7f).s },
              { T::perc, "Perc", P().pitch (1.5f).tone (0.7f).s },
              { T::ride, "Ride", P().tone (0.7f).s },
          } } },
        { "Boom Bap",
          { {
              { T::kickPunchy, "Dusty Kick", P().pitch (0.85f).decay (0.9f).tone (0.4f).drive (0.5f).lofi (0.35f).room (0.2f).s },
              { T::rim, "Dusty Rim", P().pitch (0.9f).lofi (0.35f).room (0.3f).s },
              { T::snare909, "Dusty Snare", P().pitch (0.9f).decay (1.1f).tone (0.5f).drive (0.5f).lofi (0.4f).room (0.45f).s },
              { T::clap, "Dusty Clap", P().pitch (0.9f).lofi (0.4f).room (0.5f).s },
              { T::snare808, "Snare 2", P().pitch (0.95f).tone (0.4f).drive (0.4f).lofi (0.4f).room (0.4f).s },
              { T::tomLow, "Tom Lo", P().pitch (0.9f).tone (0.5f).lofi (0.3f).room (0.3f).s },
              { T::closedHat, "Dusty Hat", P().pitch (0.9f).decay (1.1f).tone (0.35f).lofi (0.45f).s },
              { T::tomMid, "Tom Mid", P().pitch (0.9f).tone (0.5f).lofi (0.3f).room (0.3f).s },
              { T::shaker, "Shaker", P().tone (0.4f).lofi (0.4f).s },
              { T::tomHigh, "Tom Hi", P().pitch (0.9f).tone (0.5f).lofi (0.3f).room (0.3f).s },
              { T::openHat, "Dusty Open Hat", P().pitch (0.9f).decay (0.8f).tone (0.35f).lofi (0.45f).s },
              { T::cowbell, "Cowbell", P().tone (0.4f).lofi (0.4f).s },
              { T::kick808, "Kick 2", P().decay (0.6f).tone (0.3f).drive (0.5f).lofi (0.4f).s },
              { T::crash, "Dusty Crash", P().tone (0.35f).lofi (0.45f).s },
              { T::perc, "Bongo", P().pitch (1.3f).lofi (0.4f).room (0.3f).s },
              { T::ride, "Dusty Ride", P().tone (0.35f).lofi (0.45f).s },
          } } },
        { "Techno",
          { {
              { T::kick909, "Techno Kick", P().pitch (0.92f).decay (1.3f).tone (0.6f).drive (0.7f).s },
              { T::rim, "Rim", P().pitch (1.3f).tone (0.6f).s },
              { T::snare909, "Snare", P().decay (0.9f).tone (0.7f).drive (0.4f).s },
              { T::clap, "Big Clap", P().decay (1.2f).room (0.6f).s },
              { T::snare808, "Snare 2", P().pitch (1.1f).decay (0.6f).tone (0.8f).s },
              { T::tomLow, "Tom Lo", P().pitch (1.1f).decay (0.8f).tone (0.6f).s },
              { T::closedHat, "Hat", P().pitch (1.3f).decay (0.8f).tone (0.75f).s },
              { T::tomMid, "Tom Mid", P().pitch (1.1f).decay (0.8f).tone (0.6f).s },
              { T::shaker, "Shaker", P().decay (0.8f).tone (0.7f).s },
              { T::tomHigh, "Tom Hi", P().pitch (1.1f).decay (0.8f).tone (0.6f).s },
              { T::openHat, "Open Hat", P().pitch (1.3f).decay (1.1f).tone (0.75f).s },
              { T::cowbell, "Cowbell", P().pitch (1.1f).s },
              { T::kick909, "Rumble Kick", P().pitch (0.8f).decay (2.0f).drive (0.9f).room (0.6f).s },
              { T::crash, "Crash", P().tone (0.7f).s },
              { T::perc, "Blip", P().pitch (1.8f).decay (0.6f).tone (0.3f).s },
              { T::ride, "Ride", P().pitch (1.1f).tone (0.7f).s },
          } } },
        { "Lo-Fi",
          { {
              { T::kick808, "Soft Kick", P().pitch (0.95f).decay (0.7f).tone (0.2f).drive (0.3f).lofi (0.55f).s },
              { T::rim, "Rim", P().pitch (0.85f).tone (0.3f).lofi (0.6f).room (0.3f).s },
              { T::snare808, "Soft Snare", P().pitch (0.9f).decay (0.9f).tone (0.3f).lofi (0.6f).room (0.4f).s },
              { T::clap, "Soft Clap", P().pitch (0.85f).tone (0.3f).lofi (0.6f).room (0.4f).s },
              { T::snap, "Snap", P().pitch (0.85f).lofi (0.6f).room (0.3f).s },
              { T::tomLow, "Tom Lo", P().pitch (0.9f).tone (0.2f).lofi (0.55f).s },
              { T::closedHat, "Soft Hat", P().pitch (0.85f).decay (0.9f).tone (0.2f).lofi (0.6f).s },
              { T::tomMid, "Tom Mid", P().pitch (0.9f).tone (0.2f).lofi (0.55f).s },
              { T::shaker, "Shaker", P().tone (0.3f).lofi (0.6f).s },
              { T::tomHigh, "Tom Hi", P().pitch (0.9f).tone (0.2f).lofi (0.55f).s },
              { T::openHat, "Soft Open Hat", P().pitch (0.85f).decay (0.7f).tone (0.2f).lofi (0.6f).s },
              { T::cowbell, "Bell", P().pitch (0.9f).tone (0.2f).lofi (0.6f).room (0.3f).s },
              { T::sub808, "Warm Sub C", P().decay (0.8f).lofi (0.4f).s },
              { T::crash, "Soft Crash", P().tone (0.2f).lofi (0.6f).s },
              { T::perc, "Wood", P().pitch (1.6f).tone (0.3f).lofi (0.6f).s },
              { T::ride, "Soft Ride", P().tone (0.2f).lofi (0.6f).s },
          } } },
    };
    return kits;
}

const KitRecipe* findKit (const std::string& name, int* index = nullptr)
{
    const auto& kits = kitRecipes();
    for (size_t i = 0; i < kits.size(); ++i)
        if (name == kits[i].name)
        {
            if (index != nullptr)
                *index = (int) i;
            return &kits[i];
        }
    return nullptr;
}

constexpr const char* kFactoryPrefix = "factory:";
} // namespace

std::vector<FactoryKit> factoryKits()
{
    std::vector<FactoryKit> out;
    for (const auto& k : kitRecipes())
    {
        FactoryKit fk;
        fk.name = k.name;
        for (int p = 0; p < kNumPads; ++p)
            fk.padNames[(size_t) p] = k.pads[(size_t) p].name;
        out.push_back (std::move (fk));
    }
    return out;
}

SamplePtr renderFactorySound (const std::string& kitName, int pad, double sampleRate)
{
    int kitIndex = 0;
    const auto* kit = findKit (kitName, &kitIndex);
    if (kit == nullptr || pad < 0 || pad >= kNumPads)
        return nullptr;
    const auto& r = kit->pads[(size_t) pad];
    SoundParams p = r.params;
    // A fixed seed per kit and pad: every kit's noise is its own, and the same pad always renders the same.
    p.seed = 0x5eed0000u + (uint32_t) kitIndex * 977u + (uint32_t) pad * 131u;
    return renderSound (r.type, sampleRate, p);
}

std::string factorySource (const std::string& kitName, int pad)
{
    return std::string (kFactoryPrefix) + kitName + "#" + std::to_string (pad);
}

bool parseFactorySource (const std::string& source, std::string& kitName, int& pad)
{
    const std::string prefix (kFactoryPrefix);
    if (source.rfind (prefix, 0) != 0)
        return false;
    const size_t hash = source.rfind ('#');
    if (hash == std::string::npos || hash <= prefix.size() || hash + 1 >= source.size())
        return false;
    const std::string number = source.substr (hash + 1);
    if (number.find_first_not_of ("0123456789") != std::string::npos)
        return false;
    const int p = std::atoi (number.c_str());
    if (p < 0 || p >= kNumPads)
        return false;
    kitName = source.substr (prefix.size(), hash - prefix.size());
    pad = p;
    return true;
}

KitState factoryKitState (const std::string& kitName)
{
    KitState kit;
    kit.name = kitName;
    const auto* recipe = findKit (kitName);
    if (recipe == nullptr)
        return kit;
    for (int p = 0; p < kNumPads; ++p)
    {
        auto& pad = kit.pads[(size_t) p];
        pad.name = recipe->pads[(size_t) p].name;
        pad.source = factorySource (kitName, p);
    }
    // The open hat is cut by the closed one, as on every drum machine.
    kit.pads[(size_t) Pad::closedHat].params.chokeGroup = 1;
    kit.pads[(size_t) Pad::openHat].params.chokeGroup = 1;
    return kit;
}

} // namespace daw::drums
