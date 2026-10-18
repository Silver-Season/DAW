// EQ Eight: spec test 2 (docs/spec/06-effects-mixing.md), channel modes, glides without zipper noise, CPU.
#include "FxTest.h"

#include <chrono>
#include <complex>

using namespace fxtest;

namespace
{
// The RBJ cookbook peaking filter evaluated on the unit circle: the analytic response the bell must match.
double rbjBellDb (double hz, double centre, double gainDb, double q, double rate = kRate)
{
    const double a = std::pow (10.0, gainDb / 40.0), w0 = 2.0 * std::numbers::pi * centre / rate;
    const double alpha = std::sin (w0) / (2.0 * q), c = std::cos (w0);
    const double b0 = 1.0 + alpha * a, b1 = -2.0 * c, b2 = 1.0 - alpha * a;
    const double a0 = 1.0 + alpha / a, a1 = -2.0 * c, a2 = 1.0 - alpha / a;
    const auto z = std::polar (1.0, -2.0 * std::numbers::pi * hz / rate);
    return linToDb (std::abs ((b0 + b1 * z + b2 * z * z) / (a0 + a1 * z + a2 * z * z)));
}

// Gain in dB of the effect at `hz`, measured on the left channel by playing a sine through it.
double measuredDb (daw::fx::Effect& fx, double hz, size_t length = 48000)
{
    fx.reset();
    const auto in = sine (hz, -20.0, length);
    const auto out = run (fx, in);
    const size_t n = length / 2;
    return linToDb (toneAmplitude (out.l, hz, length - n, n) / toneAmplitude (in.l, hz, length - n, n));
}

double tone (const std::vector<float>& x, double hz) { return linToDb (toneAmplitude (x, hz, x.size() / 2, x.size() / 2)); }

// The largest second difference: a sine of amplitude A at w rad/sample has A w^2, a click has far more.
double roughness (const std::vector<float>& x)
{
    double worst = 0.0;
    for (size_t i = 2; i < x.size(); ++i)
        worst = std::max (worst, (double) std::abs (x[i] - 2.0f * x[i - 1] + x[i - 2]));
    return worst;
}
} // namespace

FX_TEST ("eq8: bell +6 dB at 1 kHz, Q 1 matches the analytic response within 0.1 dB (measured with sines)")
{
    auto fx = make ("eq8");
    set (*fx, "b4type", 5.0f);
    set (*fx, "b4freq", 1000.0f);
    set (*fx, "b4gain", 6.0f);
    set (*fx, "b4q", 1.0f);
    const float freqs[] { 50.0f, 200.0f, 500.0f, 707.0f, 1000.0f, 1414.0f, 2000.0f, 5000.0f, 15000.0f };
    float curve[std::size (freqs)];
    check (fx->response (freqs, curve, (int) std::size (freqs)), "eq8 has a response curve");
    for (size_t i = 0; i < std::size (freqs); ++i)
    {
        const double expected = rbjBellDb (freqs[i], 1000.0, 6.0, 1.0);
        const double got = measuredDb (*fx, freqs[i]);
        check (std::abs (got - expected) <= 0.1, "bell at " + std::to_string (freqs[i]) + " Hz: measured " + std::to_string (got)
                                                     + " dB, analytic " + std::to_string (expected));
        check (std::abs (curve[i] - expected) <= 0.01, "bell curve at " + std::to_string (freqs[i]) + " Hz: " + std::to_string (curve[i]));
    }
    check (std::abs (measuredDb (*fx, 1000.0) - 6.0) <= 0.1, "bell is +6 dB at its centre");
}

FX_TEST ("eq8: low cut 48 dB at 100 Hz gives -48 +-1 dB at 50 Hz, steeper slopes cut more")
{
    auto fx = make ("eq8");
    set (*fx, "b1on", 1.0f);
    set (*fx, "b1freq", 100.0f);
    set (*fx, "b1q", 0.7071f);
    const double expected[] { -7.0, -12.3, -24.1, -48.2 }; // Butterworth 1st, 2nd, 4th, 8th order at half the cutoff
    for (int slope = 0; slope < 4; ++slope)
    {
        set (*fx, "b1type", (float) slope);
        const double got = measuredDb (*fx, 50.0);
        check (std::abs (got - expected[slope]) <= 1.0, "low cut type " + std::to_string (slope) + " at 50 Hz: " + std::to_string (got) + " dB");
        check (std::abs (measuredDb (*fx, 1000.0)) <= 0.1, "low cut type " + std::to_string (slope) + " leaves 1 kHz alone");
    }
    const float at50 = 50.0f;
    float db = 0.0f;
    fx->response (&at50, &db, 1);
    check (std::abs (db + 48.0f) <= 1.0f, "low cut 48 curve at 50 Hz: " + std::to_string (db));

    // The mirror image: high cut 48 at 5 kHz is -48 dB an octave up.
    set (*fx, "b1on", 0.0f);
    set (*fx, "b8on", 1.0f);
    set (*fx, "b8type", 11.0f);
    set (*fx, "b8freq", 5000.0f);
    set (*fx, "b8q", 0.7071f);
    // The bilinear transform squeezes the octave towards Nyquist: the analog prototype sees tan (pi f / fs) ratios.
    const double ratio = std::tan (std::numbers::pi * 10000.0 / kRate) / std::tan (std::numbers::pi * 5000.0 / kRate);
    const double hcExpected = -10.0 * std::log10 (1.0 + std::pow (ratio, 16.0));
    const double hc = measuredDb (*fx, 10000.0);
    check (std::abs (hc - hcExpected) <= 1.0, "high cut 48 at 10 kHz: " + std::to_string (hc) + " dB, expected " + std::to_string (hcExpected));
}

FX_TEST ("eq8: default settings are transparent, a notch removes its frequency")
{
    auto fx = make ("eq8");
    for (const double hz : { 40.0, 300.0, 3000.0, 15000.0 })
        check (std::abs (measuredDb (*fx, hz)) <= 0.01, "default EQ is flat at " + std::to_string (hz));
    set (*fx, "b3type", 6.0f);
    set (*fx, "b3freq", 1000.0f);
    set (*fx, "b3q", 4.0f);
    check (measuredDb (*fx, 1000.0) < -40.0, "notch removes 1 kHz");
    check (std::abs (measuredDb (*fx, 4000.0)) < 0.2, "notch leaves 4 kHz");
}

FX_TEST ("eq8: Mid/Side applies a band to mid or side only, Left/Right to one channel")
{
    auto fx = make ("eq8");
    set (*fx, "mode", 2.0f);
    set (*fx, "b4type", 5.0f);
    set (*fx, "b4freq", 1000.0f);
    set (*fx, "b4gain", 12.0f);
    set (*fx, "b4q", 1.0f);
    set (*fx, "b4ch", 2.0f); // side
    fx->reset();

    const auto mono = sine (1000.0, -20.0, 24000);
    auto side = mono;
    for (auto& v : side.r)
        v = -v;

    auto out = run (*fx, mono);
    check (std::abs (tone (out.l, 1000.0) + 20.0) < 0.05 && std::abs (tone (out.r, 1000.0) + 20.0) < 0.05,
           "side band leaves a mono signal alone: " + std::to_string (tone (out.l, 1000.0)));
    fx->reset();
    out = run (*fx, side);
    check (std::abs (tone (out.l, 1000.0) + 8.0) < 0.1 && std::abs (tone (out.r, 1000.0) + 8.0) < 0.1,
           "side band boosts the side signal by 12 dB: " + std::to_string (tone (out.l, 1000.0)));

    set (*fx, "b4ch", 1.0f); // mid
    fx->reset();
    out = run (*fx, mono);
    check (std::abs (tone (out.l, 1000.0) + 8.0) < 0.1, "mid band boosts the mono signal: " + std::to_string (tone (out.l, 1000.0)));
    fx->reset();
    out = run (*fx, side);
    check (std::abs (tone (out.l, 1000.0) + 20.0) < 0.05, "mid band leaves the side signal: " + std::to_string (tone (out.l, 1000.0)));

    set (*fx, "mode", 1.0f); // left/right, band on the left
    fx->reset();
    out = run (*fx, mono);
    check (std::abs (tone (out.l, 1000.0) + 8.0) < 0.1 && std::abs (tone (out.r, 1000.0) + 20.0) < 0.05,
           "left band boosts only the left channel: " + std::to_string (tone (out.l, 1000.0)) + " / " + std::to_string (tone (out.r, 1000.0)));

    set (*fx, "mode", 0.0f); // stereo ignores the channel choice
    fx->reset();
    out = run (*fx, mono);
    check (std::abs (tone (out.r, 1000.0) + 8.0) < 0.1, "stereo mode applies the band to both channels");

    // Switching the mode while playing fades instead of clicking.
    set (*fx, "mode", 2.0f);
    fx->reset();
    const auto playing = sine (1000.0, -6.0, 256 * 188);
    Stereo res (playing.size());
    for (size_t pos = 0; pos < playing.size(); pos += 256)
    {
        if (pos == 24064)
            set (*fx, "mode", 0.0f);
        Stereo part (256);
        std::copy (playing.l.begin() + (long) pos, playing.l.begin() + (long) pos + 256, part.l.begin());
        std::copy (playing.r.begin() + (long) pos, playing.r.begin() + (long) pos + 256, part.r.begin());
        const auto o = run (*fx, part);
        std::copy (o.l.begin(), o.l.end(), res.l.begin() + (long) pos);
    }
    // The boosted tone has amplitude 2, so 0.034 is the sine itself; an instant switch gives 0.11.
    check (roughness (res.l) < 0.04, "mode switch while playing is smooth: " + std::to_string (roughness (res.l)));
}

FX_TEST ("eq8: sweeping and jumping the frequency, gain and type glide without clicks")
{
    // Reference: a 1 kHz sine at -12 dBFS boosted by up to 12 dB has a second difference of at most 1.0 * w^2 = 0.0171;
    // without the glides the jumps below reach 0.03 to 0.04.
    const auto in = sine (1000.0, -12.0, 96000);
    auto runAutomated = [&] (auto&& automate)
    {
        auto fx = make ("eq8");
        set (*fx, "b4type", 5.0f);
        set (*fx, "b4gain", 12.0f);
        set (*fx, "b4q", 2.0f);
        set (*fx, "b4freq", 100.0f);
        fx->reset();
        Stereo out = in;
        for (size_t pos = 0; pos < in.size(); pos += 64)
        {
            automate (*fx, pos);
            float* io[2] { out.l.data() + pos, out.r.data() + pos };
            fx->process (io, 2, 64, nullptr);
        }
        return out;
    };

    const auto sweep = runAutomated ([] (daw::fx::Effect& fx, size_t pos)
    {
        // 100 Hz to 10 kHz in one second, one value per 512-sample block, the way a host's automation arrives.
        if (pos % 512 != 0)
            return;
        const double t = std::min (1.0, (double) pos / kRate);
        fx.setParam (fx.paramIndex ("b4freq"), (float) (100.0 * std::pow (100.0, t)));
    });
    check (finite (sweep), "sweep gives finite output");
    check (roughness (sweep.l) < 0.02, "frequency sweep is smooth: worst second difference " + std::to_string (roughness (sweep.l)));
    check (peak (sweep.l) > 0.8, "the sweep passed the boost over the tone");

    const auto jump = runAutomated ([] (daw::fx::Effect& fx, size_t pos)
    {
        if (pos == 24000)
            fx.setParam (fx.paramIndex ("b4freq"), 1000.0f);
        if (pos == 48000)
            fx.setParam (fx.paramIndex ("b4gain"), -24.0f);
        if (pos == 72000)
            fx.setParam (fx.paramIndex ("b4type"), 6.0f); // to a notch
    });
    check (finite (jump), "jumps give finite output");
    check (roughness (jump.l) < 0.02, "jumps of frequency, gain and type glide: worst second difference " + std::to_string (roughness (jump.l)));
    const auto settled = linToDb (toneAmplitude (jump.l, 1000.0, 36000, 9600) / dbToLin (-12.0));
    check (std::abs (settled - 12.0) < 0.2, "after the frequency jump the tone is boosted by 12 dB: " + std::to_string (settled));
    check (peak (jump.l, 90000) < 0.01, "after the switch to a notch the tone is gone");

    const auto toggled = runAutomated ([] (daw::fx::Effect& fx, size_t pos)
    {
        if (pos == 1024)
            fx.setParam (fx.paramIndex ("b4freq"), 1000.0f);
        if (pos % 9600 == 0)
            fx.setParam (fx.paramIndex ("b4on"), (float) ((pos / 9600) % 2));
    });
    check (roughness (toggled.l) < 0.02, "switching a band on and off fades: worst second difference " + std::to_string (roughness (toggled.l)));
}

FX_TEST ("eq8: CPU of 8 active bands (two 48 dB cuts) on 10 s of stereo 48 kHz noise")
{
    auto fx = make ("eq8", kRate, 512);
    const float gains[] { 0.0f, 3.0f, -4.0f, 5.0f, -3.0f, 2.0f, -2.0f, 0.0f };
    for (int b = 1; b <= 8; ++b)
    {
        const auto id = "b" + std::to_string (b);
        set (*fx, (id + "on").c_str(), 1.0f);
        set (*fx, (id + "gain").c_str(), gains[b - 1]);
    }
    set (*fx, "b1type", 3.0f);
    set (*fx, "b8type", 11.0f);
    fx->reset();
    const auto in = noise (-6.0, (size_t) (10 * kRate), 11);
    const auto start = std::chrono::steady_clock::now();
    const auto out = run (*fx, in, 512);
    const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
    std::printf ("  [cpu] eq8: 10 s in %.1f ms = %.3f %% of real time\n", ms, ms / 100.0);
    check (finite (out), "eq8 output finite");
#if defined (NDEBUG) && defined (DAW_STRICT_CPU_TESTS)
    check (ms / 100.0 < 1.0, "eq8 under 1 % of real time");
#endif
}
