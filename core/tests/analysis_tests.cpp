// Tests for onset detection, tempo estimation and beat tracking (task 7.2) on synthesised drum loops and pads.
#include "daw/dsp/Onsets.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <numbers>
#include <random>
#include <string>
#include <vector>

namespace
{
int failures = 0, checks = 0;

void check (bool ok, const std::string& what)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::printf ("  FAIL: %s\n", what.c_str());
    }
}

void test (const char* name, const std::function<void()>& fn)
{
    std::printf ("[test] %s\n", name);
    fn();
}

constexpr double kRate = 44100.0;
constexpr double kTwoPi = 2.0 * std::numbers::pi;

struct Signal
{
    std::vector<float> samples;
    std::vector<double> hits;  // every drum hit, seconds
    std::vector<double> beats; // every quarter-note beat inside the pattern, seconds
};

void addKick (std::vector<float>& x, double at, float amp)
{
    const auto start = (int64_t) std::llround (at * kRate);
    double phase = 0.0;
    for (int64_t i = 0; i < (int64_t) (1.0 * kRate) && start + i < (int64_t) x.size(); ++i)
    {
        const double t = (double) i / kRate;
        phase += kTwoPi * (50.0 + 110.0 * std::exp (-t / 0.03)) / kRate;
        x[(size_t) (start + i)] += amp * (float) (std::exp (-t / 0.12) * std::sin (phase));
    }
}

// Noise burst, first-differenced so it sits high in the spectrum; `tone` adds a snare body.
void addNoiseHit (std::vector<float>& x, double at, float amp, double decay, double tone, std::mt19937& rng)
{
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
    const auto start = (int64_t) std::llround (at * kRate);
    float last = 0.0f;
    for (int64_t i = 0; i < (int64_t) (9.0 * decay * kRate) && start + i < (int64_t) x.size(); ++i)
    {
        const double t = (double) i / kRate;
        const float v = dist (rng);
        double s = 0.5 * (v - last);
        last = v;
        if (tone > 0.0)
            s += 0.6 * std::sin (kTwoPi * tone * t);
        x[(size_t) (start + i)] += amp * (float) (std::exp (-t / decay) * s);
    }
}

enum class Style { house, hipHop, techno16, dnb };

// A drum pattern `bars` 4/4 bars long at `bpm`, after `intro` seconds of silence and followed by `tail` seconds.
// Swing delays the off-beat eighths by that fraction of an eighth.
Signal drums (double bpm, int bars, Style style, double swing = 0.0, double noiseDb = -60.0, double intro = 0.0,
              double tail = 0.0, unsigned seed = 1)
{
    Signal s;
    const double beat = 60.0 / bpm;
    s.samples.assign ((size_t) std::llround ((intro + bars * 4 * beat + tail) * kRate), 0.0f);
    std::mt19937 rng (seed);
    std::normal_distribution<float> jitter (0.0f, 0.0015f); // a human-ish 1.5 ms

    const auto hit = [&] (double beatPos, int kind, float amp) {
        const double t = intro + beatPos * beat + (kind == 2 ? 0.0 : (double) jitter (rng)) * (beatPos > 0.0 ? 1.0 : 0.0);
        if (kind == 0)
            addKick (s.samples, t, amp);
        else if (kind == 1)
            addNoiseHit (s.samples, t, amp, 0.06, 190.0, rng); // snare
        else
            addNoiseHit (s.samples, t, amp, 0.015, 0.0, rng); // hat
        s.hits.push_back ((double) std::llround (t * kRate) / kRate);
    };

    for (int bar = 0; bar < bars; ++bar)
    {
        const double b0 = bar * 4.0;
        for (int q = 0; q < 4; ++q)
            s.beats.push_back (intro + (b0 + q) * beat);
        for (int e = 0; e < 8; ++e)
        {
            const double pos = b0 + e * 0.5 + (e % 2 == 1 ? swing * 0.5 : 0.0);
            const bool onBeat = e % 2 == 0;
            switch (style)
            {
                case Style::house:
                    if (onBeat)
                        hit (pos, 0, 0.9f);
                    else
                        hit (pos, 2, 0.35f);
                    break;
                case Style::techno16:
                    if (onBeat)
                        hit (pos, 0, 0.9f);
                    else
                        hit (pos, 2, 0.3f);
                    break;
                case Style::hipHop:
                    if (e == 0 || e == 5)
                        hit (pos, 0, 0.9f);
                    else if (e == 2 || e == 6)
                        hit (pos, 1, 0.6f);
                    else
                        hit (pos, 2, onBeat ? 0.3f : 0.2f);
                    break;
                case Style::dnb:
                    if (e == 0 || e == 5)
                        hit (pos, 0, 0.9f);
                    else if (e == 2 || e == 6)
                        hit (pos, 1, 0.7f);
                    else
                        hit (pos, 2, 0.25f);
                    break;
            }
        }
        if (style == Style::techno16) // sixteenth ticks between the eighths
            for (int e = 0; e < 8; ++e)
                hit (b0 + e * 0.5 + 0.25, 2, 0.12f);
    }
    std::sort (s.hits.begin(), s.hits.end());

    if (noiseDb > -200.0)
    {
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        const auto a = (float) std::pow (10.0, noiseDb / 20.0);
        const auto from = (size_t) std::llround (intro * kRate);
        for (size_t i = from; i < s.samples.size(); ++i)
            s.samples[i] += a * dist (rng);
    }
    return s;
}

double nearestDistance (const std::vector<double>& set, double t)
{
    double best = 1.0e9;
    for (auto v : set)
        best = std::min (best, std::abs (v - t));
    return best;
}

std::string fmt (const char* f, double a, double b = 0.0)
{
    char buf[160];
    std::snprintf (buf, sizeof (buf), f, a, b);
    return buf;
}

daw::dsp::TempoEstimate analyse (const Signal& s, const std::string& name = {})
{
    return daw::dsp::estimateTempo (s.samples.data(), (int64_t) s.samples.size(), kRate, name);
}

} // namespace

int main()
{
    using namespace daw::dsp;

    test ("onsets land within 5 ms of every hit", [] {
        for (double bpm : { 92.0, 124.0, 174.0 })
        {
            const auto s = drums (bpm, 4, bpm < 100 ? Style::hipHop : (bpm > 160 ? Style::dnb : Style::house), 0.2, -50.0);
            const auto onsets = detectOnsets (s.samples.data(), (int64_t) s.samples.size(), kRate);
            int found = 0, spurious = 0;
            double worst = 0.0;
            for (auto h : s.hits)
            {
                const double d = nearestDistance (onsets, h);
                worst = std::max (worst, d);
                found += d <= 0.005 ? 1 : 0;
                if (d > 0.005)
                    std::printf ("    missed %.4f (%.4f beats), nearest onset %.2f ms away\n", h, h * bpm / 60.0, d * 1000.0);
            }
            for (auto o : onsets)
                if (nearestDistance (s.hits, o) > 0.005)
                {
                    ++spurious;
                    std::printf ("    spurious %.4f (%.4f beats)\n", o, o * bpm / 60.0);
                }
            std::printf ("  %.0f BPM: %zu hits, %zu onsets, worst %.2f ms\n", bpm, s.hits.size(), onsets.size(), worst * 1000.0);
            check (found == (int) s.hits.size(), fmt ("%.0f BPM: every hit has an onset within 5 ms (worst %.2f ms)", bpm, worst * 1000.0));
            check (spurious == 0, fmt ("%.0f BPM: no onsets away from hits (%.0f spurious)", bpm, spurious));
        }
    });

    test ("onsets keep 50 ms apart and follow the sensitivity", [] {
        const auto s = drums (128.0, 4, Style::techno16, 0.0, -50.0);
        const auto normal = detectOnsets (s.samples.data(), (int64_t) s.samples.size(), kRate);
        const auto strict = detectOnsets (s.samples.data(), (int64_t) s.samples.size(), kRate, 0.0f);
        const auto loose = detectOnsets (s.samples.data(), (int64_t) s.samples.size(), kRate, 1.0f);
        bool gaps = true, sorted = true;
        for (size_t i = 1; i < loose.size(); ++i)
        {
            gaps = gaps && loose[i] - loose[i - 1] >= 0.05 - 1.0e-9;
            sorted = sorted && loose[i] > loose[i - 1];
        }
        std::printf ("  strict %zu, normal %zu, loose %zu onsets, %zu hits\n", strict.size(), normal.size(), loose.size(), s.hits.size());
        check (sorted && gaps, "ascending and at least 50 ms apart");
        check (strict.size() <= normal.size() && normal.size() <= loose.size(), "lower sensitivity finds fewer onsets");
        check (strict.size() >= 32, "the kicks survive the strictest setting");
    });

    test ("silence and tiny inputs", [] {
        std::vector<float> silent (44100, 0.0f);
        check (detectOnsets (silent.data(), (int64_t) silent.size(), kRate).empty(), "no onsets in silence");
        const auto t = estimateTempo (silent.data(), (int64_t) silent.size(), kRate);
        check (t.bpm == 0.0 && t.beats.empty(), "no tempo in silence");
        check (estimateTempo (silent.data(), 100, kRate).bpm == 0.0, "too short");
        check (detectOnsets (nullptr, 0, kRate).empty(), "empty input");
    });

    struct Case
    {
        double bpm;
        Style style;
        double swing;
    };
    const Case mainSet[] = { { 92.0, Style::hipHop, 0.3 }, { 124.0, Style::house, 0.15 }, { 128.0, Style::house, 0.0 },
                             { 140.0, Style::techno16, 0.0 }, { 174.0, Style::dnb, 0.1 } };

    test ("8-bar loops: exact integer tempo, no octave errors, grid from bar 1", [&] {
        for (const auto& c : mainSet)
        {
            const auto s = drums (c.bpm, 8, c.style, c.swing, -48.0);
            const auto t = analyse (s);
            double worstBeat = 0.0;
            for (auto b : s.beats)
                worstBeat = std::max (worstBeat, nearestDistance (t.beats, b));
            std::printf ("  %.0f BPM -> %.4f (conf %.2f), %zu beats, first %.2f ms, worst beat %.2f ms\n", c.bpm, t.bpm,
                         t.confidence, t.beats.size(), t.firstBeatSeconds * 1000.0, worstBeat * 1000.0);
            check (t.bpm == c.bpm, fmt ("%.0f BPM loop reads exactly (got %.4f)", c.bpm, t.bpm));
            check (std::abs (t.firstBeatSeconds) < 0.010, fmt ("%.0f BPM: first beat at 0 (got %.2f ms)", c.bpm, t.firstBeatSeconds * 1000.0));
            check (t.beats.size() == s.beats.size(), fmt ("%.0f BPM: one beat per quarter (%.0f)", c.bpm, (double) t.beats.size()));
            check (worstBeat < 0.010, fmt ("%.0f BPM: beats within 10 ms (worst %.2f ms)", c.bpm, worstBeat * 1000.0));
        }
    });

    test ("lengths off the bar grid: tempo within 0.5 %", [&] {
        for (const auto& c : mainSet)
        {
            const auto s = drums (c.bpm, 12, c.style, c.swing, -45.0, 0.0, 0.37, 7);
            const auto t = analyse (s);
            std::printf ("  %.0f BPM -> %.4f (conf %.2f)\n", c.bpm, t.bpm, t.confidence);
            check (std::abs (t.bpm / c.bpm - 1.0) < 0.005, fmt ("%.0f BPM within 0.5 %% (got %.4f)", c.bpm, t.bpm));
        }
    });

    test ("2-bar loops", [] {
        for (double bpm : { 92.0, 124.0, 140.0, 174.0 })
        {
            const auto style = bpm < 100 ? Style::hipHop : (bpm > 160 ? Style::dnb : Style::house);
            const auto s = drums (bpm, 2, style, 0.0, -50.0, 0.0, 0.0, 3);
            const auto t = analyse (s);
            std::printf ("  %.0f BPM -> %.4f, first %.2f ms\n", bpm, t.bpm, t.firstBeatSeconds * 1000.0);
            check (t.bpm == bpm, fmt ("2-bar %.0f BPM loop reads exactly (got %.4f)", bpm, t.bpm));
            check (std::abs (t.firstBeatSeconds) < 0.010, fmt ("2-bar %.0f BPM: first beat at 0 (%.2f ms)", bpm, t.firstBeatSeconds * 1000.0));
        }
    });

    test ("silent intro: the first beat follows it", [] {
        const auto s = drums (126.0, 16, Style::house, 0.1, -50.0, 0.5, 0.21, 11);
        const auto t = analyse (s);
        std::printf ("  126 BPM -> %.4f, first beat %.2f ms, %zu beats\n", t.bpm, t.firstBeatSeconds * 1000.0, t.beats.size());
        check (std::abs (t.bpm / 126.0 - 1.0) < 0.005, fmt ("tempo within 0.5 %% (%.4f)", t.bpm));
        check (std::abs (t.firstBeatSeconds - 0.5) < 0.010, fmt ("first beat at 0.5 s (got %.2f ms)", t.firstBeatSeconds * 1000.0));
        check (! t.beats.empty() && t.beats.front() > 0.49, "no beats in the silence");
    });

    test ("file-name hints", [] {
        const auto loop = drums (124.0, 6, Style::house, 0.0, -50.0, 0.0, 0.13, 5);
        const auto named = analyse (loop, "124-Am Drums.wav");
        check (named.bpm == 124.0, fmt ("\"124-\" gives exactly 124 (got %.4f)", named.bpm));
        check (analyse (loop, "Drums_124bpm.wav").bpm == 124.0, "\"_124bpm\"");
        check (analyse (loop, "Drums 62 BPM.wav").bpm == 62.0, "a half-tempo name is trusted (octave related)");

        // A sustained chord pad with faint, irregular attacks: little periodicity to go on, so the name decides.
        std::vector<float> pad ((size_t) (12.0 * kRate), 0.0f);
        std::mt19937 rng (9);
        std::uniform_real_distribution<double> when (0.0, 11.5);
        for (int i = 0; i < 14; ++i)
            addNoiseHit (pad, when (rng), 0.02f, 0.01, 0.0, rng);
        for (size_t i = 0; i < pad.size(); ++i)
        {
            const double t = (double) i / kRate;
            const double swell = 0.5 - 0.5 * std::cos (kTwoPi * t / 6.0);
            pad[i] += (float) (0.15 * swell * (std::sin (kTwoPi * 220.0 * t) + std::sin (kTwoPi * 277.2 * t) + std::sin (kTwoPi * 329.6 * t)));
        }
        const auto weak = estimateTempo (pad.data(), (int64_t) pad.size(), kRate);
        const auto hinted = estimateTempo (pad.data(), (int64_t) pad.size(), kRate, "Warm Pad_100bpm.wav");
        const auto strong = analyse (loop);
        std::printf ("  pad: %.2f BPM, conf %.2f; drums conf %.2f\n", weak.bpm, weak.confidence, strong.confidence);
        check (weak.confidence < 0.5 && weak.confidence < strong.confidence, "a pad is a weak estimate");
        check (hinted.bpm == 100.0, fmt ("the name wins over a weak estimate (got %.4f)", hinted.bpm));
        check (strong.confidence > 0.5, "a drum loop is a strong estimate");
        check (analyse (loop, "Drums 90 BPM.wav").bpm == strong.bpm, "an unrelated name does not override a strong estimate");
    });

    test ("60 s at 44.1 kHz under a second", [] {
        const auto s = drums (128.0, 32, Style::techno16, 0.1, -45.0, 0.0, 0.0, 21);
        const auto start = std::chrono::steady_clock::now();
        const auto t = analyse (s);
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
        std::printf ("  %.1f s of audio analysed in %.1f ms -> %.4f BPM\n", (double) s.samples.size() / kRate, ms, t.bpm);
        check (t.bpm == 128.0, "tempo of the long track");
#ifdef NDEBUG
        check (ms < 1000.0, fmt ("analysis under 1 s (%.1f ms)", ms));
#endif
    });

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
