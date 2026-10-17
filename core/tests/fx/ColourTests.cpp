// Tests of the colour effects: drive, bitcrusher and filter (docs/spec/06-effects-mixing.md, task 6.2; spec test 5).
#include "FxTest.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <set>

using namespace fxtest;

namespace
{
const char* const kModeNames[] { "Soft", "Tape", "Tube", "Hard", "Fold", "Crush" };

// The level in dB of a `hz` sine through the effect in its current state, measured on the second half of one second.
double gainDb (daw::fx::Effect& fx, double hz, double levelDb = -20.0)
{
    fx.reset();
    const auto out = run (fx, sine (hz, levelDb, 48000));
    return linToDb (toneAmplitude (out.l, hz, 24000, 24000)) - levelDb;
}

// Largest step between neighbouring samples.
double largestStep (const std::vector<float>& x, size_t from = 1)
{
    double worst = 0.0;
    for (size_t i = from; i < x.size(); ++i)
        worst = std::max (worst, (double) std::abs (x[i] - x[i - 1]));
    return worst;
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Drive
// ---------------------------------------------------------------------------------------------------------------------

FX_TEST ("drive: a 15 kHz sine at drive 24 dB has no aliasing products above -80 dB (Soft and Hard, spec test 5)")
{
    // 15 kHz at 48 kHz repeats every 16 samples, so every component of the output is a multiple of 3 kHz. The harmonics of
    // 15 kHz (30 kHz and up) are removed by the filter; what is left in the band apart from 15 kHz is aliasing.
    for (const int mode : { 0, 3 })
    {
        for (const double level : { -12.0, -6.0, 0.0 })
        {
            auto fx = make ("drive");
            set (*fx, "mode", (float) mode);
            set (*fx, "drive", 24.0f);
            const auto out = run (*fx, sine (15000.0, level, 48000));
            const size_t from = 8000, n = 32000;
            const double fundamental = toneAmplitude (out.l, 15000.0, from, n);
            check (fundamental > 0.01, std::string (kModeNames[mode]) + ": the fundamental survives (" + std::to_string (fundamental) + ")");
            double worst = -300.0, worstAt = 0.0;
            for (const double hz : { 3000.0, 6000.0, 9000.0, 12000.0, 18000.0, 21000.0 })
            {
                const double db = linToDb (toneAmplitude (out.l, hz, from, n) / fundamental);
                if (db > worst)
                {
                    worst = db;
                    worstAt = hz;
                }
            }
            std::printf ("  drive %s at %+.0f dBFS: worst alias %.1f dB at %.0f Hz\n", kModeNames[mode], level, worst, worstAt);
            check (worst < -80.0, std::string (kModeNames[mode]) + " at " + std::to_string (level) + " dBFS: aliasing " + std::to_string (worst) + " dB at "
                                      + std::to_string (worstAt) + " Hz");
        }
    }
}

FX_TEST ("drive: level compensation keeps a -12 dBFS 1 kHz sine within +-6 dB of the input at drive 12 dB, in every mode")
{
    const double inputRms = std::pow (10.0, -12.0 / 20.0) / std::sqrt (2.0);
    for (int mode = 0; mode < 6; ++mode)
    {
        auto fx = make ("drive");
        set (*fx, "mode", (float) mode);
        set (*fx, "drive", 12.0f);
        fx->reset();
        const auto out = run (*fx, sine (1000.0, -12.0, 48000));
        const double db = linToDb (rms (out.l, 24000) / inputRms);
        std::printf ("  drive %s at 12 dB: %+.2f dB\n", kModeNames[mode], db);
        check (std::abs (db) <= 6.0, std::string (kModeNames[mode]) + ": output level " + std::to_string (db) + " dB against the input");
        check (finite (out), std::string (kModeNames[mode]) + ": finite");
    }
    // At the top of the range the compensation is the only thing that keeps a hard clip from being 20 dB louder.
    auto fx = make ("drive");
    set (*fx, "mode", 3.0f);
    set (*fx, "drive", 36.0f);
    const auto out = run (*fx, sine (1000.0, -12.0, 48000));
    check (std::abs (linToDb (rms (out.l, 24000) / inputRms)) <= 6.0, "hard clip at 36 dB: level " + std::to_string (linToDb (rms (out.l, 24000) / inputRms)));
}

FX_TEST ("drive: Soft, Tape and Tube bend a loud sine, and the six modes sound different")
{
    // Two drives, because at some levels two modes agree (Hard and Fold are the same curve until the signal passes 1).
    std::vector<std::vector<float>> outputs[2];
    for (int d = 0; d < 2; ++d)
        for (int mode = 0; mode < 6; ++mode)
        {
            auto fx = make ("drive");
            set (*fx, "mode", (float) mode);
            set (*fx, "drive", d == 0 ? 6.0f : 18.0f);
            outputs[d].push_back (run (*fx, sine (300.0, -6.0, 24000)).l);
        }
    for (int a = 0; a < 6; ++a)
        for (int b = a + 1; b < 6; ++b)
        {
            double diff = 0.0;
            for (int d = 0; d < 2; ++d)
                for (size_t i = 12000; i < outputs[d][(size_t) a].size(); ++i)
                    diff = std::max (diff, (double) std::abs (outputs[d][(size_t) a][i] - outputs[d][(size_t) b][i]));
            check (diff > 0.01, std::string (kModeNames[a]) + " and " + kModeNames[b] + " differ (" + std::to_string (diff) + ")");
        }
}

FX_TEST ("drive: symmetric shapers make odd harmonics only; Tube and Bias add even ones; the DC blocker removes the offset")
{
    auto harmonic = [] (int mode, float bias, int order, double* mean)
    {
        auto fx = make ("drive");
        set (*fx, "mode", (float) mode);
        set (*fx, "drive", 12.0f);
        set (*fx, "bias", bias);
        const auto out = run (*fx, sine (1000.0, -12.0, 96000));
        if (mean != nullptr)
        {
            double sum = 0.0;
            for (size_t i = 48000; i < 96000; ++i)
                sum += out.l[i];
            *mean = sum / 48000.0;
        }
        return linToDb (toneAmplitude (out.l, 1000.0 * order, 48000, 48000) / toneAmplitude (out.l, 1000.0, 48000, 48000));
    };
    check (harmonic (0, 0.0f, 2, nullptr) < -90.0, "Soft: second harmonic " + std::to_string (harmonic (0, 0.0f, 2, nullptr)) + " dB");
    check (harmonic (0, 0.0f, 3, nullptr) > -40.0, "Soft: third harmonic present");
    check (harmonic (2, 0.0f, 2, nullptr) > -40.0, "Tube: second harmonic " + std::to_string (harmonic (2, 0.0f, 2, nullptr)) + " dB");
    double mean = 1.0;
    check (harmonic (0, 60.0f, 2, &mean) > -40.0, "Soft with Bias: second harmonic");
    check (std::abs (mean) < 1.0e-3, "Bias: no DC at the output (" + std::to_string (mean) + ")");
}

FX_TEST ("drive: Tone tilts the spectrum around 1 kHz and 0 is neutral")
{
    auto fx = make ("drive");
    set (*fx, "mode", 0.0f);
    set (*fx, "drive", 0.0f);
    const double lowFlat = gainDb (*fx, 100.0, -40.0), highFlat = gainDb (*fx, 8000.0, -40.0);
    check (std::abs(lowFlat - highFlat) < 0.5, "Tone 0 is flat (" + std::to_string (lowFlat) + " / " + std::to_string (highFlat) + " dB)");
    set (*fx, "tone", 100.0f);
    const double lowUp = gainDb (*fx, 100.0, -40.0), highUp = gainDb (*fx, 8000.0, -40.0);
    check (highUp - lowUp > 8.0, "Tone +100 brightens (" + std::to_string (lowUp) + " / " + std::to_string (highUp) + " dB)");
    set (*fx, "tone", -100.0f);
    const double lowDown = gainDb (*fx, 100.0, -40.0), highDown = gainDb (*fx, 8000.0, -40.0);
    check (lowDown - highDown > 8.0, "Tone -100 darkens (" + std::to_string (lowDown) + " / " + std::to_string (highDown) + " dB)");
}

FX_TEST ("drive: a small signal passes flat from 100 Hz to 20 kHz (the oversampler and the anti-aliasing cost no treble)")
{
    auto fx = make ("drive");
    set (*fx, "mode", 0.0f);
    set (*fx, "drive", 0.0f);
    for (const double hz : { 100.0, 1000.0, 5000.0, 10000.0, 15000.0, 18000.0, 20000.0 })
    {
        const double db = gainDb (*fx, hz, -40.0);
        check (std::abs (db) < 0.5, std::to_string ((int) hz) + " Hz at " + std::to_string (db) + " dB");
    }
}

FX_TEST ("drive: the result does not depend on the block size, mono works, and both channels agree")
{
    const auto in = noise (-12.0, 9600, 21);
    Stereo reference;
    for (const int block : { 256, 1, 7, 100, 1000 })
    {
        auto fx = make ("drive");
        set (*fx, "drive", 18.0f);
        set (*fx, "tone", 30.0f);
        set (*fx, "bias", 20.0f);
        const auto out = run (*fx, in, block);
        check (finite (out), "block " + std::to_string (block) + ": finite");
        if (block == 256)
        {
            reference = out;
            continue;
        }
        double diff = 0.0;
        for (size_t i = 0; i < in.size(); ++i)
            diff = std::max (diff, (double) std::abs (out.l[i] - reference.l[i]));
        check (diff < 1.0e-4, "block " + std::to_string (block) + ": same output as block 256 (diff " + std::to_string (diff) + ")");
    }
    // Identical channels in, identical channels out.
    auto fx = make ("drive");
    set (*fx, "drive", 20.0f);
    auto mono = noise (-12.0, 4800, 4);
    mono.r = mono.l;
    const auto out = run (*fx, mono);
    double diff = 0.0;
    for (size_t i = 0; i < out.size(); ++i)
        diff = std::max (diff, (double) std::abs (out.l[i] - out.r[i]));
    check (diff == 0.0, "left and right agree for identical input");
    // One channel on its own gives what the left channel gives in stereo.
    auto fxMono = make ("drive");
    set (*fxMono, "drive", 20.0f);
    std::vector<float> m = mono.l;
    for (size_t pos = 0; pos < m.size(); pos += 256)
    {
        float* io[1] { m.data() + pos };
        fxMono->process (io, 1, (int) std::min<size_t> (256, m.size() - pos));
    }
    double monoDiff = 0.0;
    for (size_t i = 0; i < m.size(); ++i)
        monoDiff = std::max (monoDiff, (double) std::abs (m[i] - out.l[i]));
    check (monoDiff < 1.0e-6, "mono processing equals the left channel of stereo (" + std::to_string (monoDiff) + ")");
}

FX_TEST ("drive: extreme settings stay finite and bounded, silence in is silence out")
{
    for (int mode = 0; mode < 6; ++mode)
    {
        auto fx = make ("drive");
        set (*fx, "mode", (float) mode);
        set (*fx, "drive", 36.0f);
        set (*fx, "input", 24.0f);
        set (*fx, "bias", 100.0f);
        set (*fx, "tone", -100.0f);
        const auto out = run (*fx, noise (0.0, 24000, 5));
        check (finite (out), std::string (kModeNames[mode]) + ": finite at the extremes");
        check (peak (out.l) <= 4.0, std::string (kModeNames[mode]) + ": bounded at the extremes (" + std::to_string (peak (out.l)) + ")");
        fx->reset();
        const auto quiet = run (*fx, Stereo (9600));
        check (peak (quiet.l) < 1.0e-6, std::string (kModeNames[mode]) + ": silence stays silent");
    }
}

FX_TEST ("drive, crush, filter: NaN, Inf and huge values in the input do not crash, and reset recovers")
{
    for (const char* id : { "drive", "crush", "filter" })
        for (int mode = 0; mode < (std::string (id) == "drive" ? 6 : 1); ++mode)
        {
            auto fx = make (id);
            if (std::string (id) == "drive")
                set (*fx, "mode", (float) mode);
            Stereo bad = noise (-6.0, 2048, 3);
            bad.l[100] = std::numeric_limits<float>::quiet_NaN();
            bad.r[300] = std::numeric_limits<float>::infinity();
            bad.l[500] = -std::numeric_limits<float>::infinity();
            bad.r[700] = 3.0e38f;
            bad.l[900] = -3.0e38f;
            run (*fx, bad);
            fx->reset();
            const auto fine = run (*fx, noise (-6.0, 4800, 4));
            check (finite (fine), std::string (id) + " " + std::to_string (mode) + ": finite again after reset");
        }
}

FX_TEST ("drive: reports the latency of its oversampler and keeps it in every mode")
{
    for (int mode = 0; mode < 6; ++mode)
    {
        auto fx = make ("drive");
        set (*fx, "mode", (float) mode);
        check (fx->latencySamples() == 48, std::string (kModeNames[mode]) + ": latency " + std::to_string (fx->latencySamples()));
        // The wet impulse peaks at the latency: Mix 100, a small impulse so that the shaper is nearly linear.
        set (*fx, "drive", 0.0f);
        const auto out = run (*fx, impulse (4800, 100, 0.01f));
        size_t at = 0;
        float best = 0.0f;
        for (size_t i = 0; i < out.size(); ++i)
            if (std::abs (out.l[i]) > best)
            {
                best = std::abs (out.l[i]);
                at = i;
            }
        if (mode != 5) // the staircase of Crush mode swallows a 0.01 impulse
            check ((int) at - 100 == fx->latencySamples(), std::string (kModeNames[mode]) + ": the impulse peaks " + std::to_string ((int) at - 100) + " samples late");
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Bitcrusher
// ---------------------------------------------------------------------------------------------------------------------

FX_TEST ("crush: bits 4 gives at most 16 distinct levels, and a ramp uses all of them")
{
    auto fx = make ("crush");
    set (*fx, "bits", 4.0f);
    set (*fx, "rate", 48000.0f);
    const auto out = run (*fx, sine (997.0, -0.1, 48000));
    std::set<float> levels (out.l.begin(), out.l.end());
    check (levels.size() <= 16, "a sine has " + std::to_string (levels.size()) + " levels at 4 bits");
    check (levels.size() >= 14, "and most of them are used (" + std::to_string (levels.size()) + ")");

    fx->reset();
    Stereo ramp (9600);
    for (size_t i = 0; i < ramp.size(); ++i)
        ramp.l[i] = ramp.r[i] = -1.0f + 2.0f * (float) i / (float) ramp.size();
    const auto stairs = run (*fx, ramp);
    std::set<float> rampLevels (stairs.l.begin(), stairs.l.end());
    check (rampLevels.size() == 16, "a ramp has exactly 16 levels at 4 bits (" + std::to_string (rampLevels.size()) + ")");

    for (const int bits : { 1, 2, 3, 8 })
    {
        set (*fx, "bits", (float) bits);
        fx->reset();
        const auto r = run (*fx, ramp);
        std::set<float> l (r.l.begin(), r.l.end());
        const size_t expected = bits == 1 ? 3 : (size_t) 1 << bits;
        check (l.size() == expected, std::to_string (bits) + " bits: " + std::to_string (l.size()) + " levels, expected " + std::to_string (expected));
        check (l.count (0.0f) == 1, std::to_string (bits) + " bits: zero is a level");
    }
}

FX_TEST ("crush: 24 bits is transparent, 1 bit keeps silence silent")
{
    auto fx = make ("crush");
    set (*fx, "bits", 24.0f);
    set (*fx, "rate", 48000.0f);
    const auto in = noise (-6.0, 9600, 3);
    const auto out = run (*fx, in);
    double err = 0.0;
    for (size_t i = 0; i < in.size(); ++i)
        err = std::max (err, (double) std::abs (out.l[i] - in.l[i]));
    check (err < 1.0e-6, "24 bits error " + std::to_string (err));
    set (*fx, "bits", 1.0f);
    fx->reset();
    check (peak (run (*fx, Stereo (4800)).l) == 0.0, "1 bit: silence out");
}

FX_TEST ("crush: rate 1000 Hz holds each value for 48 samples; 48 kHz holds nothing; fractional rates average out")
{
    auto fx = make ("crush");
    set (*fx, "bits", 24.0f);
    set (*fx, "rate", 1000.0f);
    fx->reset();
    // Every input sample is different, so every change of the output is a new hold.
    Stereo in (48000);
    for (size_t i = 0; i < in.size(); ++i)
        in.l[i] = in.r[i] = 0.8f * (float) std::sin (0.01 * (double) i * (double) i * 0.001 + 0.3);
    const auto out = run (*fx, in);
    size_t runLength = 1, shortest = 1000, longest = 0, runs = 0;
    for (size_t i = 1; i < out.size(); ++i)
    {
        if (out.l[i] == out.l[i - 1])
            ++runLength;
        else
        {
            if (runs > 0) // the first run is cut by the start
            {
                shortest = std::min (shortest, runLength);
                longest = std::max (longest, runLength);
            }
            ++runs;
            runLength = 1;
        }
    }
    check (shortest == 48 && longest == 48, "runs of " + std::to_string (shortest) + " to " + std::to_string (longest) + " samples");
    check (std::abs (out.l[0] - in.l[0]) < 1.0e-6f, "the first sample is taken at once");
    check (std::abs (out.l[48] - in.l[48]) < 1.0e-6f && std::abs (out.l[96] - in.l[96]) < 1.0e-6f, "and then every 48th");

    set (*fx, "rate", 48000.0f);
    fx->reset();
    const auto full = run (*fx, in);
    double err = 0.0;
    for (size_t i = 0; i < in.size(); ++i)
        err = std::max (err, (double) std::abs (full.l[i] - in.l[i]));
    check (err < 1.0e-6, "rate 48 kHz passes every sample (" + std::to_string (err) + ")");

    set (*fx, "rate", 11025.0f);
    fx->reset();
    const auto frac = run (*fx, in);
    size_t changes = 0;
    for (size_t i = 1; i < frac.size(); ++i)
        changes += frac.l[i] != frac.l[i - 1] ? 1 : 0;
    const double expected = 11025.0;
    check (std::abs ((double) changes - expected) < 3.0, "11025 Hz takes " + std::to_string (changes) + " samples in a second, expected " + std::to_string (expected));
}

FX_TEST ("crush: dither adds a steady hiss of about one step, and without it silence is silent")
{
    auto fx = make ("crush");
    set (*fx, "bits", 8.0f);
    set (*fx, "rate", 48000.0f);
    check (peak (run (*fx, Stereo (4800)).l) == 0.0, "dither off: silence out");
    set (*fx, "dither", 1.0f);
    fx->reset();
    const auto hiss = run (*fx, Stereo (48000));
    const double step = 1.0 / 128.0;
    const double level = rms (hiss.l) / step;
    check (level > 0.3 && level < 1.2, "dither noise is " + std::to_string (level) + " steps RMS");
    check (rms (hiss.l, 0, 24000) > 0.0 && std::abs (rms (hiss.l, 0, 24000) - rms (hiss.l, 24000)) < 0.2 * rms (hiss.l), "and it is steady");
    // A sine below one step comes through with dither (it is averaged into the noise) and is lost without.
    Stereo faint = sine (1000.0, -60.0, 48000);
    set (*fx, "dither", 0.0f);
    fx->reset();
    const double plain = toneAmplitude (run (*fx, faint).l, 1000.0, 0, 48000);
    set (*fx, "dither", 1.0f);
    fx->reset();
    const double dithered = toneAmplitude (run (*fx, faint).l, 1000.0, 0, 48000);
    check (plain < 1.0e-5, "a -60 dBFS sine is lost at 8 bits without dither (" + std::to_string (plain) + ")");
    check (std::abs (linToDb (dithered) - (-60.0)) < 2.0, "and kept with dither (" + std::to_string (linToDb (dithered)) + " dB)");
}

// ---------------------------------------------------------------------------------------------------------------------
// Filter
// ---------------------------------------------------------------------------------------------------------------------

FX_TEST ("filter: LP 24 at 1 kHz attenuates 4 kHz by at least 40 dB and passes 250 Hz within 1 dB (resonance 0)")
{
    auto fx = make ("filter");
    set (*fx, "type", 1.0f);
    set (*fx, "freq", 1000.0f);
    set (*fx, "resonance", 0.0f);
    const double low = gainDb (*fx, 250.0), high = gainDb (*fx, 4000.0);
    std::printf ("  LP 24: 250 Hz %+.2f dB, 4 kHz %+.2f dB\n", low, high);
    check (high < -40.0, "4 kHz at " + std::to_string (high) + " dB");
    check (std::abs (low) < 1.0, "250 Hz at " + std::to_string (low) + " dB");
}

FX_TEST ("filter: every type has the response it is named for")
{
    struct Case
    {
        const char* name;
        int type;
        double pass[2];   // frequencies that must come through within 1.5 dB (0 = unused)
        double stop[2];   // frequencies that must be cut ...
        double stopDb;    // ... by at least this much
    };
    const Case cases[] {
        { "LP 12", 0, { 100.0, 0.0 }, { 8000.0, 0.0 }, 30.0 },
        { "LP 24", 1, { 100.0, 0.0 }, { 4000.0, 0.0 }, 40.0 },
        { "HP 12", 2, { 8000.0, 0.0 }, { 125.0, 0.0 }, 30.0 },
        { "HP 24", 3, { 8000.0, 0.0 }, { 250.0, 0.0 }, 40.0 },
        { "BP 12", 4, { 1000.0, 0.0 }, { 62.0, 16000.0 }, 15.0 },
        { "Notch", 5, { 100.0, 10000.0 }, { 1000.0, 0.0 }, 30.0 },
        { "Ladder", 6, { 100.0, 0.0 }, { 4000.0, 0.0 }, 40.0 },
    };
    for (const auto& c : cases)
    {
        auto fx = make ("filter");
        set (*fx, "type", (float) c.type);
        set (*fx, "freq", 1000.0f);
        set (*fx, "resonance", 0.0f);
        for (const double hz : c.pass)
            if (hz > 0.0)
            {
                const double db = gainDb (*fx, hz);
                check (std::abs (db) < 1.5, std::string (c.name) + ": " + std::to_string ((int) hz) + " Hz passes (" + std::to_string (db) + " dB)");
            }
        for (const double hz : c.stop)
            if (hz > 0.0)
            {
                const double db = gainDb (*fx, hz);
                check (db < -c.stopDb, std::string (c.name) + ": " + std::to_string ((int) hz) + " Hz is cut (" + std::to_string (db) + " dB)");
            }
    }
}

FX_TEST ("filter: resonance raises a peak at the cutoff, in the state-variable types and in the ladder")
{
    for (const int type : { 0, 1, 6 })
    {
        auto fx = make ("filter");
        set (*fx, "type", (float) type);
        set (*fx, "freq", 1000.0f);
        set (*fx, "resonance", 0.0f);
        const double flat = gainDb (*fx, 1000.0, -30.0);
        set (*fx, "resonance", 90.0f);
        const double peaked = gainDb (*fx, 1000.0, -30.0);
        check (peaked - flat > 6.0, "type " + std::to_string (type) + ": resonance adds " + std::to_string (peaked - flat) + " dB at the cutoff");
    }
}

FX_TEST ("filter: drive is transparent at 0 dB and keeps the level near the input's when it saturates")
{
    auto fx = make ("filter");
    set (*fx, "type", 0.0f);
    set (*fx, "freq", 20000.0f);
    set (*fx, "resonance", 0.0f);
    const double clean = gainDb (*fx, 440.0, -6.0);
    check (std::abs (clean) < 0.5, "drive 0: unity (" + std::to_string (clean) + " dB)");
    set (*fx, "drive", 24.0f);
    const double driven = gainDb (*fx, 440.0, -12.0);
    check (std::abs (driven) < 6.0, "drive 24 dB at -12 dBFS stays within 6 dB of the input (" + std::to_string (driven) + " dB)");
    fx->reset();
    const auto out = run (*fx, sine (440.0, -6.0, 9600));
    check (peak (out.l) < 1.0, "and saturates instead of growing");
}

FX_TEST ("filter: a cutoff sweep has no NaN and no jumps, in every type, with and without resonance")
{
    for (int type = 0; type < 7; ++type)
        for (const float resonance : { 20.0f, 95.0f })
        {
            auto fx = make ("filter");
            set (*fx, "type", (float) type);
            set (*fx, "resonance", resonance);
            const auto in = sine (330.0, -12.0, 96000);
            Stereo out (in.size());
            // Moved every 64 samples, the way a host automates a knob, from 80 Hz to 12 kHz and back.
            for (size_t pos = 0; pos < in.size(); pos += 64)
            {
                const double t = (double) pos / (double) in.size();
                const double sweep = t < 0.5 ? t * 2.0 : 2.0 - t * 2.0;
                fx->setParam (fx->paramIndex ("freq"), (float) (80.0 * std::pow (150.0, sweep)));
                float* io[2] { out.l.data() + pos, out.r.data() + pos };
                std::copy (in.l.begin() + (long) pos, in.l.begin() + (long) pos + 64, out.l.begin() + (long) pos);
                std::copy (in.r.begin() + (long) pos, in.r.begin() + (long) pos + 64, out.r.begin() + (long) pos);
                fx->process (io, 2, 64, nullptr);
            }
            const std::string what = "type " + std::to_string (type) + " resonance " + std::to_string ((int) resonance);
            check (finite (out), what + ": finite");
            // The sine itself moves by at most 0.0108 per sample at -12 dBFS; resonance can ring above that, but a click
            // from a coefficient jump would be several times larger than anything the signal does.
            const double step = largestStep (out.l);
            check (step < 0.25, what + ": largest step " + std::to_string (step));
            check (peak (out.l) < 4.0, what + ": bounded (" + std::to_string (peak (out.l)) + ")");
        }
}

FX_TEST ("filter: the LFO moves the cutoff by up to four octaves, smoothly, in every shape")
{
    for (int shape = 0; shape < 5; ++shape)
    {
        auto fx = make ("filter");
        set (*fx, "lfoShape", (float) shape);
        set (*fx, "lfoRate", 4.0f);
        set (*fx, "lfoDepth", 100.0f);
        set (*fx, "freq", 1000.0f);
        fx->reset();
        const auto out = run (*fx, noise (-12.0, 48000, 9), 64);
        check (finite (out), "shape " + std::to_string (shape) + ": finite");
        // Sample the cutoff every 64 samples over a second.
        float lowest = 1.0e9f, highest = 0.0f;
        auto probe = make ("filter");
        set (*probe, "lfoShape", (float) shape);
        set (*probe, "lfoRate", 4.0f);
        set (*probe, "lfoDepth", 100.0f);
        probe->reset();
        Stereo block (64);
        for (int b = 0; b < 750; ++b)
        {
            float* io[2] { block.l.data(), block.r.data() };
            probe->process (io, 2, 64, nullptr);
            lowest = std::min (lowest, probe->readout (0));
            highest = std::max (highest, probe->readout (0));
        }
        if (shape != 4) // sample and hold takes a random value per cycle: 4 cycles may not reach the corners
        {
            check (lowest < 140.0f, "shape " + std::to_string (shape) + ": lowest cutoff " + std::to_string (lowest));
            check (highest > 7000.0f, "shape " + std::to_string (shape) + ": highest cutoff " + std::to_string (highest));
        }
        check (lowest >= 20.0f && highest <= 0.45f * 48000.0f, "shape " + std::to_string (shape) + ": the cutoff stays inside the audible range");
    }
    // A square LFO flips the cutoff by eight octaves at once; the smoothing must keep that from clicking.
    auto fx = make ("filter");
    set (*fx, "lfoShape", 3.0f);
    set (*fx, "lfoRate", 3.0f);
    set (*fx, "lfoDepth", 100.0f);
    set (*fx, "resonance", 50.0f);
    fx->reset();
    const auto out = run (*fx, sine (200.0, -12.0, 48000));
    check (largestStep (out.l, 1) < 0.25, "square LFO: largest step " + std::to_string (largestStep (out.l)));
}

FX_TEST ("filter: a synced LFO has the same phase at the same song position, whatever happened before")
{
    // The LFO value is a function of the position only: sine at a quarter of a beat into a 1/4 cycle is the top.
    struct Probe
    {
        float lfo;
        float cutoff;
    };
    auto lfoAt = [] (double ppq, bool previousNoise, int shape) -> Probe
    {
        auto fx = make ("filter");
        set (*fx, "lfoSync", 1.0f);
        set (*fx, "lfoDiv", 2.0f); // 1/4: one beat
        set (*fx, "lfoShape", (float) shape);
        set (*fx, "lfoDepth", 100.0f);
        fx->reset();
        if (previousNoise) // some earlier playing at another position, with its own state
        {
            daw::fx::TransportInfo t;
            t.bpm = 97.0;
            t.ppq = 12.7;
            t.playing = true;
            fx->setTransport (t);
            run (*fx, noise (-12.0, 5000, 2));
        }
        daw::fx::TransportInfo t;
        t.bpm = 120.0;
        t.ppq = ppq;
        t.playing = true;
        fx->setTransport (t);
        Stereo one (1);
        float* io[2] { one.l.data(), one.r.data() };
        fx->process (io, 2, 1, nullptr);
        return { fx->readout (1), fx->readout (0) };
    };
    for (const bool history : { false, true })
    {
        check (std::abs (lfoAt (0.25, history, 0).lfo - 1.0f) < 1.0e-4f, "sine at ppq 0.25 is +1 (history " + std::to_string (history) + ")");
        check (std::abs (lfoAt (0.75, history, 0).lfo + 1.0f) < 1.0e-4f, "sine at ppq 0.75 is -1");
        check (std::abs (lfoAt (8.25, history, 0).lfo - 1.0f) < 1.0e-4f, "sine at ppq 8.25 is +1 as well, four cycles later");
        check (std::abs (lfoAt (0.5, history, 2).lfo - 0.0f) < 1.0e-4f, "saw at ppq 0.5 is 0");
        check (lfoAt (0.1, history, 3).lfo == 1.0f && lfoAt (0.6, history, 3).lfo == -1.0f, "square follows the position");
    }
    const auto a = lfoAt (3.3, false, 4), b = lfoAt (3.3, true, 4), c = lfoAt (3.9, true, 4), d = lfoAt (4.3, true, 4);
    check (a.lfo == b.lfo && b.lfo == c.lfo, "sample and hold: one value for the whole cycle, whatever the history");
    check (c.lfo != d.lfo, "and another in the next cycle");

    // And the whole output is the same when the start is the same.
    auto render = [] (double ppq)
    {
        auto fx = make ("filter");
        set (*fx, "lfoSync", 1.0f);
        set (*fx, "lfoDepth", 80.0f);
        set (*fx, "lfoShape", 1.0f);
        set (*fx, "resonance", 60.0f);
        fx->reset();
        daw::fx::TransportInfo t;
        t.ppq = ppq;
        t.playing = true;
        fx->setTransport (t);
        return run (*fx, noise (-12.0, 24000, 17));
    };
    const auto first = render(1.6), second = render(1.6), shifted = render (1.85);
    double same = 0.0, different = 0.0;
    for (size_t i = 0; i < first.size(); ++i)
    {
        same = std::max (same, (double) std::abs (first.l[i] - second.l[i]));
        different = std::max (different, (double) std::abs (first.l[i] - shifted.l[i]));
    }
    check (same == 0.0, "two renders from the same position are identical");
    check (different > 0.01, "and from another position they are not");

    // Free-running, the LFO has the rate asked for: the upward zero crossings of a sine are one period apart.
    auto fx = make ("filter");
    set (*fx, "lfoRate", 2.0f);
    set (*fx, "lfoDepth", 100.0f);
    fx->reset();
    Stereo block (48);
    std::vector<float> trace;
    for (int i = 0; i < 2000; ++i)
    {
        float* io[2] { block.l.data(), block.r.data() };
        fx->process (io, 2, 48, nullptr);
        trace.push_back (fx->readout (1));
    }
    std::vector<size_t> upward;
    for (size_t i = 1; i < trace.size(); ++i)
        if (trace[i - 1] < 0.0f && trace[i] >= 0.0f)
            upward.push_back (i);
    check (upward.size() >= 2, "a 2 Hz sine crosses upwards twice in two seconds");
    for (size_t i = 1; i < upward.size(); ++i)
        check (std::abs ((int) (upward[i] - upward[i - 1]) - 500) <= 1, "2 Hz is 500 blocks of 48 samples per period (" + std::to_string (upward[i] - upward[i - 1]) + ")");
}

FX_TEST ("filter: the envelope follower opens the cutoff with loud input at +100 % and closes it at -100 %")
{
    auto cutoffAfter = [] (float depth, double levelDb)
    {
        auto fx = make ("filter");
        set (*fx, "envDepth", depth);
        set (*fx, "envAttack", 1.0f);
        set (*fx, "envRelease", 50.0f);
        set (*fx, "freq", 1000.0f);
        fx->reset();
        run (*fx, noise (levelDb, 24000, 6));
        return fx->readout (0);
    };
    const float neutral = cutoffAfter (0.0f, -6.0), open = cutoffAfter (100.0f, -6.0), closed = cutoffAfter (-100.0f, -6.0);
    const float quietOpen = cutoffAfter (100.0f, -60.0);
    check (std::abs (neutral - 1000.0f) < 1.0f, "no envelope: the cutoff stays at 1 kHz (" + std::to_string (neutral) + ")");
    check (open > 8000.0f, "+100 % with loud noise: " + std::to_string (open) + " Hz");
    check (closed < 130.0f, "-100 % with loud noise: " + std::to_string (closed) + " Hz");
    check (quietOpen < open * 0.2f, "quiet input opens it less (" + std::to_string (quietOpen) + " Hz)");

    // After the sound stops, the release closes it again.
    auto fx = make ("filter");
    set (*fx, "envDepth", 100.0f);
    set (*fx, "envRelease", 100.0f);
    fx->reset();
    run (*fx, noise (-6.0, 12000, 6));
    const float during = fx->readout (0);
    run (*fx, Stereo (48000));
    const float after = fx->readout (0);
    check (after < during * 0.2f, "the cutoff falls back after the sound (" + std::to_string (during) + " -> " + std::to_string (after) + ")");
}

FX_TEST ("filter: it rings out within its reported tail even at high resonance")
{
    for (const int type : { 0, 1, 6 })
    {
        auto fx = make ("filter");
        set (*fx, "type", (float) type);
        set (*fx, "resonance", 95.0f);
        set (*fx, "freq", 300.0f);
        run (*fx, noise (-6.0, 4800, 5));
        const size_t tail = (size_t) ((fx->tailSeconds() + 0.5) * kRate);
        const auto after = run (*fx, Stereo (tail + 4800));
        check (peak (after.l, tail) < 1.0e-4, "type " + std::to_string (type) + ": silent after the tail of " + std::to_string (fx->tailSeconds()) + " s ("
                                                  + std::to_string (linToDb (peak (after.l, tail))) + " dB)");
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// CPU
// ---------------------------------------------------------------------------------------------------------------------

FX_TEST ("CPU: 10 s of stereo 48 kHz noise through drive, crush and filter takes under 1 % of real time")
{
    const auto in = noise (-12.0, 480000, 11);
    struct Setup
    {
        const char* label;
        const char* id;
        std::vector<std::pair<const char*, float>> params;
    };
    const Setup setups[] {
        { "drive (defaults)", "drive", {} },
        { "drive Hard, 24 dB, bias", "drive", { { "mode", 3.0f }, { "drive", 24.0f }, { "bias", 30.0f }, { "tone", 40.0f } } },
        { "drive Fold", "drive", { { "mode", 4.0f }, { "drive", 12.0f } } },
        { "crush", "crush", { { "dither", 1.0f } } },
        { "filter (defaults)", "filter", {} },
        { "filter LP 24, LFO + envelope", "filter", { { "type", 1.0f }, { "lfoDepth", 80.0f }, { "lfoRate", 5.0f }, { "envDepth", 60.0f }, { "resonance", 70.0f } } },
        { "filter Ladder, LFO", "filter", { { "type", 6.0f }, { "lfoDepth", 50.0f }, { "drive", 12.0f } } },
    };
    for (const auto& s : setups)
    {
        auto fx = make (s.id);
        for (const auto& p : s.params)
            set (*fx, p.first, p.second);
        run (*fx, noise (-12.0, 4800, 2)); // warm up
        double best = 1.0e9;
        Stereo out;
        for (int attempt = 0; attempt < 5; ++attempt) // the best of a few runs: a shared machine adds noise, never speed
        {
            fx->reset();
            const auto t0 = std::chrono::steady_clock::now();
            out = run (*fx, in);
            best = std::min (best, std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
        }
        std::printf ("  %s: %.3f s for 10 s of audio = %.2f %% of real time\n", s.label, best, best / 10.0 * 100.0);
        check (finite (out), std::string (s.label) + ": finite output");
        check (best < 0.1, std::string (s.label) + ": CPU " + std::to_string (best / 10.0 * 100.0) + " % of real time");
    }
}
