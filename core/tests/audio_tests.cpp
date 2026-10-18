// Tests for the sinc interpolator, the audio analysis helpers (loopback latency, phrase detection, peaks) and loudness.
#include "daw/dsp/Loudness.h"
#include "daw/dsp/SincInterpolator.h"
#include "daw/engine/AudioAnalysis.h"
#include "daw/engine/AudioClip.h"
#include "daw/engine/Clip.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <numbers>
#include <random>
#include <string>
#include <utility>
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

float dbToLin (double db) { return (float) std::pow (10.0, db / 20.0); }

// Adds a decaying 220 Hz burst of 0.3 s starting at `at`.
void addBurst (std::vector<float>& x, int64_t at, double sr, double peakDb)
{
    const int64_t len = (int64_t) (0.3 * sr);
    const float peak = dbToLin (peakDb);
    for (int64_t i = 0; i < len && at + i < (int64_t) x.size(); ++i)
    {
        const double t = (double) i / sr;
        x[(size_t) (at + i)] += peak * (float) (std::exp (-12.0 * t) * std::sin (2.0 * std::numbers::pi * 220.0 * t));
    }
}

std::vector<float> noise (size_t n, double db, unsigned seed)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
    std::vector<float> x (n);
    const float a = dbToLin (db);
    for (auto& v : x)
        v = a * dist (rng);
    return x;
}

using namespace daw;
} // namespace

int main()
{
    test ("audio clip: a loop that follows the project tempo reads its own beats at any tempo", [] {
        engine::AudioPart part;
        part.fileFramesPerBeat = 44100.0 * 60.0 / 90.0; // a 90 BPM loop at 44.1 kHz
        for (const double bpm : { 90.0, 120.0, 174.0 })
            check (std::abs (engine::framesPerBeat (part, 48000.0 * 60.0 / bpm, 48000.0) - 29400.0) < 1.0e-6,
                   "a content beat is one beat of the file at " + std::to_string (bpm) + " BPM");
        part.pitchRatio = 2.0;
        check (std::abs (engine::framesPerBeat (part, 24000.0, 48000.0) - 58800.0) < 1.0e-6, "transposition still applies on top");
    });

    test ("sinc: resampling a 1 kHz sine from 48k to 44.1k", [] {
        const auto& sinc = dsp::sharedSincInterpolator();
        const int n = 4800;
        std::vector<float> x ((size_t) n);
        for (int i = 0; i < n; ++i)
            x[(size_t) i] = (float) std::sin (2.0 * std::numbers::pi * 1000.0 * i / 48000.0);
        const double step = 48000.0 / 44100.0;
        double maxErr = 0.0;
        for (int j = 0;; ++j)
        {
            const double pos = j * step;
            if (pos >= n - 16)
                break;
            if (pos < 16)
                continue;
            const double expected = std::sin (2.0 * std::numbers::pi * 1000.0 * pos / 48000.0);
            maxErr = std::max (maxErr, std::abs (expected - (double) sinc.valueAt (x.data(), n, pos)));
        }
        check (maxErr < 1e-3, "max error " + std::to_string (maxErr));
    });

    test ("sinc: integer positions are exact, DC and edges behave", [] {
        const auto& sinc = dsp::sharedSincInterpolator();
        std::vector<float> x (200);
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = (float) i * 0.01f - 1.0f;
        bool exact = true;
        for (int i = 0; i < 200; ++i)
            exact = exact && sinc.valueAt (x.data(), 200, i) == x[(size_t) i];
        check (exact, "integer positions return the stored sample");
        check (sinc.valueAt (x.data(), 200, 50.0 + 1e-12) == x[50], "tiny fraction takes the fast path");

        std::vector<float> dc (200, 1.0f);
        double worst = 0.0;
        for (double pos = 20.0; pos < 180.0; pos += 0.137)
            worst = std::max (worst, std::abs (1.0 - (double) sinc.valueAt (dc.data(), 200, pos)));
        check (worst < 1e-3, "DC gain is unity, worst " + std::to_string (worst));

        bool finite = true;
        for (double pos : { -100.5, -20.3, -3.7, 0.5, 199.5, 203.2, 500.5, 1e9 + 0.5 })
            finite = finite && std::isfinite (sinc.valueAt (dc.data(), 200, pos));
        check (finite, "no NaN near or beyond the ends");
        check (sinc.valueAt (dc.data(), 200, -100.5) == 0.0f && sinc.valueAt (dc.data(), 200, 400.5) == 0.0f,
               "far outside is silent");
        check (sinc.valueAt (dc.data(), 200, 0.0) == 1.0f && sinc.valueAt (dc.data(), 200, 200.0) == 0.0f,
               "exact ends");
    });

    test ("round trip latency measurement", [] {
        const int clicks = 10, spacing = 4800, maxLag = 4000;
        const auto train = engine::makeClickTrain (clicks, spacing, 0.5f);
        check ((int) train.size() == clicks * spacing && train[0] == 0.5f && train[4800] == 0.5f && train[1] == 0.0f,
               "click train layout");
        for (int delay : { 0, 371, 1500 })
        {
            auto rec = noise (train.size() + 8000, -60.0, 1);
            for (size_t i = 0; i < train.size(); ++i)
                if (train[i] != 0.0f)
                    rec[i + (size_t) delay] += train[i];
            const auto r = engine::measureRoundTrip (rec.data(), (int64_t) rec.size(), clicks, spacing, maxLag);
            check (r.ok && std::abs (r.latencySamples - delay) <= 1 && r.clicksFound == clicks,
                   "delay " + std::to_string (delay) + " measured " + std::to_string (r.latencySamples));
        }
        const auto rec = noise (train.size() + 8000, -60.0, 2);
        check (! engine::measureRoundTrip (rec.data(), (int64_t) rec.size(), clicks, spacing, maxLag).ok,
               "pure noise is rejected");
    });

    test ("last phrase detection", [] {
        const double sr = 48000.0;
        auto run = [&] (double seconds, double phraseAt, bool earlier, bool nearEarlier)
        {
            auto x = noise ((size_t) (seconds * sr), -80.0, 3);
            const int64_t S = (int64_t) (phraseAt * sr);
            for (int k = 0; k < 3; ++k)
                addBurst (x, S + (int64_t) (k * 0.8 * sr), sr, -6.0);
            if (earlier)
                addBurst (x, S - (int64_t) (5.5 * sr), sr, -6.0);
            if (nearEarlier)
                addBurst (x, S - (int64_t) (2.3 * sr), sr, -6.0);
            return std::pair<std::vector<float>, int64_t> { std::move (x), S };
        };

        {
            auto [x, S] = run (60.0, 30.0, false, false);
            const auto p = engine::findLastAudioPhrase (x.data(), (int64_t) x.size(), sr);
            check (p.found, "phrase found");
            check (std::abs (p.start - (S - 9600)) <= 240, "start at onset minus 0.2 s, off by " + std::to_string (p.start - (S - 9600)));
            check (std::abs (p.onsetSample - S) <= 48, "onset sample");
            check (p.end > S + (int64_t) (1.6 * sr) && p.end < S + (int64_t) (2.2 * sr), "end after the last decay");
        }
        {
            auto [x, S] = run (60.0, 30.0, true, false);
            const auto p = engine::findLastAudioPhrase (x.data(), (int64_t) x.size(), sr);
            check (p.found && std::abs (p.start - (S - 9600)) <= 240, "earlier phrase beyond 4 s is not merged");
        }
        {
            auto [x, S] = run (60.0, 30.0, false, true);
            const int64_t E = S - (int64_t) (2.3 * sr);
            const auto p = engine::findLastAudioPhrase (x.data(), (int64_t) x.size(), sr);
            check (p.found && std::abs (p.start - (E - 9600)) <= 240, "bursts 2 s apart are merged");
        }
        {
            auto [x, S] = run (300.0, 250.0, true, false);
            const auto p = engine::findLastAudioPhrase (x.data(), (int64_t) x.size(), sr);
            check (p.found && std::abs (p.start - (S - 9600)) <= 240, "five minute ring");
        }
        {
            const auto x = noise ((size_t) (10 * sr), -80.0, 4);
            check (! engine::findLastAudioPhrase (x.data(), (int64_t) x.size(), sr).found, "silence has no phrase");
        }
    });

    test ("peak overview", [] {
        std::vector<float> ramp (10);
        for (int i = 0; i < 10; ++i)
            ramp[(size_t) i] = (float) i;
        std::vector<float> out;
        engine::computePeaks (ramp.data(), 10, 4, out);
        check (out.size() == 6 && out[0] == 0 && out[1] == 3 && out[2] == 4 && out[3] == 7 && out[4] == 8 && out[5] == 9,
               "min/max pairs including the partial block");
        engine::computePeaks (ramp.data(), 0, 4, out);
        check (out.empty(), "empty input");
    });


    //==========================================================================
    // Audio clips (task 5.2)
    const auto makeSource = [] (double rate, int64_t frames, const std::function<float (int64_t)>& fn, int64_t publishUpTo = -1) {
        auto src = std::make_shared<engine::AudioSource> (1, frames, rate);
        for (int64_t i = 0; i < frames; ++i)
            src->left[(size_t) i] = fn (i);
        src->publish (publishUpTo < 0 ? frames : publishUpTo);
        return src;
    };
    const auto makeClip = [] (std::shared_ptr<engine::AudioSource> src, double lengthBeats) {
        engine::MidiClip clip;
        clip.lengthBeats = lengthBeats;
        clip.loop = true;
        auto part = std::make_shared<engine::AudioPart>();
        part->source = std::move (src);
        clip.audio = part;
        return clip;
    };
    // Renders like the engine does in Session View: block after block, the elapsed beats counted from the launch.
    const auto renderBlocks = [] (const engine::MidiClip& clip, double entry, double spb, double rate, int total, int block,
                                  engine::AudioRenderStats& stats) {
        std::vector<float> l ((size_t) total, 0.0f), r ((size_t) total, 0.0f);
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min (block, total - pos);
            engine::renderAudioClip (clip, entry, clip.loop, (double) pos / spb, spb, rate, {}, l.data() + pos, r.data() + pos, n, stats);
        }
        return l;
    };
    const auto maxStep = [] (const std::vector<float>& x, size_t from, size_t to) {
        float m = 0.0f;
        for (size_t i = std::max<size_t> (from, 1); i < std::min (to, x.size()); ++i)
            m = std::max (m, std::abs (x[i] - x[i - 1]));
        return m;
    };
    const auto zeroCrossings = [] (const std::vector<float>& x, size_t from, size_t to) {
        int n = 0;
        for (size_t i = from + 1; i < to; ++i)
            if ((x[i - 1] < 0.0f) != (x[i] < 0.0f))
                ++n;
        return n;
    };

    test ("audio clip: a one-bar sine loops eight times without a click at the seam", [&] {
        const double rate = 48000.0, spb = rate * 60.0 / 120.0; // 120 BPM: a bar is 96000 frames, 2000 periods of 1 kHz
        auto src = makeSource (rate, 96000, [rate] (int64_t i) { return 0.5f * (float) std::sin (2.0 * std::numbers::pi * 1000.0 * (double) i / rate); });
        const auto clip = makeClip (src, 4.0);
        engine::AudioRenderStats stats;
        const auto out = renderBlocks (clip, 0.0, spb, rate, 8 * 96000, 512, stats);
        const float inside = maxStep (out, 1000, 90000);
        float seams = 0.0f;
        for (int k = 1; k < 8; ++k)
            seams = std::max (seams, maxStep (out, (size_t) (k * 96000 - 200), (size_t) (k * 96000 + 200)));
        check (inside > 0.01f && seams <= inside * 1.5f, "seam step " + std::to_string (seams) + " vs inside " + std::to_string (inside));
        check (std::abs (out[(size_t) (7 * 96000 + 200)] - src->left[200]) < 1.0e-6f, "the eighth pass plays the file exactly");
        check (stats.missedFrames == 0, "no misses");
    });

    test ("audio clip: a loop that does not fit the period is crossfaded", [&] {
        const double rate = 48000.0, spb = rate * 60.0 / 120.0;
        // 441 Hz over half a bar: the seam lands in the middle of a period; audio before the brace start exists
        auto src = makeSource (rate, 96000, [rate] (int64_t i) { return 0.5f * (float) std::sin (2.0 * std::numbers::pi * 441.0 * (double) i / rate); });
        auto clip = makeClip (src, 4.0);
        clip.loopStart = 2.0;
        engine::AudioRenderStats stats;
        const auto out = renderBlocks (clip, 0.0, spb, rate, 4 * 96000, 480, stats);
        const float inside = maxStep (out, 1000, 90000);
        float seams = 0.0f;
        for (int k = 1; k < 7; ++k)
            seams = std::max (seams, maxStep (out, (size_t) (96000 + k * 48000 - 200), (size_t) (96000 + k * 48000 + 200)));
        check (seams <= inside * 1.5f, "crossfaded seam " + std::to_string (seams) + " vs inside " + std::to_string (inside));
    });

    test ("audio clip: without audio before the brace the seam fades out and in", [&] {
        const double rate = 48000.0, spb = rate * 60.0 / 120.0;
        auto src = makeSource (rate, 48000, [] (int64_t) { return 0.5f; });
        const auto clip = makeClip (src, 2.0);
        engine::AudioRenderStats stats;
        const auto out = renderBlocks (clip, 0.0, spb, rate, 3 * 48000, 512, stats);
        check (maxStep (out, 47000, 49000) < 0.05f, "no jump at the seam");
        check (std::abs (out[48000 + 2000] - 0.5f) < 1.0e-6f, "full level after the fade in");
    });

    test ("audio clip: sample rate and transposition by resampling", [&] {
        const double fileRate = 44100.0, rate = 48000.0, spb = rate * 60.0 / 120.0;
        auto src = makeSource (fileRate, 88200, [fileRate] (int64_t i) { return 0.5f * (float) std::sin (2.0 * std::numbers::pi * 1000.0 * (double) i / fileRate); });
        auto clip = makeClip (src, 4.0);
        clip.loop = false;
        engine::AudioRenderStats stats;
        auto out = renderBlocks (clip, 0.0, spb, rate, 48000, 512, stats);
        const int zc = zeroCrossings (out, 0, 48000); // one second: 2000 crossings at 1 kHz
        check (std::abs (zc - 2000) <= 2, "a 44.1 kHz file plays at its pitch at 48 kHz: " + std::to_string (zc));

        auto part = std::make_shared<engine::AudioPart> (*clip.audio);
        part->pitchRatio = 2.0;
        clip.audio = part;
        out = renderBlocks (clip, 0.0, spb, rate, 24000, 512, stats);
        const int zc2 = zeroCrossings (out, 0, 24000);
        check (std::abs (zc2 - 2000) <= 2, "+12 semitones doubles the frequency: " + std::to_string (zc2));
    });

    test ("audio clip: one-shot ends, misses are counted, reverse and fades", [&] {
        const double rate = 48000.0, spb = rate * 60.0 / 120.0;
        auto src = makeSource (rate, 48000, [] (int64_t i) { return (float) i / 48000.0f; }, 24000);
        auto clip = makeClip (src, 2.0);
        clip.loop = false;
        engine::AudioRenderStats stats;
        auto out = renderBlocks (clip, 0.0, spb, rate, 60000, 512, stats);
        check (stats.missedFrames == 24000, "frames not loaded yet are counted: " + std::to_string (stats.missedFrames));
        check (out[30000] == 0.0f && out[50000] == 0.0f, "unloaded frames and the end of a one-shot are silent");
        check (std::abs (out[1000] - 1000.0f / 48000.0f) < 1.0e-6f, "loaded frames play");

        src->publish (48000);
        auto part = std::make_shared<engine::AudioPart> (*clip.audio);
        part->reverse = true;
        clip.audio = part;
        out = renderBlocks (clip, 0.0, spb, rate, 48000, 512, stats);
        check (out[100] > 0.99f && out[47000] < 0.03f, "reverse plays from the end");

        part = std::make_shared<engine::AudioPart> (*clip.audio);
        part->reverse = false;
        clip.audio = part;
        std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
        engine::AudioFades fades;
        fades.fadeIn = 0.5;
        fades.fadeOut = 0.5;
        fades.length = 2.0;
        engine::renderAudioClip (clip, 0.0, false, 0.0, spb, rate, fades, l.data(), r.data(), 48000, stats);
        check (l[0] == 0.0f && l[47999] < 0.01f && std::abs (l[24000] - 0.5f) < 1.0e-6f, "fades in and out");
    });

    test ("tempo in file names", [] {
        check (engine::tempoFromFileName ("Drums_124bpm.wav") == 124.0, "_124bpm");
        check (engine::tempoFromFileName ("loop 90 BPM.aif") == 90.0, "90 BPM");
        check (engine::tempoFromFileName ("bass-128-bpm-Am.wav") == 128.0, "-128-bpm");
        check (engine::tempoFromFileName ("kick.wav") == 0.0, "none");
        check (engine::tempoFromFileName ("take2bpm.wav") == 0.0, "not part of a word / out of range");
    });

    test ("loudness: EBU reference signals, gating, loudness range and true peak (spec 04, test 5)", [] {
        for (const double rate : { 44100.0, 48000.0, 96000.0 })
        {
            // a stereo 1 kHz sine at -23 dBFS reads -23.0 LUFS at any rate
            const size_t n = (size_t) (20.0 * rate);
            std::vector<float> s (n);
            for (size_t i = 0; i < n; ++i)
                s[i] = dbToLin (-23.0) * (float) std::sin (2.0 * std::numbers::pi * 1000.0 * (double) i / rate);
            const auto r = dsp::measureLoudness (s.data(), s.data(), n, rate);
            check (std::abs (r.integratedLufs + 23.0) < 0.1, "sine -23 dBFS at " + std::to_string ((int) rate) + " Hz: " + std::to_string (r.integratedLufs));
            check (r.rangeLu < 0.5, "a steady sine has no loudness range: " + std::to_string (r.rangeLu));
        }
        {
            // EBU Tech 3341 case 3 in spirit: 10 s at -36, 60 s at -23, 10 s at -36 dBFS: the quiet parts are gated out
            const double rate = 48000.0;
            std::vector<float> s ((size_t) (80.0 * rate));
            for (size_t i = 0; i < s.size(); ++i)
            {
                const double t = (double) i / rate;
                const double db = (t >= 10.0 && t < 70.0) ? -23.0 : -36.0;
                s[i] = dbToLin (db) * (float) std::sin (2.0 * std::numbers::pi * 1000.0 * t);
            }
            const auto r = dsp::measureLoudness (s.data(), s.data(), s.size(), rate);
            check (std::abs (r.integratedLufs + 23.0) < 0.1, "the relative gate keeps -23: " + std::to_string (r.integratedLufs));
            // EBU Tech 3342 case 1: 20 s at -20 and 20 s at -30 LUFS give an LRA of 10 LU
            std::vector<float> t ((size_t) (40.0 * rate));
            for (size_t i = 0; i < t.size(); ++i)
            {
                const double sec = (double) i / rate;
                t[i] = dbToLin (sec < 20.0 ? -20.0 : -30.0) * (float) std::sin (2.0 * std::numbers::pi * 1000.0 * sec);
            }
            const auto lra = dsp::measureLoudness (t.data(), t.data(), t.size(), rate);
            check (std::abs (lra.rangeLu - 10.0) < 1.0, "LRA of -20 then -30: " + std::to_string (lra.rangeLu));
        }
        {
            // a sine at a quarter of the rate, 45 degrees off: the samples miss the crests by 3 dB
            std::vector<float> s (48000);
            for (size_t i = 0; i < s.size(); ++i)
                s[i] = 0.5f * (float) std::sin (std::numbers::pi / 2.0 * (double) i + std::numbers::pi / 4.0);
            const double sample = 20.0 * std::log10 (0.5 * std::sqrt (0.5));
            const double tp = dsp::truePeakDb (s.data(), s.size());
            check (std::abs (tp - sample - 3.01) < 0.1, "true peak 3 dB over the samples: " + std::to_string (tp - sample));
        }
        std::vector<float> silence (48000, 0.0f);
        check (dsp::measureLoudness (silence.data(), silence.data(), silence.size(), 48000.0).integratedLufs <= -70.0, "silence is -70 LUFS");
    });

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
