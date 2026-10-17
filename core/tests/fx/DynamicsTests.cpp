// Compressor, gate and limiter: spec tests 3 and 4 (docs/spec/06-effects-mixing.md), sidechain, hysteresis, CPU.
#include "FxTest.h"

#include <chrono>

using namespace fxtest;

namespace
{
double peakDb (const std::vector<float>& x, size_t from, size_t to = (size_t) -1) { return linToDb (peak (x, from, to)); }

// Gain reduction in dB at sample i, read from the output and the input sample (latency 0, Mix 100 %).
double reductionAt (const Stereo& in, const Stereo& out, size_t i) { return -linToDb (std::abs (out.l[i] / in.l[i])); }

// True peak of x[from, to) by 4x oversampling with a long (64 taps per phase) Blackman-Harris windowed sinc, independent of
// the limiter's own short interpolator.
double truePeakDb (const std::vector<float>& x, size_t from, size_t to)
{
    constexpr int half = 32;
    double weights[3][2 * half];
    for (int p = 1; p < 4; ++p)
        for (int k = -half + 1; k <= half; ++k)
        {
            const double t = p / 4.0 - k, r = (t + half) / (2.0 * half);
            const double window = 0.35875 - 0.48829 * std::cos (2 * std::numbers::pi * r) + 0.14128 * std::cos (4 * std::numbers::pi * r)
                                  - 0.01168 * std::cos (6 * std::numbers::pi * r);
            weights[p - 1][k + half - 1] = std::sin (std::numbers::pi * t) / (std::numbers::pi * t) * window;
        }
    double best = peak (x, from, to);
    for (size_t n = std::max (from, (size_t) half); n + half < std::min (to, x.size()); ++n)
        for (const auto& w : weights)
        {
            double y = 0.0;
            for (int k = -half + 1; k <= half; ++k)
                y += w[k + half - 1] * x[n + (size_t) k];
            best = std::max (best, std::abs (y));
        }
    return linToDb (best);
}

Stereo sineWithPhase (double hz, double db, size_t n, double phase)
{
    Stereo s (n);
    const float a = dbToLin (db);
    for (size_t i = 0; i < n; ++i)
        s.l[i] = s.r[i] = a * (float) std::sin (2.0 * std::numbers::pi * hz * (double) i / kRate + phase);
    return s;
}

void cpuCase (const char* type, double budgetPercent, void (*setup) (daw::fx::Effect&), double inputDb = -6.0)
{
    auto fx = make (type, kRate, 512);
    setup (*fx);
    fx->reset();
    const auto in = noise (inputDb, (size_t) (10 * kRate), 13);
    const auto sc = noise (-6.0, in.size(), 17);
    const auto start = std::chrono::steady_clock::now();
    const auto out = run (*fx, in, 512, fx->acceptsSidechain() ? &sc : nullptr);
    const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
    std::printf ("  [cpu] %s: 10 s in %.1f ms = %.3f %% of real time\n", type, ms, ms / 100.0);
    check (finite (out), std::string (type) + " output finite");
#ifdef NDEBUG
    check (ms / 100.0 < budgetPercent, std::string (type) + " under " + std::to_string (budgetPercent) + " % of real time");
#else
    (void) budgetPercent;
#endif
}
} // namespace

FX_TEST ("comp: sine -10 dBFS, threshold -20, ratio 4, knee 0 settles at -17.5 +-0.2 dBFS")
{
    auto fx = make ("comp");
    set (*fx, "threshold", -20.0f);
    set (*fx, "ratio", 4.0f);
    set (*fx, "knee", 0.0f);
    fx->reset();
    const auto out = run (*fx, sine (1000.0, -10.0, 48000));
    const double level = peakDb (out.l, 38400);
    check (std::abs (level + 17.5) <= 0.2, "steady level " + std::to_string (level) + " dBFS");
    check (std::abs (fx->readout (0) - 7.5f) <= 0.2f, "gain reduction readout " + std::to_string (fx->readout (0)));
    check (std::abs (fx->readout (1) + 10.0f) <= 0.2f, "input level readout " + std::to_string (fx->readout (1)));
}

FX_TEST ("comp: attack 10 ms reaches 63 +-5 % of the final gain reduction in 10 ms")
{
    auto fx = make ("comp");
    set (*fx, "threshold", -20.0f);
    set (*fx, "ratio", 4.0f);
    set (*fx, "knee", 0.0f);
    set (*fx, "attack", 10.0f);
    fx->reset();
    // 4 kHz: a waveform peak every 6 samples, at 477 and 483 around the 480th sample (10 ms).
    const auto in = sine (4000.0, -10.0, 48000);
    const auto out = run (*fx, in);
    const double at10ms = 0.5 * (reductionAt (in, out, 477) + reductionAt (in, out, 483));
    const double final = reductionAt (in, out, 47979);
    const double share = at10ms / final;
    check (std::abs (final - 7.5) < 0.2, "final gain reduction " + std::to_string (final) + " dB");
    check (std::abs (share - 0.632) <= 0.05, "after 10 ms " + std::to_string (100.0 * share) + " % of the final reduction");
}

FX_TEST ("comp: soft knee, infinite ratio, auto makeup, RMS detector")
{
    auto steady = [] (auto&& setup, double inputDb)
    {
        auto fx = make ("comp");
        set (*fx, "threshold", -20.0f);
        set (*fx, "ratio", 4.0f);
        set (*fx, "knee", 0.0f);
        setup (*fx);
        fx->reset();
        return peakDb (run (*fx, sine (1000.0, inputDb, 48000)).l, 38400);
    };
    // Soft knee 6 dB at the threshold: (1 - 1/4) * (6 / 2)^2 / (2 * 6) = 0.5625 dB of reduction.
    const double knee = steady ([] (auto& fx) { set (fx, "knee", 6.0f); }, -20.0);
    check (std::abs (knee + 20.5625) <= 0.05, "soft knee at the threshold: " + std::to_string (knee));
    const double below = steady ([] (auto& fx) { set (fx, "knee", 6.0f); }, -24.0);
    check (std::abs (below + 24.0) <= 0.01, "below the knee nothing happens: " + std::to_string (below));
    const double limit = steady ([] (auto& fx) { set (fx, "ratio", 100.0f); }, -10.0);
    check (std::abs (limit + 20.0) <= 0.2, "ratio 100 limits at the threshold: " + std::to_string (limit));
    // Auto makeup gives back half the reduction a 0 dBFS signal would get: 0.5 * 20 * 0.75 = 7.5 dB.
    const double made = steady ([] (auto& fx) { set (fx, "autoMakeup", 1.0f); }, -10.0);
    check (std::abs (made + 10.0) <= 0.2, "auto makeup: " + std::to_string (made));
    const double manual = steady ([] (auto& fx) { set (fx, "makeup", 3.0f); }, -10.0);
    check (std::abs (manual + 14.5) <= 0.2, "makeup 3 dB: " + std::to_string (manual));
    // RMS of a sine is 3.01 dB under its peak: (20 - 10 - 3.01) * 0.75 = 5.24 dB of reduction.
    const double rms = steady ([] (auto& fx) { set (fx, "detector", 1.0f); }, -10.0);
    check (std::abs (rms + 15.24) <= 0.3, "RMS detector: " + std::to_string (rms));
}

FX_TEST ("comp: a loud sidechain key ducks a quiet input, the key filter and listen work")
{
    auto fx = make ("comp");
    check (fx->acceptsSidechain(), "compressor takes a sidechain");
    set (*fx, "threshold", -20.0f);
    set (*fx, "ratio", 4.0f);
    set (*fx, "knee", 0.0f);
    set (*fx, "attack", 1.0f);
    set (*fx, "release", 200.0f);
    fx->reset();
    const auto in = sine (1000.0, -30.0, 48000);
    const auto plain = run (*fx, in);
    check (std::abs (peakDb (plain.l, 24000) + 30.0) < 0.01, "without a key the quiet input stays untouched");

    fx->reset();
    const auto key = sine (2000.0, -6.0, 48000); // (20 - 6) * 0.75 = 10.5 dB of reduction
    const auto ducked = run (*fx, in, kBlock, &key);
    const double level = peakDb (ducked.l, 24000);
    check (std::abs (level + 40.5) < 0.3, "the key ducks the input to " + std::to_string (level));
    check (std::abs (fx->readout (0) - 10.5f) < 0.3f, "reduction readout with the key " + std::to_string (fx->readout (0)));
    check (std::abs (fx->readout (1) + 30.0f) < 0.1f, "the input meter shows the input, not the key");

    // A 60 Hz key through the 500 Hz sidechain high-pass loses about 37 dB and stays under the threshold.
    set (*fx, "scHpf", 500.0f);
    fx->reset();
    const auto bass = sine (60.0, -6.0, 48000);
    const auto filtered = run (*fx, in, kBlock, &bass);
    check (std::abs (peakDb (filtered.l, 24000) + 30.0) < 0.2, "the key filter keeps the bass from ducking: " + std::to_string (peakDb (filtered.l, 24000)));

    set (*fx, "scListen", 1.0f);
    set (*fx, "scHpf", 20.0f);
    fx->reset();
    const auto heard = run (*fx, in, kBlock, &key);
    check (std::abs (peakDb (heard.l, 24000) + 6.0) < 0.05, "listen plays the key");
}

FX_TEST ("comp and gate: lookahead is the reported latency and moves the reduction ahead of the sound")
{
    for (const char* type : { "comp", "gate" })
    {
        auto fx = make (type);
        set (*fx, "lookahead", 5.0f);
        check (fx->latencySamples() == 240, std::string (type) + ": 5 ms lookahead = 240 samples, got " + std::to_string (fx->latencySamples()));
        fx->setParam (fx->paramIndex ("mix"), 0.0f);
        fx->reset();
        const auto out = run (*fx, impulse (4800, 100));
        check (std::abs (out.l[340] - 1.0f) < 1.0e-6f, std::string (type) + ": the dry impulse waits for the lookahead");
    }
    // The compressor with lookahead: a burst after silence is already reduced at its first sample.
    auto fx = make ("comp");
    set (*fx, "threshold", -30.0f);
    set (*fx, "ratio", 100.0f);
    set (*fx, "knee", 0.0f);
    set (*fx, "attack", 0.5f);
    set (*fx, "lookahead", 5.0f);
    fx->reset();
    Stereo in (24000);
    const auto burst = sine (1000.0, -6.0, 12000);
    std::copy (burst.l.begin(), burst.l.end(), in.l.begin() + 12000);
    std::copy (burst.r.begin(), burst.r.end(), in.r.begin() + 12000);
    const auto out = run (*fx, in);
    check (peakDb (out.l, 12240, 12240 + 48) < -20.0, "the first cycle of the burst is already compressed: " + std::to_string (peakDb (out.l, 12240, 12288)));
}

FX_TEST ("gate: opens above the threshold, holds between the thresholds (hysteresis), closes below")
{
    auto fx = make ("gate");
    set (*fx, "threshold", -40.0f);
    set (*fx, "hysteresis", 6.0f);
    set (*fx, "attack", 0.5f);
    set (*fx, "hold", 20.0f);
    set (*fx, "release", 10.0f);
    set (*fx, "floor", -80.0f);
    fx->reset();
    struct Step
    {
        double db;
        bool open;
        const char* what;
    };
    const Step steps[] { { -43.0, false, "-43 dB from closed stays closed" }, { -30.0, true, "-30 dB opens" },
                         { -43.0, true, "-43 dB after open stays open" },     { -50.0, false, "-50 dB closes" },
                         { -43.0, false, "-43 dB after closed stays closed" }, { -38.0, true, "-38 dB opens again" } };
    double previousDb = -120.0;
    for (const auto& s : steps)
    {
        const auto in = sine (1000.0, s.db, 14400);
        const auto out = run (*fx, in);
        const double gain = linToDb (toneAmplitude (out.l, 1000.0, 9600, 4800) / toneAmplitude (in.l, 1000.0, 9600, 4800));
        const bool open = fx->readout (2) > 0.5f;
        check (open == s.open, std::string (s.what) + ": open readout " + std::to_string (fx->readout (2)));
        if (s.open)
            check (std::abs (gain) < 0.05 && fx->readout (0) < 0.05f, std::string (s.what) + ": gain " + std::to_string (gain));
        else
            check (gain < -79.0 && fx->readout (0) > 79.0f, std::string (s.what) + ": gain " + std::to_string (gain));
        // The meter rises at once and falls about 29 dB per second, so only rising steps read their level right away.
        if (s.db >= previousDb)
            check (std::abs (fx->readout (1) - (float) s.db) < 0.3f, std::string (s.what) + ": input meter " + std::to_string (fx->readout (1)));
        else
            check (fx->readout (1) < (float) previousDb && fx->readout (1) > (float) s.db, std::string (s.what) + ": input meter falls");
        previousDb = s.db;
    }

    // Floor: a closed gate lets the floor through.
    set (*fx, "floor", -20.0f);
    fx->reset();
    const auto in = sine (1000.0, -60.0, 14400);
    const auto out = run (*fx, in);
    const double gain = linToDb (toneAmplitude (out.l, 1000.0, 9600, 4800) / toneAmplitude (in.l, 1000.0, 9600, 4800));
    check (std::abs (gain + 20.0) < 0.1, "floor -20 dB: closed gain " + std::to_string (gain));
}

FX_TEST ("gate: a sidechain key opens the gate for a quiet input; hold keeps it open")
{
    auto fx = make ("gate");
    check (fx->acceptsSidechain(), "gate takes a sidechain");
    set (*fx, "threshold", -30.0f);
    set (*fx, "hold", 0.0f);
    set (*fx, "release", 10.0f);
    fx->reset();
    const auto in = sine (500.0, -50.0, 24000);
    auto key = sine (3000.0, -10.0, 24000);
    std::fill (key.l.begin() + 12000, key.l.end(), 0.0f); // the key stops halfway
    std::fill (key.r.begin() + 12000, key.r.end(), 0.0f);
    const auto out = run (*fx, in, kBlock, &key);
    check (std::abs (peakDb (out.l, 4800, 12000) + 50.0) < 0.1, "open while the key plays: " + std::to_string (peakDb (out.l, 4800, 12000)));
    check (peakDb (out.l, 20000) < -120.0, "closed after the key stopped: " + std::to_string (peakDb (out.l, 20000)));

    // Hold 200 ms: still open 150 ms after the key stopped, closed after 300 ms.
    set (*fx, "hold", 200.0f);
    fx->reset();
    auto longIn = sine (500.0, -50.0, 48000);
    auto longKey = sine (3000.0, -10.0, 48000);
    std::fill (longKey.l.begin() + 12000, longKey.l.end(), 0.0f);
    std::fill (longKey.r.begin() + 12000, longKey.r.end(), 0.0f);
    const auto held = run (*fx, longIn, kBlock, &longKey);
    check (std::abs (peakDb (held.l, 12000 + 6000, 12000 + 7200) + 50.0) < 0.1, "hold keeps the gate open");
    check (peakDb (held.l, 12000 + 14400, 12000 + 15600) < -100.0, "closed after hold and release");
}

FX_TEST ("limiter: sine +6 dBFS gives a true peak at most ceiling + 0.1 dB (4x oversampled measurement)")
{
    // The measurement itself: a 12 kHz sine at 45 degrees has samples 3 dB under its peak; the oversampler must find it.
    const auto probe = sineWithPhase (12000.0, -6.0, 4800, std::numbers::pi / 4.0);
    check (std::abs (peakDb (probe.l, 0) + 9.01) < 0.05, "probe samples are 3 dB under the peak");
    check (std::abs (truePeakDb (probe.l, 0, probe.size()) + 6.0) < 0.02, "the test's oversampler finds the true peak");

    for (const float ceiling : { -0.3f, -1.0f, -6.0f })
        for (const double hz : { 60.0, 997.0, 7123.0, 12000.0, 16999.0, 20011.0 })
        {
            auto fx = make ("limiter");
            set (*fx, "ceiling", ceiling);
            fx->reset();
            const auto in = sineWithPhase (hz, 6.0, 48000, std::numbers::pi / 4.0);
            const auto out = run (*fx, in);
            const double tp = truePeakDb (out.l, 4800, out.size());
            const auto label = "ceiling " + std::to_string (ceiling) + ", " + std::to_string ((int) hz) + " Hz: true peak " + std::to_string (tp);
            check (tp <= ceiling + 0.1, label);
            check (tp >= ceiling - 1.0, label + " (not far under the ceiling)");
            check (peakDb (out.l, 0) <= ceiling + 1.0e-4, label + ", sample peak under the ceiling from the first sample");
        }
}

FX_TEST ("limiter: sample peak mode, gain, latency = lookahead, readout")
{
    auto fx = make ("limiter");
    check (fx->latencySamples() == 96, "default lookahead 2 ms = 96 samples, got " + std::to_string (fx->latencySamples()));
    set (*fx, "lookahead", 5.0f);
    check (fx->latencySamples() == 240, "5 ms = 240 samples");
    set (*fx, "truePeak", 0.0f);
    set (*fx, "gain", 12.0f);
    set (*fx, "ceiling", -1.0f);
    fx->reset();
    const auto in = noise (-6.0, 48000, 21);
    const auto out = run (*fx, in);
    check (peakDb (out.l, 0) <= -1.0 + 1.0e-4 && peakDb (out.r, 0) <= -1.0 + 1.0e-4, "sample peaks stay under the ceiling");
    check (peakDb (out.l, 24000) > -1.5, "and reach it");
    check (fx->readout (0) > 6.0f, "gain reduction readout " + std::to_string (fx->readout (0)));
    check (std::abs (fx->readout (1) - (float) peakDb (in.l, 40000)) < 3.0f, "input meter " + std::to_string (fx->readout (1)));

    // Below the ceiling the limiter is transparent (just delayed).
    auto clean = make ("limiter");
    const auto quiet = sine (1000.0, -12.0, 9600);
    const auto passed = run (*clean, quiet);
    double err = 0.0;
    for (size_t i = 96; i < quiet.size(); ++i)
        err = std::max (err, (double) std::abs (passed.l[i] - quiet.l[i - 96]));
    check (err < 1.0e-6, "a quiet signal passes unchanged: error " + std::to_string (err));
}

FX_TEST ("dynamics: CPU of compressor, gate and limiter on 10 s of stereo 48 kHz noise")
{
    cpuCase ("comp", 1.0, [] (daw::fx::Effect& fx)
    {
        set (fx, "threshold", -30.0f);
        set (fx, "scHpf", 100.0f);
        set (fx, "lookahead", 2.0f);
        set (fx, "autoRelease", 1.0f);
    });
    cpuCase ("gate", 1.0, [] (daw::fx::Effect& fx)
    {
        set (fx, "threshold", -12.0f);
        set (fx, "scHpf", 100.0f);
        set (fx, "lookahead", 2.0f);
    });
    cpuCase ("limiter", 3.0, [] (daw::fx::Effect& fx) { set (fx, "gain", 12.0f); });
}

FX_TEST ("eq8, comp, gate, limiter: mono and other sample rates give finite output, latency follows the rate")
{
    for (const char* type : { "eq8", "comp", "gate", "limiter" })
        for (const double rate : { 44100.0, 96000.0, 192000.0 })
        {
            auto fx = make (type, rate, 1024);
            if (fx->paramIndex ("lookahead") >= 0)
            {
                set (*fx, "lookahead", 5.0f);
                check (fx->latencySamples() == (int) std::lround (0.005 * rate), std::string (type) + ": lookahead at " + std::to_string ((int) rate));
            }
            auto in = noise (0.0, (size_t) rate / 2, 9);
            for (size_t pos = 0; pos + 1000 <= in.size(); pos += 1000)
            {
                float* chunk[1] { in.l.data() + pos };
                fx->process (chunk, 1, 1000, nullptr);
            }
            bool ok = true;
            for (const float v : in.l)
                ok = ok && std::isfinite (v) && std::abs (v) < 4.0f;
            check (ok, std::string (type) + ": mono at " + std::to_string ((int) rate) + " Hz is finite");
        }
}
