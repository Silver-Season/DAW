// Shared helpers of the effect tests. Each test file registers its cases with FX_TEST; FxTestMain.cpp runs them all.
#pragma once

#include "daw/fx/Effect.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <numbers>
#include <random>
#include <string>
#include <vector>

namespace fxtest
{
inline int failures = 0, checks = 0;

inline void check (bool ok, const std::string& what)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::printf ("  FAIL: %s\n", what.c_str());
    }
}

struct Case
{
    const char* name;
    void (*fn)();
};
inline std::vector<Case>& registry()
{
    static std::vector<Case> cases;
    return cases;
}
struct Register
{
    Register (const char* name, void (*fn)()) { registry().push_back ({ name, fn }); }
};

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

inline float dbToLin (double db) { return (float) std::pow (10.0, db / 20.0); }
inline double linToDb (double g) { return g <= 1.0e-12 ? -240.0 : 20.0 * std::log10 (g); }

// A stereo signal as two vectors.
struct Stereo
{
    std::vector<float> l, r;
    explicit Stereo (size_t n = 0) : l (n, 0.0f), r (n, 0.0f) {}
    size_t size() const { return l.size(); }
};

inline Stereo sine (double hz, double db, size_t n, double rate = kRate)
{
    Stereo s (n);
    const float a = dbToLin (db);
    for (size_t i = 0; i < n; ++i)
        s.l[i] = s.r[i] = a * (float) std::sin (2.0 * std::numbers::pi * hz * (double) i / rate);
    return s;
}

inline Stereo noise (double db, size_t n, unsigned seed = 1)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
    Stereo s (n);
    const float a = dbToLin (db);
    for (size_t i = 0; i < n; ++i)
    {
        s.l[i] = a * dist (rng);
        s.r[i] = a * dist (rng);
    }
    return s;
}

inline Stereo impulse (size_t n, size_t at = 0, float value = 1.0f)
{
    Stereo s (n);
    if (at < n)
        s.l[at] = s.r[at] = value;
    return s;
}

// Runs the effect over the signal in blocks of `block` (in place on a copy), with an optional sidechain.
inline Stereo run (daw::fx::Effect& fx, const Stereo& in, int block = kBlock, const Stereo* sidechain = nullptr)
{
    Stereo out = in;
    for (size_t pos = 0; pos < in.size(); pos += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, in.size() - pos);
        float* io[2] { out.l.data() + pos, out.r.data() + pos };
        if (sidechain != nullptr)
        {
            const float* sc[2] { sidechain->l.data() + pos, sidechain->r.data() + pos };
            fx.process (io, 2, n, sc);
        }
        else
            fx.process (io, 2, n, nullptr);
    }
    return out;
}

inline std::unique_ptr<daw::fx::Effect> make (const char* type, double rate = kRate, int block = kBlock)
{
    auto fx = daw::fx::createEffect (type);
    if (fx != nullptr)
        fx->prepare (rate, block);
    return fx;
}

inline void set (daw::fx::Effect& fx, const char* id, float value)
{
    const int i = fx.paramIndex (id);
    check (i >= 0, std::string ("parameter ") + id + " exists in " + std::string (fx.type()));
    if (i >= 0)
        fx.setParam (i, value);
}

inline double rms (const std::vector<float>& x, size_t from = 0, size_t to = (size_t) -1)
{
    to = std::min (to, x.size());
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
        sum += (double) x[i] * x[i];
    return to > from ? std::sqrt (sum / (double) (to - from)) : 0.0;
}

inline double peak (const std::vector<float>& x, size_t from = 0, size_t to = (size_t) -1)
{
    to = std::min (to, x.size());
    double p = 0.0;
    for (size_t i = from; i < to; ++i)
        p = std::max (p, (double) std::abs (x[i]));
    return p;
}

inline bool finite (const Stereo& s)
{
    for (size_t i = 0; i < s.size(); ++i)
        if (! std::isfinite (s.l[i]) || ! std::isfinite (s.r[i]))
            return false;
    return true;
}

// Amplitude of the `hz` component of x[from, from + n) (single-bin DFT, Hann window).
inline double toneAmplitude (const std::vector<float>& x, double hz, size_t from, size_t n, double rate = kRate)
{
    double re = 0.0, im = 0.0, wsum = 0.0;
    for (size_t i = 0; i < n && from + i < x.size(); ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * (double) i / (double) (n - 1));
        const double ph = 2.0 * std::numbers::pi * hz * (double) (from + i) / rate;
        re += w * x[from + i] * std::cos (ph);
        im += w * x[from + i] * std::sin (ph);
        wsum += w;
    }
    return 2.0 * std::sqrt (re * re + im * im) / wsum;
}
} // namespace fxtest

#define FX_TEST_CAT2(a, b) a##b
#define FX_TEST_CAT(a, b) FX_TEST_CAT2 (a, b)
#define FX_TEST(name)                                                                                                   \
    static void FX_TEST_CAT (fxTestFn, __LINE__)();                                                                     \
    static const fxtest::Register FX_TEST_CAT (fxTestReg, __LINE__) (name, &FX_TEST_CAT (fxTestFn, __LINE__));          \
    static void FX_TEST_CAT (fxTestFn, __LINE__)()
