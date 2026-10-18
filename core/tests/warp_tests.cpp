// Tests for warp markers and the Beats mode (07-warp.md, tasks 7.1, 7.3 and 7.6): the warp map, slicing, stretching
// along key frames (markers land where they were put) and the Beats mode (hits on the new beats, attacks kept).
#include "daw/dsp/TimeStretch.h"
#include "daw/engine/WarpMap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <numbers>
#include <random>
#include <string>
#include <vector>

namespace
{
using daw::dsp::KeyFrame;
using daw::dsp::StretchMode;
using daw::engine::WarpMap;
using daw::engine::WarpMarker;

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

// Clicks: a 2 ms decaying burst at each position, on quiet noise (-40 dB).
std::vector<float> clicks (const std::vector<int64_t>& at, int64_t length)
{
    std::vector<float> x ((size_t) length);
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> noise (-0.01f, 0.01f);
    for (auto& v : x)
        v = noise (rng);
    for (const auto p : at)
        for (int i = 0; i < 400 && p + i < length; ++i)
            x[(size_t) (p + i)] += 0.8f * std::exp (-(float) i / 60.0f) * (float) std::sin (2.0 * std::numbers::pi * 2000.0 * i / kRate);
    return x;
}

// Where each click starts in a signal: the first sample above half the peak after silence.
std::vector<int64_t> findClicks (const std::vector<float>& x)
{
    std::vector<int64_t> out;
    int64_t last = -100000;
    for (int64_t i = 0; i < (int64_t) x.size(); ++i)
        if (std::abs (x[(size_t) i]) > 0.2f && i - last > 4000)
        {
            // walk back to the start of the burst
            int64_t s = i;
            while (s > 0 && std::abs (x[(size_t) (s - 1)]) > 0.02f && i - s < 20)
                --s;
            out.push_back (s);
            last = i;
        }
    return out;
}

float peakIn (const std::vector<float>& x, int64_t from, int64_t count)
{
    float p = 0.0f;
    for (int64_t i = std::max<int64_t> (0, from); i < std::min<int64_t> ((int64_t) x.size(), from + count); ++i)
        p = std::max (p, std::abs (x[(size_t) i]));
    return p;
}
} // namespace

int main()
{
    test ("warp map: linear without markers, piecewise with them, round trip", [] {
        const WarpMap line (1000.0, 500.0);
        check (line.isLinear(), "no markers is linear");
        check (std::abs (line.frameAt (2.0) - 2000.0) < 1e-9, "linear frame");
        check (std::abs (line.beatAt (2000.0) - 2.0) < 1e-9, "linear beat");

        const WarpMap m ({ { 0.0, 0.0 }, { 1000.0, 1.0 }, { 3000.0, 2.0 } }, 0.0, 1000.0);
        check (! m.isLinear(), "markers make it piecewise");
        check (std::abs (m.frameAt (0.5) - 500.0) < 1e-9, "first segment");
        check (std::abs (m.frameAt (1.5) - 2000.0) < 1e-9, "second segment");
        check (std::abs (m.frameAt (3.0) - 5000.0) < 1e-9, "after the last marker: the last slope");
        check (std::abs (m.frameAt (-1.0) + 1000.0) < 1e-9, "before the first marker: the first slope");
        for (double b = -2.0; b < 5.0; b += 0.37)
            check (std::abs (m.beatAt (m.frameAt (b)) - b) < 1e-9, "round trip at " + std::to_string (b));
        check (std::abs (m.framesPerBeatAt (1.5) - 2000.0) < 1e-9, "slope of a segment");

        const WarpMap backwards ({ { 0.0, 0.0 }, { 2000.0, 1.0 }, { 1000.0, 2.0 } }, 0.0, 1000.0);
        check (backwards.markers().size() == 2, "a marker that runs backwards is dropped");

        const auto doubled = m.scaledInBeats (2.0);
        check (std::abs (doubled.frameAt (3.0) - 2000.0) < 1e-9, "scaled in beats");
        check (m.hash() != doubled.hash(), "hash changes with the map");
    });

    test ("slicing by onsets and by grid", [] {
        const auto s = daw::engine::sliceByOnsets ({ 1.0, 1.02, 2.0, 3.99 }, 0.0, 4.0, 0.05);
        check (s.size() == 3, "close onsets and one at the end are merged: " + std::to_string (s.size()));
        if (s.size() == 3)
            check (s[0].start == 0.0 && s[0].end == 1.0 && s[1].end == 2.0 && s[2].end == 4.0, "slice bounds");
        const auto g = daw::engine::sliceByGrid (0.5, 3.0, 1.0);
        check (g.size() == 3 && g[0].end == 1.0 && g[1].end == 2.0 && g[2].end == 3.0, "grid slices aligned to the division");
    });

    test ("stretching along key frames puts every marker where it was put", [] {
        // Clicks every half second; a marker moves the third click later and the rest follows.
        const int64_t n = (int64_t) (4.0 * kRate);
        std::vector<int64_t> at;
        for (int i = 0; i < 8; ++i)
            at.push_back ((int64_t) (0.5 * i * kRate) + 1000);
        const auto x = clicks (at, n);
        const int64_t out = (int64_t) (5.0 * kRate);
        // the click at at[2] moves to 1.6 s; the end of the file to 5 s
        const std::vector<KeyFrame> keys { { 0, 0 }, { at[2], (int64_t) (1.6 * kRate) }, { n, out } };
        for (const auto mode : { StretchMode::complex, StretchMode::complexPro, StretchMode::beats })
        {
            const auto y = daw::dsp::stretchOfflineMapped ({ x }, kRate, keys, out, mode);
            check ((int64_t) y[0].size() == out, "exact length");
            const auto found = findClicks (y[0]);
            const auto want = (int64_t) (1.6 * kRate);
            bool near = false;
            for (const auto f : found)
                near = near || std::abs (f - want) < (int64_t) (0.005 * kRate);
            check (near, "the marked click lands at 1.6 s (mode " + std::to_string ((int) mode) + ")");
        }
    });

    test ("Beats: hits land on the new beats within 1 ms and keep their attack", [] {
        // 16 clicks at 92 BPM (one per beat), stretched to 124 BPM: every beat moves to b * 60/124 s.
        const double from = 92.0, to = 124.0;
        const double beatIn = 60.0 / from * kRate, beatOut = 60.0 / to * kRate;
        std::vector<int64_t> at;
        for (int i = 0; i < 16; ++i)
            at.push_back ((int64_t) std::llround (i * beatIn));
        const int64_t n = (int64_t) std::llround (16 * beatIn), out = (int64_t) std::llround (16 * beatOut);
        const auto x = clicks (at, n);
        const std::vector<KeyFrame> keys { { 0, 0 }, { n, out } };
        const auto y = daw::dsp::stretchBeats ({ x }, kRate, keys, out, at, 1.0);
        const auto found = findClicks (y[0]);
        check (found.size() == 16, "16 hits: " + std::to_string (found.size()));
        double worst = 0.0;
        for (size_t i = 0; i < std::min<size_t> (16, found.size()); ++i)
            worst = std::max (worst, std::abs ((double) found[i] - (double) i * beatOut) / kRate * 1000.0);
        check (worst <= 1.0, "hit error " + std::to_string (worst) + " ms");
        const float original = peakIn (x, at[5], 200), stretched = peakIn (y[0], (int64_t) std::llround (5 * beatOut), 200);
        check (stretched >= 0.9f * original, "attack kept: " + std::to_string (stretched / original));

        // Slower: the gaps hold the tail with decay 1 and are near silent with decay 0.
        const int64_t slow = (int64_t) std::llround (16 * beatIn * 1.5);
        const auto held = daw::dsp::stretchBeats ({ x }, kRate, { { 0, 0 }, { n, slow } }, slow, at, 1.0);
        const auto cut = daw::dsp::stretchBeats ({ x }, kRate, { { 0, 0 }, { n, slow } }, slow, at, 0.0);
        const int64_t inGap = (int64_t) std::llround (5 * beatIn * 1.5 + beatIn * 1.2);
        float heldLevel = 0.0f, cutLevel = 0.0f;
        for (int64_t i = inGap; i < inGap + 2000; ++i)
        {
            heldLevel = std::max (heldLevel, std::abs (held[0][(size_t) i]));
            cutLevel = std::max (cutLevel, std::abs (cut[0][(size_t) i]));
        }
        check (heldLevel > 0.002f, "decay 1 fills the gap");
        check (cutLevel < 1.0e-6f, "decay 0 leaves it silent");
    });

    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
