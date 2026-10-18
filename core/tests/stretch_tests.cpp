// Tests for the time stretcher (07-warp.md, task 7.1): pitch kept, exact length, transients at the new tempo,
// stereo coherence, streaming use and offline speed.
#include "daw/dsp/TimeStretch.h"

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
using daw::dsp::StretchMode;

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

const char* modeName (StretchMode m)
{
    switch (m)
    {
        case StretchMode::beats: return "beats";
        case StretchMode::tones: return "tones";
        case StretchMode::texture: return "texture";
        case StretchMode::complex: return "complex";
        case StretchMode::complexPro: return "complexPro";
    }
    return "?";
}

constexpr StretchMode kAllModes[] = { StretchMode::beats, StretchMode::tones, StretchMode::texture,
                                      StretchMode::complex, StretchMode::complexPro };

std::vector<float> sine (double freq, double seconds, float amp = 0.5f)
{
    std::vector<float> x ((size_t) std::lround (seconds * kRate));
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = amp * (float) std::sin (2.0 * std::numbers::pi * freq * (double) i / kRate);
    return x;
}

// Dominant frequency in [from, to) between 300 and 600 Hz: Hann-windowed DFT magnitude scanned in 0.05 Hz steps
// (coarse 1 Hz pass, then fine around the peak). Zero crossings would be thrown off by the phase resets a phase
// vocoder may do, which do not change the pitch.
double measureFrequency (const std::vector<float>& x, size_t from, size_t to)
{
    to = std::min (to, x.size());
    const size_t n = to > from ? to - from : 0;
    std::vector<double> w (n);
    for (size_t i = 0; i < n; ++i)
        w[i] = x[from + i] * (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * (double) i / (double) n));
    auto magnitude = [&] (double f)
    {
        // Goertzel recurrence for one frequency.
        const double k = 2.0 * std::cos (2.0 * std::numbers::pi * f / kRate);
        double s1 = 0.0, s2 = 0.0;
        for (const double v : w)
        {
            const double s = v + k * s1 - s2;
            s2 = s1;
            s1 = s;
        }
        return s1 * s1 + s2 * s2 - k * s1 * s2;
    };
    auto scan = [&] (double lo, double hi, double step)
    {
        double best = lo, bestMag = -1.0;
        for (double f = lo; f <= hi; f += step)
            if (const double m = magnitude (f); m > bestMag)
            {
                bestMag = m;
                best = f;
            }
        return best;
    };
    const double coarse = scan (300.0, 600.0, 1.0);
    return scan (coarse - 1.0, coarse + 1.0, 0.05);
}

double rms (const std::vector<float>& x, size_t from, size_t to)
{
    double s = 0.0;
    for (size_t i = from; i < to; ++i)
        s += (double) x[i] * x[i];
    return to > from ? std::sqrt (s / (double) (to - from)) : 0.0;
}

bool allFinite (const std::vector<std::vector<float>>& chans)
{
    for (auto& ch : chans)
        for (float v : ch)
            if (! std::isfinite (v))
                return false;
    return true;
}

// A click (3 ms burst of fixed-seed noise, fast decay) every `period` seconds, the first at `offset`.
std::vector<float> clickTrain (double seconds, double period, double offset)
{
    std::vector<float> x ((size_t) std::lround (seconds * kRate), 0.0f);
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
    const int len = (int) (0.003 * kRate);
    for (double t = offset; t < seconds - 0.01; t += period)
    {
        const auto at = (size_t) std::lround (t * kRate);
        for (int i = 0; i < len && at + (size_t) i < x.size(); ++i)
            x[at + (size_t) i] = 0.8f * dist (rng) * (float) std::exp (-(double) i / (0.0006 * kRate));
    }
    return x;
}

// Onset of the click nearest `expected`: first sample within +-60 ms whose magnitude reaches 30 % of the window peak.
double findClick (const std::vector<float>& x, double expected)
{
    const auto lo = (size_t) std::max (0.0, (expected - 0.06) * kRate);
    const auto hi = std::min (x.size(), (size_t) ((expected + 0.06) * kRate));
    float peak = 0.0f;
    for (size_t i = lo; i < hi; ++i)
        peak = std::max (peak, std::abs (x[i]));
    if (peak < 1e-4f)
        return -1.0;
    for (size_t i = lo; i < hi; ++i)
        if (std::abs (x[i]) >= 0.3f * peak)
            return (double) i / kRate;
    return -1.0;
}
} // namespace

int main()
{
    test ("sine keeps its pitch and the length is exact (0.75 and 1.33, every mode)", []
    {
        const auto in = sine (440.0, 4.0);
        for (const auto mode : kAllModes)
            for (const double ratio : { 0.75, 1.33 })
            {
                const auto out = daw::dsp::stretchOffline ({ in }, kRate, ratio, mode);
                const auto want = (size_t) std::llround ((double) in.size() * ratio);
                const std::string tag = std::string (modeName (mode)) + " x" + std::to_string (ratio);
                check (out.size() == 1 && out[0].size() == want, tag + ": length " + std::to_string (out[0].size())
                                                                     + " != " + std::to_string (want));
                check (allFinite (out), tag + ": finite");
                const auto& y = out[0];
                const double f = measureFrequency (y, (size_t) (0.3 * kRate), y.size() - (size_t) (0.3 * kRate));
                check (std::abs (f - 440.0) / 440.0 < 0.005, tag + ": frequency " + std::to_string (f));
                // Level kept, and the tail delivered (no silent gap at the end from latency).
                const double mid = rms (y, y.size() / 3, 2 * y.size() / 3);
                const double tail = rms (y, y.size() - (size_t) (0.1 * kRate), y.size() - (size_t) (0.02 * kRate));
                check (mid > 0.25 && mid < 0.5, tag + ": rms " + std::to_string (mid));
                check (tail > 0.5 * mid, tag + ": tail rms " + std::to_string (tail));
            }
    });

    test ("click train at 120 BPM stretched to 124 BPM lands on the new beats within 5 ms", []
    {
        const double period = 60.0 / 120.0, offset = 0.25;
        const auto in = clickTrain (8.0, period, offset);
        const double ratio = 120.0 / 124.0;
        for (const auto mode : kAllModes)
        {
            const auto out = daw::dsp::stretchOffline ({ in }, kRate, ratio, mode);
            double worst = 0.0, sharpness = 1.0;
            int found = 0, total = 0;
            for (double t = offset; t * ratio < (double) out[0].size() / kRate - 0.07; t += period)
            {
                ++total;
                const double at = findClick (out[0], t * ratio);
                if (at < 0.0)
                    continue;
                ++found;
                worst = std::max (worst, std::abs (at - t * ratio));
                // A preserved transient keeps its energy packed right after the onset instead of smeared around it.
                const auto& y = out[0];
                const auto e = [&] (double from, double to)
                {
                    return rms (y, (size_t) std::max (0.0, from * kRate), std::min (y.size(), (size_t) (to * kRate)));
                };
                const double packed = e (at - 0.001, at + 0.006), spread = e (t * ratio - 0.06, t * ratio + 0.06);
                sharpness = std::min (sharpness, packed * packed * 0.007 / (spread * spread * 0.12));
            }
            std::printf ("  %s: %d/%d clicks, worst offset %.2f ms, min energy in 7 ms after onset %.0f %%\n",
                         modeName (mode), found, total, worst * 1000.0, sharpness * 100.0);
            check (total >= 14 && found == total, std::string (modeName (mode)) + ": every click found");
            check (worst < 0.005, std::string (modeName (mode)) + ": worst offset " + std::to_string (worst * 1000.0)
                                      + " ms");
            // R3 keeps clicks nearly intact; R2 in real-time mode smears them a little (Complex) and Texture is meant
            // to smooth them, so those two only guard against regressions.
            const double minSharp = mode == StretchMode::texture ? 0.5 : mode == StretchMode::complex ? 0.6 : 0.9;
            check (sharpness > minSharp, std::string (modeName (mode)) + ": transient kept sharp");
        }
    });

    test ("a mono source in both channels stays identical (phase coherent stereo)", []
    {
        // Two tones and a click train, so both the tonal and the transient paths are exercised.
        auto mono = sine (330.0, 3.0, 0.3f);
        const auto second = sine (1250.0, 3.0, 0.15f);
        const auto clicks = clickTrain (3.0, 0.5, 0.1);
        for (size_t i = 0; i < mono.size(); ++i)
            mono[i] += second[i] + 0.5f * clicks[i];

        for (const auto mode : kAllModes)
            for (const double ratio : { 0.8, 1.25 })
            {
                const auto out = daw::dsp::stretchOffline ({ mono, mono }, kRate, ratio, mode);
                double sxy = 0.0, sxx = 0.0, syy = 0.0, maxDiff = 0.0;
                for (size_t i = 0; i < out[0].size(); ++i)
                {
                    const double l = out[0][i], r = out[1][i];
                    sxy += l * r;
                    sxx += l * l;
                    syy += r * r;
                    maxDiff = std::max (maxDiff, std::abs (l - r));
                }
                const double corr = sxy / std::sqrt (sxx * syy + 1e-30);
                const std::string tag = std::string (modeName (mode)) + " x" + std::to_string (ratio);
                check (out.size() == 2 && out[0].size() == out[1].size(), tag + ": two equal channels");
                check (sxx > 1.0 && corr > 0.99, tag + ": correlation " + std::to_string (corr));
                check (maxDiff < 1e-3, tag + ": max L-R difference " + std::to_string (maxDiff));
            }
    });

    test ("streaming: small blocks, limited output room, ratio change and reset", []
    {
        const auto in = sine (440.0, 4.0);
        daw::dsp::Stretcher s;
        s.prepare (kRate, 1, StretchMode::complexPro, 1024);
        s.setRatio (1.5);
        check (s.latency() > 0, "latency reported: " + std::to_string (s.latency()));
        std::printf ("  complexPro latency at x1.5: %d input frames\n", s.latency());

        std::vector<float> out, block (300);
        float* outPtr[] = { block.data() };
        size_t read = 0;
        auto pump = [&] (size_t untilRead)
        {
            while (read < untilRead)
            {
                const int numIn = (int) std::min<size_t> (256, untilRead - read);
                const float* inPtr[] = { in.data() + read };
                read += (size_t) numIn;
                int got = s.process (inPtr, numIn, outPtr, (int) block.size());
                out.insert (out.end(), block.begin(), block.begin() + got);
                while (s.available() > 0) // the render thread drains whatever is ready
                {
                    got = s.process (inPtr, 0, outPtr, (int) block.size());
                    out.insert (out.end(), block.begin(), block.begin() + got);
                }
            }
        };

        pump (in.size() / 2); // 2 s at x1.5 = 3 s
        const double firstHalf = (double) out.size() / kRate;
        check (std::abs (firstHalf - 3.0) < 0.15, "first half output " + std::to_string (firstHalf) + " s");
        s.setRatio (0.5);
        const size_t before = out.size();
        pump (in.size()); // 2 s at x0.5 = 1 s
        const double secondHalf = (double) (out.size() - before) / kRate;
        check (std::abs (secondHalf - 1.0) < 0.15, "second half output " + std::to_string (secondHalf) + " s");
        const double f = measureFrequency (out, (size_t) (0.3 * kRate), out.size() - (size_t) (0.3 * kRate));
        check (std::abs (f - 440.0) / 440.0 < 0.005, "streamed frequency " + std::to_string (f));

        // After a reset the stream starts aligned again: the first output frames follow the new input at once.
        s.reset();
        check (s.available() == 0, "nothing buffered after reset");
        std::vector<float> clicks (44100, 0.0f);
        clicks[4410] = 1.0f;
        std::vector<float> res (88200, 0.0f);
        const float* inPtr[] = { clicks.data() };
        float* resPtr[] = { res.data() };
        int got = s.process (inPtr, (int) clicks.size(), resPtr, (int) res.size(), true);
        for (int idle = 0; ! s.finished() && idle < 4 && got < (int) res.size();)
        {
            float* p[] = { res.data() + got };
            const int more = s.process (inPtr, 0, p, (int) res.size() - got);
            got += more;
            idle = more == 0 ? idle + 1 : 0;
        }
        size_t peakAt = 0;
        for (size_t i = 0; i < res.size(); ++i)
            if (std::abs (res[i]) > std::abs (res[peakAt]))
                peakAt = i;
        // The impulse at 0.1 s plays at 0.05 s at ratio 0.5.
        check (std::abs ((double) peakAt / kRate - 0.05) < 0.005,
               "impulse after reset at " + std::to_string ((double) peakAt / kRate) + " s");
    });

    test ("offline: 10 s of stereo at 44.1 kHz, CPU time per mode", []
    {
        auto left = sine (220.0, 10.0, 0.3f), right = sine (331.0, 10.0, 0.3f);
        const auto clicks = clickTrain (10.0, 0.5, 0.0);
        for (size_t i = 0; i < left.size(); ++i)
        {
            left[i] += 0.5f * clicks[i];
            right[i] += 0.5f * clicks[i];
        }
        for (const auto mode : kAllModes)
        {
            const auto start = std::chrono::steady_clock::now();
            const auto out = daw::dsp::stretchOffline ({ left, right }, kRate, 1.1, mode);
            const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
            std::printf ("  %-10s 10 s stereo x1.1: %.3f s (%.1fx real time)\n", modeName (mode), secs, 10.0 / secs);
            check (out.size() == 2 && out[0].size() == (size_t) std::llround (left.size() * 1.1),
                   std::string (modeName (mode)) + ": length");
            check (allFinite (out), std::string (modeName (mode)) + ": finite");
        }
    });

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
