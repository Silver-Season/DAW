// Tests for the sinc interpolator and the audio analysis helpers (loopback latency, phrase detection, peaks).
#include "daw/dsp/SincInterpolator.h"
#include "daw/engine/AudioAnalysis.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
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

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
