// Reverb and delay (docs/spec/06-effects-mixing.md, task 6.2; spec test 6 and the checks of their DSP).
#include "FxTest.h"

#include "daw/fx/Helpers.h"

#include <chrono>

using namespace fxtest;

namespace
{
// Index of the largest |x| in [from, to).
size_t peakIndex (const std::vector<float>& x, size_t from, size_t to)
{
    size_t best = from;
    for (size_t i = from; i < std::min (to, x.size()); ++i)
        if (std::abs (x[i]) > std::abs (x[best]))
            best = i;
    return best;
}

double energy (const std::vector<float>& x, size_t from, size_t to)
{
    double e = 0.0;
    for (size_t i = from; i < std::min (to, x.size()); ++i)
        e += (double) x[i] * x[i];
    return e;
}

double maxStep (const std::vector<float>& x, size_t from = 1)
{
    double m = 0.0;
    for (size_t i = std::max<size_t> (1, from); i < x.size(); ++i)
        m = std::max (m, (double) std::abs (x[i] - x[i - 1]));
    return m;
}

// A Hann-windowed 1 kHz burst: band-limited, so the filters of the delay loop leave its level alone.
Stereo burst (size_t n, size_t length, float amplitude)
{
    Stereo s (n);
    for (size_t i = 0; i < length && i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * (double) i / (double) (length - 1));
        s.l[i] = s.r[i] = amplitude * (float) (w * std::sin (2.0 * std::numbers::pi * 1000.0 * (double) i / kRate));
    }
    return s;
}

// Runs the effect, calling `change` once when the block that starts at `at` is reached.
template <typename Change>
Stereo runWithChange (daw::fx::Effect& fx, const Stereo& in, size_t at, Change change)
{
    Stereo out = in;
    bool changed = false;
    for (size_t pos = 0; pos < in.size(); pos += (size_t) kBlock)
    {
        if (! changed && pos >= at)
        {
            change();
            changed = true;
        }
        const int n = (int) std::min ((size_t) kBlock, in.size() - pos);
        float* io[2] { out.l.data() + pos, out.r.data() + pos };
        fx.process (io, 2, n, nullptr);
    }
    return out;
}

// RT60 from the Schroeder backward integral of `h`: the slope between -5 and -35 dB, extrapolated to -60 dB.
double rt60 (const std::vector<float>& h)
{
    std::vector<double> edc (h.size() + 1, 0.0);
    for (size_t i = h.size(); i-- > 0;)
        edc[i] = edc[i + 1] + (double) h[i] * h[i];
    const double total = edc[0];
    double t5 = -1.0, t35 = -1.0;
    for (size_t i = 0; i < h.size(); ++i)
    {
        const double db = 10.0 * std::log10 (std::max (1.0e-300, edc[i] / total));
        if (t5 < 0.0 && db <= -5.0)
            t5 = (double) i / kRate;
        if (t35 < 0.0 && db <= -35.0)
        {
            t35 = (double) i / kRate;
            break;
        }
    }
    return t5 >= 0.0 && t35 > t5 ? (t35 - t5) * 2.0 : 0.0;
}

double cpuPercent (const char* type, double seconds)
{
    auto fx = make (type);
    const auto in = noise (-6.0, (size_t) (seconds * kRate), 11);
    const auto t0 = std::chrono::steady_clock::now();
    const auto out = run (*fx, in);
    const auto t1 = std::chrono::steady_clock::now();
    check (finite (out), std::string (type) + ": finite output in the CPU run");
    const double used = std::chrono::duration<double> (t1 - t0).count();
    return 100.0 * used / seconds;
}
} // namespace

// ---- Delay ----

FX_TEST ("delay: 1/4 at 120 BPM gives the echo 24000 +-1 samples after the impulse (spec test 6)")
{
    auto fx = make ("delay");
    fx->setTransport ({ 120.0, 0.0, true });
    set (*fx, "mix", 100.0f);
    set (*fx, "feedback", 0.0f);
    fx->reset();
    const size_t at = 1000;
    const auto out = run (*fx, impulse (48000, at));
    for (auto* ch : { &out.l, &out.r })
    {
        const size_t p = peakIndex (*ch, 0, ch->size());
        const long offset = (long) p - (long) at;
        check (std::abs (offset - 24000) <= 1, "echo " + std::to_string (offset) + " samples after the impulse");
        check (peak (*ch, 0, at + 23990) < 1.0e-6, "nothing before the echo");
    }

    // The same with the time in milliseconds: 500 ms, synced off.
    set (*fx, "sync", 0.0f);
    set (*fx, "timeL", 500.0f);
    fx->reset();
    const auto ms = run (*fx, impulse (48000, at));
    check (std::abs ((long) peakIndex (ms.l, 0, ms.size()) - (long) at - 24000) <= 1, "500 ms gives 24000 samples");
}

FX_TEST ("delay: changing the time while it sounds gives no jump larger than 0.1 (spec test 6)")
{
    for (int variant = 0; variant < 3; ++variant)
    {
        auto fx = make ("delay");
        set (*fx, "mix", 100.0f);
        set (*fx, "feedback", 50.0f);
        if (variant == 2)
            set (*fx, "modDepth", 50.0f);
        fx->reset();
        const auto in = sine (440.0, linToDb (0.5), 4 * 48000);
        // Several changes while the echoes ring: division, then milliseconds, then link off with a new right time.
        Stereo out = in;
        for (size_t pos = 0, block = 0; pos < in.size(); pos += (size_t) kBlock, ++block)
        {
            if (pos >= 48000 && block % 40 == 0)
            {
                const float k = (float) ((block / 40) % 5);
                if (variant == 0)
                    set (*fx, "divL", 3.0f + k * 2.0f);
                else
                {
                    set (*fx, "sync", 0.0f);
                    set (*fx, "link", 0.0f);
                    set (*fx, "timeL", 100.0f + 90.0f * k);
                    set (*fx, "timeR", 470.0f - 60.0f * k);
                }
            }
            const int n = (int) std::min ((size_t) kBlock, in.size() - pos);
            float* io[2] { out.l.data() + pos, out.r.data() + pos };
            fx->process (io, 2, n, nullptr);
        }
        const double step = std::max (maxStep (out.l), maxStep (out.r));
        check (step <= 0.1, "variant " + std::to_string (variant) + ": largest sample-to-sample step " + std::to_string (step));
        check (rms (out.l, 3 * 48000) > 0.1, "variant " + std::to_string (variant) + ": the echoes still sound");
    }
}

FX_TEST ("delay: ping-pong alternates the echoes between the channels")
{
    auto fx = make ("delay");
    set (*fx, "mix", 100.0f);
    set (*fx, "sync", 0.0f);
    set (*fx, "timeL", 100.0f);
    set (*fx, "feedback", 50.0f);
    set (*fx, "pingPong", 1.0f);
    fx->reset();
    const auto out = run (*fx, burst (48000, 240, 0.5f));
    for (int k = 1; k <= 6; ++k)
    {
        const size_t from = (size_t) k * 4800, to = from + 480;
        const double el = energy (out.l, from, to), er = energy (out.r, from, to);
        const bool left = (k % 2) == 1;
        const double ours = left ? el : er, other = left ? er : el;
        check (ours > 1.0e-6 && linToDb (std::sqrt (other / ours)) < -60.0,
               "echo " + std::to_string (k) + " on the " + (left ? "left" : "right") + ", other side "
                   + std::to_string (linToDb (std::sqrt (other / std::max (ours, 1.0e-30)))) + " dB");
    }
}

FX_TEST ("delay: feedback 50 % gives successive echoes -6 dB +-0.5")
{
    auto fx = make ("delay");
    set (*fx, "mix", 100.0f);
    set (*fx, "sync", 0.0f);
    set (*fx, "timeL", 100.0f);
    set (*fx, "feedback", 50.0f);
    set (*fx, "hp", 20.0f);
    set (*fx, "lp", 20000.0f);
    fx->reset();
    const auto out = run (*fx, burst (48000, 240, 0.5f));
    double previous = 0.0;
    for (int k = 1; k <= 6; ++k)
    {
        const size_t from = (size_t) k * 4800;
        const double level = std::sqrt (energy (out.l, from, from + 480));
        if (k > 1)
        {
            const double db = linToDb (level / previous);
            check (std::abs (db + 6.02) <= 0.5, "echo " + std::to_string (k) + " is " + std::to_string (db) + " dB from the previous");
        }
        previous = level;
    }
}

FX_TEST ("delay: tailSeconds covers the echoes down to -60 dB")
{
    for (const float fbPercent : { 0.0f, 50.0f, 80.0f })
    {
        auto fx = make ("delay");
        set (*fx, "mix", 100.0f);
        set (*fx, "sync", 0.0f);
        set (*fx, "timeL", 100.0f);
        set (*fx, "feedback", fbPercent);
        set (*fx, "hp", 20.0f);
        set (*fx, "lp", 20000.0f);
        fx->reset();
        const auto out = run (*fx, burst (20 * 48000, 240, 0.5f));
        const size_t tail = (size_t) (fx->tailSeconds() * kRate);
        const double first = peak (out.l, 0, 4800 + 480);
        const double after = peak (out.l, tail + 480);
        check (first > 0.1 && linToDb (after / first) < -59.0,
               "feedback " + std::to_string ((int) fbPercent) + " %: after " + std::to_string (fx->tailSeconds()) + " s the echoes are at "
                   + std::to_string (linToDb (after / first)) + " dB");
    }
}

FX_TEST ("delay: ducking lowers the echo while the input plays; freeze holds the echoes")
{
    auto fx = make ("delay");
    set (*fx, "mix", 100.0f);
    set (*fx, "sync", 0.0f);
    set (*fx, "timeL", 100.0f);
    set (*fx, "feedback", 90.0f);
    set (*fx, "ducking", 100.0f);
    fx->reset();
    // A steady tone keeps the duck closed; once it stops the echoes come back up.
    auto in = sine (440.0, -6.0, 96000);
    std::fill (in.l.begin() + 48000, in.l.end(), 0.0f);
    std::fill (in.r.begin() + 48000, in.r.end(), 0.0f);
    const auto out = run (*fx, in);
    const double during = rms (out.l, 24000, 48000), after = rms (out.l, 72000, 96000);
    check (after > 10.0 * during, "ducked " + std::to_string (linToDb (during)) + " dB, released " + std::to_string (linToDb (after)) + " dB");

    auto frozen = make ("delay");
    set (*frozen, "mix", 100.0f);
    set (*frozen, "sync", 0.0f);
    set (*frozen, "timeL", 250.0f);
    frozen->reset();
    const auto noisy = noise (-12.0, 5 * 48000, 4);
    const auto held = runWithChange (*frozen, noisy, 24000, [&] { set (*frozen, "freeze", 1.0f); });
    const double early = rms (held.l, 48000, 96000), late = rms (held.l, 192000, 240000);
    check (early > 0.01 && std::abs (linToDb (late / early)) < 0.5,
           "freeze holds: " + std::to_string (linToDb (early)) + " dB then " + std::to_string (linToDb (late)) + " dB");
}

// ---- Reverb ----

FX_TEST ("reverb: decay 2 s gives an RT60 (1 kHz octave, -5..-35 dB extrapolated) within +-25 %")
{
    for (const float size : { 20.0f, 60.0f, 100.0f })
    {
        auto fx = make ("reverb");
        set (*fx, "mix", 100.0f);
        set (*fx, "decay", 2.0f);
        set (*fx, "size", size);
        fx->reset();
        const auto ir = run (*fx, impulse (5 * 48000, 0));
        daw::fx::Biquad band;
        band.set (daw::fx::Biquad::Type::bandPass, 1000.0, 1.41, 0.0, kRate);
        std::vector<float> filtered (ir.size());
        for (size_t i = 0; i < ir.size(); ++i)
            filtered[i] = band.tick (ir.l[i]);
        const double t = rt60 (filtered);
        check (std::abs (t - 2.0) <= 0.5, "size " + std::to_string ((int) size) + ": RT60 " + std::to_string (t) + " s");
    }
}

FX_TEST ("reverb: the late tail has no periodic ringing (autocorrelation below 0.5 beyond 1 ms)")
{
    for (const float size : { 0.0f, 60.0f, 100.0f })
    {
        auto fx = make ("reverb");
        set (*fx, "mix", 100.0f);
        set (*fx, "size", size);
        fx->reset();
        const auto ir = run (*fx, impulse (2 * 48000, 0));
        const size_t from = 14400, n = 24000; // 0.3 .. 0.8 s
        double worst = 0.0;
        size_t worstLag = 0;
        for (size_t lag = 48; lag <= 6000; ++lag)
        {
            double xy = 0.0, xx = 0.0, yy = 0.0;
            for (size_t i = from; i < from + n; ++i)
            {
                const double a = ir.l[i], b = ir.l[i + lag];
                xy += a * b;
                xx += a * a;
                yy += b * b;
            }
            const double r = std::abs (xy) / std::sqrt (std::max (1.0e-300, xx * yy));
            if (r > worst)
            {
                worst = r;
                worstLag = lag;
            }
        }
        check (worst < 0.5, "size " + std::to_string ((int) size) + ": autocorrelation " + std::to_string (worst) + " at lag "
                                + std::to_string (worstLag) + " samples");
    }
}

FX_TEST ("reverb: freeze holds the level and mutes the input")
{
    auto fx = make ("reverb");
    set (*fx, "mix", 100.0f);
    fx->reset();
    // Noise keeps playing after freeze: a loop that still took input would keep growing.
    const auto in = noise (-12.0, 8 * 48000, 9);
    const auto out = runWithChange (*fx, in, 48000, [&] { set (*fx, "freeze", 1.0f); });
    const double early = rms (out.l, 2 * 48000, 3 * 48000), late = rms (out.l, 7 * 48000, 8 * 48000);
    check (early > 0.01 && std::abs (linToDb (late / early)) < 0.5,
           "frozen level " + std::to_string (linToDb (early)) + " dB then " + std::to_string (linToDb (late)) + " dB");
    check (fx->tailSeconds() >= 30.0, "frozen tail reported as the 30 s cap");

    // Released, it decays again.
    const auto released = runWithChange (*fx, Stereo (6 * 48000), 0, [&] { set (*fx, "freeze", 0.0f); });
    check (rms (released.l, 5 * 48000) < 0.01 * early, "released, the tail decays");

    // Frozen from the start (a project loaded with freeze on): nothing gets in, nothing breaks.
    auto cold = make ("reverb");
    set (*cold, "mix", 100.0f);
    set (*cold, "freeze", 1.0f);
    cold->reset();
    const auto silent = run (*cold, noise (-6.0, 48000, 10));
    check (finite (silent) && peak (silent.l) < 1.0e-6, "frozen from the start: the input is muted");
}

FX_TEST ("reverb: width 0 gives a mono wet signal; width 100 a decorrelated one")
{
    auto fx = make ("reverb");
    set (*fx, "mix", 100.0f);
    set (*fx, "width", 0.0f);
    fx->reset();
    const auto in = noise (-12.0, 48000, 12);
    const auto out = run (*fx, in);
    double diff = 0.0;
    for (size_t i = 0; i < out.size(); ++i)
        diff = std::max (diff, (double) std::abs (out.l[i] - out.r[i]));
    check (diff < 1.0e-6 && rms (out.l) > 0.01, "width 0: |L - R| at most " + std::to_string (diff));

    set (*fx, "width", 100.0f);
    fx->reset();
    const auto mono = noise (-12.0, 48000, 13);
    Stereo both = mono;
    both.r = both.l; // the same signal on both sides
    const auto wide = run (*fx, both);
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (size_t i = 4800; i < wide.size(); ++i)
    {
        lr += (double) wide.l[i] * wide.r[i];
        ll += (double) wide.l[i] * wide.l[i];
        rr += (double) wide.r[i] * wide.r[i];
    }
    const double corr = lr / std::sqrt (ll * rr);
    check (std::abs (corr) < 0.5, "width 100 on a mono input: L/R correlation " + std::to_string (corr));
}

FX_TEST ("reverb: pre-delay, sync and size do not click or blow up")
{
    auto fx = make ("reverb");
    set (*fx, "mix", 100.0f);
    set (*fx, "predelay", 100.0f);
    fx->reset();
    const auto ir = run (*fx, impulse (48000, 0));
    check (peak (ir.l, 0, 4700) < 1.0e-6 && peak (ir.l, 4800, 48000) > 1.0e-3, "nothing comes out before the 100 ms pre-delay");

    fx->setTransport ({ 120.0, 0.0, true });
    set (*fx, "predelaySync", 4.0f); // 1/8 at 120 BPM = 250 ms
    check (std::abs (fx->tailSeconds() - (2.5 + 0.25)) < 1.0e-6, "tail = decay + synced pre-delay");
    fx->reset();
    const auto synced = run (*fx, impulse (48000, 0));
    check (peak (synced.l, 0, 11900) < 1.0e-6 && peak (synced.l, 12000, 48000) > 1.0e-3, "1/8 at 120 BPM delays the reverb by 250 ms");

    // Sweeping the size and the pre-delay on a sine must not produce steps much larger than the sine itself does.
    auto sweep = make ("reverb");
    set (*sweep, "mix", 100.0f);
    sweep->reset();
    const auto in = sine (220.0, -12.0, 3 * 48000);
    Stereo out = in;
    for (size_t pos = 0, block = 0; pos < in.size(); pos += (size_t) kBlock, ++block)
    {
        set (*sweep, "size", (float) (50.0 + 50.0 * std::sin ((double) block * 0.05)));
        set (*sweep, "predelay", (float) (block % 100 < 50 ? 20.0 : 120.0));
        const int n = (int) std::min ((size_t) kBlock, in.size() - pos);
        float* io[2] { out.l.data() + pos, out.r.data() + pos };
        sweep->process (io, 2, n, nullptr);
    }
    check (finite (out) && peak (out.l) < 4.0, "sweeping stays finite and bounded");
    check (maxStep (out.l) < 0.1, "sweeping gives no clicks (largest step " + std::to_string (maxStep (out.l)) + ")");
}

FX_TEST ("space: mono buffers and other sample rates work")
{
    for (const double sr : { 44100.0, 96000.0, 192000.0 })
        for (const char* type : { "reverb", "delay" })
        {
            auto fx = make (type, sr, 512);
            set (*fx, "mix", 100.0f);
            fx->reset();
            auto in = noise (-6.0, (size_t) sr, 21);
            for (size_t pos = 0; pos < in.size(); pos += 512)
            {
                float* io[1] { in.l.data() + pos };
                fx->process (io, 1, (int) std::min ((size_t) 512, in.size() - pos), nullptr);
            }
            bool ok = true;
            for (float v : in.l)
                ok = ok && std::isfinite (v) && std::abs (v) < 4.0f;
            check (ok && rms (in.l, in.size() / 2) > 1.0e-3,
                   std::string (type) + " at " + std::to_string ((int) sr) + " Hz, mono: finite and sounding");
        }
}

// ---- CPU ----

FX_TEST ("space CPU: 10 s of stereo noise at 48 kHz (delay < 1 %, reverb < 3 % of real time)")
{
    const double delay = cpuPercent ("delay", 10.0), reverb = cpuPercent ("reverb", 10.0);
    std::printf ("  delay  %.3f %% of real time\n  reverb %.3f %% of real time\n", delay, reverb);
#if defined (NDEBUG) && defined (DAW_STRICT_CPU_TESTS)
    check (delay < 1.0, "delay CPU " + std::to_string (delay) + " %");
    check (reverb < 3.0, "reverb CPU " + std::to_string (reverb) + " %");
#endif
}
