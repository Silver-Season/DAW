// Tests of the modulation effects (docs/spec/06-effects-mixing.md, task 6.2): chorus, flanger, phaser, auto pan.
#include "FxTest.h"

#include <algorithm>
#include <chrono>
#include <complex>

using namespace fxtest;

namespace
{
// The `hz` component of x[from, from + n) as a complex number (Hann window), for transfer function estimates.
std::complex<double> bin (const std::vector<float>& x, double hz, size_t from, size_t n)
{
    std::complex<double> sum;
    for (size_t i = 0; i < n && from + i < x.size(); ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * (double) i / (double) (n - 1));
        sum += w * x[from + i] * std::polar (1.0, -2.0 * std::numbers::pi * hz * (double) (from + i) / kRate);
    }
    return sum;
}

// |out / in| at `hz` in dB, averaged over the power of consecutive segments (a stable estimate for noise).
double gainDbAt (const Stereo& in, const Stereo& out, double hz, size_t segment = 4096, size_t from = 0)
{
    double pin = 0.0, pout = 0.0;
    for (size_t pos = from; pos + segment <= in.size(); pos += segment)
    {
        pin += std::norm (bin (in.l, hz, pos, segment));
        pout += std::norm (bin (out.l, hz, pos, segment));
    }
    return 10.0 * std::log10 (std::max (1.0e-30, pout) / std::max (1.0e-30, pin));
}

size_t argMaxAbs (const std::vector<float>& x)
{
    size_t at = 0;
    for (size_t i = 0; i < x.size(); ++i)
        if (std::abs (x[i]) > std::abs (x[at]))
            at = i;
    return at;
}

Stereo constant (size_t n, float value = 1.0f)
{
    Stereo s (n);
    std::fill (s.l.begin(), s.l.end(), value);
    std::fill (s.r.begin(), s.r.end(), value);
    return s;
}

// Runs `blocks` blocks of `block` samples with the transport advancing the way a host would do it.
Stereo runSynced (daw::fx::Effect& fx, const Stereo& in, double bpm, double startPpq, int block)
{
    Stereo out = in;
    double ppq = startPpq;
    for (size_t pos = 0; pos < in.size(); pos += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, in.size() - pos);
        fx.setTransport ({ bpm, ppq, true });
        float* io[2] { out.l.data() + pos, out.r.data() + pos };
        fx.process (io, 2, n, nullptr);
        ppq += (double) n / kRate * bpm / 60.0;
    }
    return out;
}

double maxStep (const std::vector<float>& x, size_t from = 0)
{
    double m = 0.0;
    for (size_t i = from + 1; i < x.size(); ++i)
        m = std::max (m, (double) std::abs (x[i] - x[i - 1]));
    return m;
}
} // namespace

FX_TEST ("chorus: depth 0 and one voice is a pure delay of `delay` ms")
{
    for (const float ms : { 12.0f, 2.0f, 7.3f, 30.0f })
    {
        auto fx = make ("chorus");
        set (*fx, "voices", 0.0f);
        set (*fx, "depth", 0.0f);
        set (*fx, "feedback", 0.0f);
        set (*fx, "delay", ms);
        set (*fx, "mix", 100.0f);
        fx->reset();
        const auto out = run (*fx, impulse (4800, 100));
        const double expected = 100.0 + ms * 0.001 * kRate;
        const size_t at = argMaxAbs (out.l);
        check (std::abs ((double) at - expected) <= 1.0, "chorus " + std::to_string (ms) + " ms: peak at " + std::to_string (at) + ", expected "
                                                             + std::to_string (expected));
        check (peak (out.l) > 0.4, "chorus delay: the impulse keeps its energy (peak " + std::to_string (peak (out.l)) + ")");
        // With Width at 100 % but no depth both channels still see the same fixed delay.
        double diff = 0.0;
        for (size_t i = 0; i < out.size(); ++i)
            diff = std::max (diff, (double) std::abs (out.l[i] - out.r[i]));
        check (diff < 1.0e-6, "chorus delay: channels equal without modulation");
    }
}

FX_TEST ("chorus: voices differ, width widens a mono source, level stays close to the input")
{
    const auto in = noise (-12.0, 96000, 21);
    Stereo mono = in;
    mono.r = mono.l;
    for (int voices = 0; voices < 4; ++voices)
    {
        auto fx = make ("chorus");
        set (*fx, "voices", (float) voices);
        set (*fx, "mix", 50.0f);
        fx->reset();
        const auto out = run (*fx, mono);
        check (finite (out), "chorus voices: finite");
        const double db = linToDb (rms (out.l, 9600) / rms (mono.l, 9600));
        // 3 dB for an uncorrelated wet at Mix 50, plus what the interpolation takes from the highs
        check (std::abs (db) < 4.0, "chorus " + std::to_string (voices + 1) + " voices: level change " + std::to_string (db) + " dB");
        double side = 0.0;
        for (size_t i = 9600; i < out.size(); ++i)
            side += std::abs (out.l[i] - out.r[i]);
        check (side > 1.0, "chorus " + std::to_string (voices + 1) + " voices: stereo width appears from a mono source");
    }
    auto fx = make ("chorus");
    set (*fx, "width", 0.0f);
    fx->reset();
    const auto out = run (*fx, mono);
    double side = 0.0;
    for (size_t i = 0; i < out.size(); ++i)
        side = std::max (side, (double) std::abs (out.l[i] - out.r[i]));
    check (side < 1.0e-6, "chorus: width 0 keeps a mono source mono");
}

FX_TEST ("flanger: feedback makes the comb peaks deeper")
{
    const auto in = noise (-12.0, 96000, 5);
    // 1 ms = 48 samples: peaks at multiples of 1 kHz, valleys halfway between.
    auto flange = [&] (float feedback, float mix)
    {
        auto fx = make ("flanger");
        set (*fx, "sync", 0.0f);
        set (*fx, "depth", 0.0f);
        set (*fx, "delay", 1.0f);
        set (*fx, "phase", 0.0f);
        set (*fx, "feedback", feedback);
        set (*fx, "mix", mix);
        fx->reset();
        return run (*fx, in);
    };

    const auto flat = flange (0.0f, 100.0f);
    const auto deep = flange (70.0f, 100.0f);
    const double flatSpread = gainDbAt (in, flat, 2000.0) - gainDbAt (in, flat, 2500.0);
    const double deepSpread = gainDbAt (in, deep, 2000.0) - gainDbAt (in, deep, 2500.0);
    check (std::abs (flatSpread) < 1.5, "flanger wet only without feedback is flat (" + std::to_string (flatSpread) + " dB)");
    check (deepSpread > 12.0, "flanger wet only with feedback 70 %: peak " + std::to_string (deepSpread) + " dB above the valley (theory 15)");

    const auto plain = flange (0.0f, 50.0f);
    const auto resonant = flange (70.0f, 50.0f);
    const double lift = gainDbAt (in, resonant, 2000.0) - gainDbAt (in, plain, 2000.0);
    check (lift > 5.0, "flanger mixed with the dry: feedback lifts the peak by " + std::to_string (lift) + " dB (theory 6.7)");

    // Negative feedback moves the peaks to the odd multiples of 500 Hz.
    const auto inverted = flange (-70.0f, 100.0f);
    const double oddSpread = gainDbAt (in, inverted, 2500.0) - gainDbAt (in, inverted, 2000.0);
    check (oddSpread > 12.0, "flanger negative feedback: peak at 2.5 kHz is " + std::to_string (oddSpread) + " dB above 2 kHz");
}

FX_TEST ("flanger: the stereo phase offsets the right sweep, a locked sweep repeats")
{
    auto fx = make ("flanger");
    set (*fx, "phase", 0.0f);
    set (*fx, "mix", 100.0f);
    set (*fx, "rate", 2.0f);
    fx->reset();
    const auto same = run (*fx, noise (-12.0, 24000, 4));
    double diff = 0.0;
    // The noise differs per channel, so compare the effect on a mono source instead.
    Stereo mono = noise (-12.0, 24000, 4);
    mono.r = mono.l;
    fx->reset();
    const auto monoOut = run (*fx, mono);
    for (size_t i = 0; i < monoOut.size(); ++i)
        diff = std::max (diff, (double) std::abs (monoOut.l[i] - monoOut.r[i]));
    check (diff < 1.0e-6, "flanger phase 0: both channels sweep together");

    set (*fx, "phase", 90.0f);
    fx->reset();
    const auto apart = run (*fx, mono);
    diff = 0.0;
    for (size_t i = 0; i < apart.size(); ++i)
        diff = std::max (diff, (double) std::abs (apart.l[i] - apart.r[i]));
    check (diff > 0.1, "flanger phase 90: the channels differ");
    check (finite (same) && finite (apart), "flanger: finite");

    // Synced: the same song position gives the same sound.
    set (*fx, "sync", 1.0f);
    set (*fx, "div", 0.0f);
    auto render = [&] (double ppq)
    {
        fx->reset();
        fx->setTransport ({ 120.0, ppq, true });
        return run (*fx, mono, 512);
    };
    const auto first = render (0.0), again = render (1.0); // one cycle is a quarter note: same phase one note later
    double d = 0.0;
    for (size_t i = 0; i < 512; ++i)
        d = std::max (d, (double) std::abs (first.l[i] - again.l[i]));
    check (d < 1.0e-4, "flanger synced: one quarter note later the sweep starts at the same place (" + std::to_string (d) + ")");
}

FX_TEST ("phaser: the notches move with the LFO, no NaN, level close to the input at Mix 50")
{
    // The sweep is locked to the song at 60 bpm with a cycle of 8 bars (32 s), so that a window of a third of a second
    // sees an almost standing filter. A quarter of a cycle is the top of the sine, three quarters the bottom.
    const size_t window = 32768, warm = 4096;
    const auto in = noise (-12.0, window + warm, 17);

    auto firstNotch = [&] (double ppq, double* depthDb)
    {
        auto fx = make ("phaser");
        set (*fx, "sync", 1.0f);
        set (*fx, "div", 5.0f);
        set (*fx, "phase", 0.0f);
        set (*fx, "feedback", 0.0f);
        set (*fx, "depth", 70.0f);
        set (*fx, "center", 800.0f);
        set (*fx, "mix", 50.0f);
        fx->reset();
        fx->setTransport ({ 60.0, ppq, true });
        const auto out = run (*fx, in);
        check (finite (out), "phaser: finite");
        // |H| on a log grid of 40 Hz ... 12 kHz; the first dip below -15 dB is the lowest notch.
        double prev = 0.0, prev2 = 0.0, found = 0.0;
        for (int k = 0; k < 400 && found == 0.0; ++k)
        {
            const double hz = 40.0 * std::pow (12000.0 / 40.0, k / 399.0);
            const double mag = 20.0 * std::log10 (std::max (1.0e-9, std::abs (bin (out.l, hz, warm, window) / bin (in.l, hz, warm, window))));
            if (k >= 2 && prev < prev2 && prev < mag && prev < -15.0)
            {
                found = 40.0 * std::pow (12000.0 / 40.0, (k - 1) / 399.0);
                *depthDb = prev;
            }
            prev2 = prev;
            prev = mag;
        }
        return found;
    };

    double depthTop = 0.0, depthBottom = 0.0;
    const double top = firstNotch (8.0, &depthTop);    // sine at +1: corner 800 * 2^1.75 = 2.7 kHz
    const double bottom = firstNotch (24.0, &depthBottom); // sine at -1: corner 238 Hz
    std::printf ("  phaser: lowest notch %.0f Hz (%.1f dB) at the top of the sweep, %.0f Hz (%.1f dB) at the bottom\n", top, depthTop, bottom,
                 depthBottom);
    check (top > 0.0 && bottom > 0.0, "phaser: both windows have a notch");
    check (top > 4.0 * bottom, "phaser: the notch moves (" + std::to_string (top) + " Hz vs " + std::to_string (bottom) + " Hz)");
    // Six stages: the first notch is where every stage has turned by 30 degrees, f = fc * tan (15 degrees).
    check (std::abs (top / (2693.0 * 0.2679) - 1.0) < 0.2, "phaser: first notch near the theory at the top (" + std::to_string (top) + ")");
    check (std::abs (bottom / (238.0 * 0.2679) - 1.0) < 0.3, "phaser: first notch near the theory at the bottom (" + std::to_string (bottom) + ")");

    // Free-running at the defaults: the level stays within 3 dB of the input.
    for (const int stages : { 0, 2, 5 })
    {
        auto fx = make ("phaser");
        set (*fx, "stages", (float) stages);
        fx->reset();
        const auto noisy = noise (-12.0, 96000, 3);
        const auto out = run (*fx, noisy);
        check (finite (out), "phaser: finite on noise");
        const double db = linToDb (rms (out.l, 9600) / rms (noisy.l, 9600));
        std::printf ("  phaser %d stages: level at Mix 50 is %+.2f dB\n", 2 * (stages + 1), db);
        check (std::abs (db) < 3.0, "phaser " + std::to_string (2 * (stages + 1)) + " stages: output within 3 dB of the input (" + std::to_string (db) + ")");
    }
}

FX_TEST ("phaser: stereo phase decorrelates a mono source, Mix 0 passes it unchanged")
{
    Stereo mono = noise (-12.0, 48000, 8);
    mono.r = mono.l;
    auto fx = make ("phaser");
    set (*fx, "phase", 0.0f);
    fx->reset();
    auto out = run (*fx, mono);
    double diff = 0.0;
    for (size_t i = 0; i < out.size(); ++i)
        diff = std::max (diff, (double) std::abs (out.l[i] - out.r[i]));
    check (diff < 1.0e-6, "phaser phase 0: channels identical");
    set (*fx, "phase", 120.0f);
    fx->reset();
    out = run (*fx, mono);
    diff = 0.0;
    for (size_t i = 0; i < out.size(); ++i)
        diff = std::max (diff, (double) std::abs (out.l[i] - out.r[i]));
    check (diff > 0.05, "phaser phase 120: channels differ");
}

FX_TEST ("autopan: phase 180 at depth 100 is a constant-power pan, phase 0 a tremolo")
{
    const auto ones = constant (96000);
    auto render = [&] (float shape, float depth, float phase)
    {
        auto fx = make ("autopan");
        set (*fx, "sync", 0.0f);
        set (*fx, "rate", 2.0f);
        set (*fx, "shape", shape);
        set (*fx, "depth", depth);
        set (*fx, "phase", phase);
        fx->reset();
        return run (*fx, ones);
    };

    // Sine, triangle, square and S&H are all antisymmetric half a cycle later, so the law holds for them.
    const char* names[] { "sine", "triangle", "saw", "square", "S&H" };
    for (const int shape : { 0, 1, 3, 4 })
        for (const float depth : { 100.0f, 60.0f })
        {
            const auto out = render ((float) shape, depth, 180.0f);
            double lo = 1.0e9, hi = -1.0e9, gMin = 1.0e9, gMax = 0.0;
            for (size_t i = 0; i < out.size(); ++i)
            {
                const double power = (double) out.l[i] * out.l[i] + (double) out.r[i] * out.r[i];
                lo = std::min (lo, power);
                hi = std::max (hi, power);
                gMin = std::min (gMin, (double) out.l[i]);
                gMax = std::max (gMax, (double) out.l[i]);
            }
            check (hi - lo < 0.05, std::string ("autopan ") + names[shape] + " depth " + std::to_string (depth) + ": L^2 + R^2 within "
                                       + std::to_string (hi - lo));
            // The peak of each channel is 0 dB; the power of the pair is 1 at full depth and 1 / cos^2 (pi / 4 (1 - depth)) otherwise.
            const double expected = 1.0 / std::pow (std::cos (std::numbers::pi * 0.25 * (1.0 - depth / 100.0)), 2.0);
            check (std::abs (hi - expected) < 0.02 && std::abs (lo - expected) < 0.02, std::string ("autopan ") + names[shape] + ": the constant is " + std::to_string (expected) + " (" + std::to_string (lo) + ".."
                                               + std::to_string (hi) + ")");
            if (shape == 0 && depth == 100.0f)
                check (gMin < 0.05 && gMax > 0.99, "autopan pans fully: left gain " + std::to_string (gMin) + ".." + std::to_string (gMax));
        }

    // Phase 0: the same gain on both channels, moving between silence and unity.
    for (const int shape : { 0, 1, 2, 3, 4 })
    {
        const auto out = render ((float) shape, 100.0f, 0.0f);
        double diff = 0.0, lo = 1.0e9, hi = -1.0e9;
        for (size_t i = 0; i < out.size(); ++i)
        {
            diff = std::max (diff, (double) std::abs (out.l[i] - out.r[i]));
            lo = std::min (lo, (double) out.l[i]);
            hi = std::max (hi, (double) out.l[i]);
        }
        check (diff < 1.0e-6, std::string ("autopan ") + names[shape] + " tremolo: identical channels");
        check (hi <= 1.0001 && hi - lo > (shape == 4 ? 0.1 : 0.4), std::string ("autopan ") + names[shape] + " tremolo: gain moves " + std::to_string (lo) + ".."
                                                  + std::to_string (hi) + ", never above unity");
    }

    // Depth 0 is transparent at any phase.
    for (const float phase : { 0.0f, 90.0f, 180.0f })
    {
        const auto out = render (0.0f, 0.0f, phase);
        check (std::abs (peak (out.l) - 1.0) < 1.0e-4 && std::abs (rms (out.r) - 1.0) < 1.0e-4, "autopan depth 0 leaves the signal alone");
    }
}

FX_TEST ("autopan: with sync the sweep starts at the same gain at every start")
{
    const auto ones = constant (64);
    for (int shape = 0; shape < 5; ++shape)
    {
        auto fx = make ("autopan");
        set (*fx, "sync", 1.0f);
        set (*fx, "div", 2.0f); // 1/8: half a quarter note per cycle
        set (*fx, "shape", (float) shape);
        set (*fx, "phase", 90.0f);
        set (*fx, "offset", 30.0f);
        fx->reset();

        auto firstGain = [&] (double ppq)
        {
            fx->setTransport ({ 120.0, ppq, true });
            const auto out = run (*fx, ones, 64);
            return std::pair<float, float> { out.l[0], out.r[0] };
        };

        const auto a = firstGain (0.0);
        firstGain (3.37); // somewhere else, with the smoothing of the edges in a different state
        const auto b = firstGain (0.0);
        runSynced (*fx, constant (48000), 120.0, 1.1, 128); // and a second of playing
        const auto c = firstGain (0.0);
        check (std::abs (a.first - b.first) < 1.0e-6 && std::abs (a.second - b.second) < 1.0e-6,
               "autopan shape " + std::to_string (shape) + ": ppq 0 gives " + std::to_string (a.first) + "/" + std::to_string (a.second)
                   + " and again " + std::to_string (b.first) + "/" + std::to_string (b.second));
        check (std::abs (a.first - c.first) < 1.0e-6 && std::abs (a.second - c.second) < 1.0e-6, "autopan shape " + std::to_string (shape) + ": same after playing");

        if (shape != 4) // S&H draws a new value in every cycle, so only the position itself repeats
        {
            const auto d = firstGain (4.0); // eight whole cycles later
            check (std::abs (a.first - d.first) < 1.0e-5 && std::abs (a.second - d.second) < 1.0e-5,
                   "autopan shape " + std::to_string (shape) + ": ppq 4 starts at the same place as ppq 0");
        }
    }

    // The sine at phase 0 starts in the middle of the sweep, which is the -3 dB point of the pan law.
    auto fx = make ("autopan");
    fx->setTransport ({ 120.0, 0.0, true });
    const auto out = run (*fx, ones, 64);
    check (std::abs (out.l[0] - 0.70711f) < 1.0e-3f && std::abs (out.r[0] - 0.70711f) < 1.0e-3f, "autopan: ppq 0 starts in the middle");

    // A host that calls setTransport on every block does not cause steps in the gain.
    fx->reset();
    const auto swept = runSynced (*fx, constant (96000), 120.0, 0.0, 128);
    check (maxStep (swept.l) < 0.02 && maxStep (swept.r) < 0.02, "autopan synced: no steps at the block boundaries (" + std::to_string (maxStep (swept.l)) + ")");

    // The sweep follows the tempo: a quarter-note cycle at 120 bpm is 0.5 s.
    set (*fx, "div", 3.0f);
    fx->reset();
    const auto slow = runSynced (*fx, constant (48000), 120.0, 0.0, 256);
    size_t crossings = 0;
    for (size_t i = 1; i < slow.size(); ++i)
        crossings += (slow.l[i - 1] - 0.7071f) * (slow.l[i] - 0.7071f) < 0.0f ? 1u : 0u;
    check (crossings == 4, "autopan 1/4 at 120 bpm: two cycles in a second (" + std::to_string (crossings) + " crossings)");
}

FX_TEST ("autopan: square, saw and S&H have no jump above 0.05 on a constant input")
{
    const auto ones = constant (96000);
    const char* names[] { "sine", "triangle", "saw", "square", "S&H" };
    for (int shape = 0; shape < 5; ++shape)
        for (const float phase : { 0.0f, 180.0f })
        {
            auto fx = make ("autopan");
            set (*fx, "sync", 0.0f);
            set (*fx, "rate", 9.0f);
            set (*fx, "shape", (float) shape);
            set (*fx, "depth", 100.0f);
            set (*fx, "phase", phase);
            fx->reset();
            const auto out = run (*fx, ones);
            const double step = std::max (maxStep (out.l), maxStep (out.r));
            check (step <= 0.05, std::string ("autopan ") + names[shape] + " phase " + std::to_string ((int) phase) + ": largest step "
                                     + std::to_string (step));
            if (shape == 3)
                check (step > 0.005, "autopan square still has an edge (largest step " + std::to_string (step) + ")");
        }
}

FX_TEST ("modulation effects: random settings stay finite and bounded")
{
    std::mt19937 rng (99);
    std::uniform_real_distribution<float> dist (0.0f, 1.0f);
    for (const char* type : { "chorus", "flanger", "phaser", "autopan" })
    {
        auto fx = make (type);
        for (int trial = 0; trial < 12; ++trial)
        {
            for (int p = 0; p < (int) fx->params().size(); ++p)
                fx->setParam (p, daw::fx::fromNormalised (fx->params()[(size_t) p], trial < 2 ? (float) trial : dist (rng)));
            fx->setTransport ({ 40.0 + 20.0 * trial, 0.25 * trial, trial % 3 != 0 });
            const auto out = run (*fx, noise (-6.0, 24000, (unsigned) trial + 1), 100 + trial);
            check (finite (out), std::string (type) + ": finite with random settings, trial " + std::to_string (trial));
            check (peak (out.l) < 200.0 && peak (out.r) < 200.0, std::string (type) + ": bounded, trial " + std::to_string (trial) + " peak "
                                                                     + std::to_string (peak (out.l)));
        }
    }
}

FX_TEST ("modulation effects: mono and odd block sizes work")
{
    for (const char* type : { "chorus", "flanger", "phaser", "autopan" })
    {
        auto fx = make (type);
        auto in = noise (-12.0, 9000, 2);
        float* io[1] { in.l.data() };
        for (size_t pos = 0; pos < in.size(); pos += 77)
        {
            float* chunk[1] { io[0] + pos };
            fx->process (chunk, 1, (int) std::min<size_t> (77, in.size() - pos), nullptr);
        }
        check (peak (in.l) > 0.0 && std::isfinite (peak (in.l)), std::string (type) + ": mono works");
    }
}

FX_TEST ("modulation effects: CPU for 10 s of stereo noise at 48 kHz is under 1 % of real time")
{
    struct Setup
    {
        const char* type;
        const char* label;
        std::vector<std::pair<const char*, float>> params;
    };
    const std::vector<Setup> setups {
        { "chorus", "defaults", {} },
        { "chorus", "4 voices, feedback 50 %", { { "voices", 3.0f }, { "feedback", 50.0f } } },
        { "flanger", "defaults", {} },
        { "flanger", "synced, feedback -90 %", { { "sync", 1.0f }, { "feedback", -90.0f } } },
        { "phaser", "defaults", {} },
        { "phaser", "12 stages", { { "stages", 5.0f } } },
        { "autopan", "defaults", {} },
        { "autopan", "square, free", { { "sync", 0.0f }, { "shape", 3.0f } } },
        { "autopan", "S&H, 180 degrees, free", { { "sync", 0.0f }, { "shape", 4.0f } } },
    };
    const auto in = noise (-12.0, 480000, 1);
    for (const auto& s : setups)
    {
        auto fx = make (s.type);
        for (const auto& [id, v] : s.params)
            set (*fx, id, v);
        fx->reset();
        double best = 1.0e9;
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            fx->reset();
            fx->setTransport ({ 120.0, 0.0, true });
            auto buffer = in;
            const auto t0 = std::chrono::steady_clock::now();
            for (size_t pos = 0; pos < buffer.size(); pos += 512)
            {
                float* io[2] { buffer.l.data() + pos, buffer.r.data() + pos };
                fx->process (io, 2, (int) std::min<size_t> (512, buffer.size() - pos), nullptr);
            }
            best = std::min (best, std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
        }
        std::printf ("  cpu %-8s %-26s %7.2f ms for 10 s = %.3f %%\n", s.type, s.label, best * 1000.0, best / 10.0 * 100.0);
#ifdef NDEBUG // an unoptimised or sanitised build is meant to be slow; the budget is for the release build
        check (best / 10.0 < 0.01, std::string (s.type) + " (" + s.label + "): under 1 % of real time");
#endif
    }
}
