#include "daw/dsp/Wavetable.h"
#include "daw/dsp/FFT.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <functional>
#include <mutex>
#include <numbers>

namespace daw::dsp
{

namespace
{
    constexpr int N = Wavetable::kFrameSize;
    constexpr int kStride = N + 1;
    constexpr double kPi = std::numbers::pi;
}

std::shared_ptr<const Wavetable> Wavetable::fromFrames (std::string name, const std::vector<std::vector<float>>& frames)
{
    auto wt = std::shared_ptr<Wavetable> (new Wavetable());
    wt->name_ = std::move (name);
    wt->numFrames_ = (int) frames.size();
    wt->data_.assign ((size_t) wt->numFrames_ * kNumMips * kStride, 0.0f);

    std::vector<std::complex<double>> spectrum (N), work (N);

    for (int f = 0; f < wt->numFrames_; ++f)
    {
        const auto& src = frames[(size_t) f];
        for (int i = 0; i < N; ++i)
            spectrum[(size_t) i] = { i < (int) src.size() ? (double) src[(size_t) i] : 0.0, 0.0 };

        fft (spectrum, false);
        spectrum[0] = 0.0; // remove DC

        for (int m = 0; m < kNumMips; ++m)
        {
            const int maxHarmonic = (N / 2) >> m;
            work = spectrum;
            for (int h = maxHarmonic + 1; h <= N / 2; ++h)
            {
                work[(size_t) h] = 0.0;
                work[(size_t) ((N - h) % N)] = 0.0;
            }
            if (maxHarmonic == N / 2)
                work[N / 2] = 0.0; // Nyquist bin is never useful

            fft (work, true);

            float* dst = wt->data_.data() + ((size_t) f * kNumMips + (size_t) m) * kStride;
            for (int i = 0; i < N; ++i)
                dst[i] = (float) work[(size_t) i].real();
            dst[N] = dst[0];
        }
    }

    // Normalise the whole table by the peak of its full-bandwidth frames.
    float peak = 0.0f;
    for (int f = 0; f < wt->numFrames_; ++f)
    {
        const float* d = wt->data (f, 0);
        for (int i = 0; i < N; ++i)
            peak = std::max (peak, std::abs (d[i]));
    }
    if (peak > 0.0f)
        for (auto& s : wt->data_)
            s /= peak;

    return wt;
}

int Wavetable::mipForIncrement (double phaseInc) noexcept
{
    if (phaseInc <= 0.0)
        return 0;
    const double maxHarmonicBelowNyquist = 0.5 / phaseInc;
    for (int m = 0; m < kNumMips; ++m)
        if ((double) ((N / 2) >> m) <= maxHarmonicBelowNyquist)
            return m;
    return kNumMips - 1;
}

const float* Wavetable::data (int frame, int mip) const noexcept
{
    return data_.data() + ((size_t) frame * kNumMips + (size_t) mip) * kStride;
}

float Wavetable::sample (float framePos, double phase, int mip) const noexcept
{
    const float fp = std::clamp (framePos, 0.0f, 1.0f) * (float) (numFrames_ - 1);
    const int f0 = std::min ((int) fp, numFrames_ - 1);
    const int f1 = std::min (f0 + 1, numFrames_ - 1);
    const float ft = fp - (float) f0;

    const double idx = phase * N;
    const int i = std::clamp ((int) idx, 0, N - 1);
    const float t = (float) (idx - i);

    const float* a = data (f0, mip);
    const float va = a[i] + t * (a[i + 1] - a[i]);
    if (f1 == f0 || ft <= 0.0f)
        return va;

    const float* b = data (f1, mip);
    const float vb = b[i] + t * (b[i + 1] - b[i]);
    return va + ft * (vb - va);
}

//==============================================================================
namespace factory
{
namespace
{
    using Frame = std::vector<float>;
    constexpr int kFrames = 32;

    // Builds one frame from a harmonic series: x(t) = sum amp(h) * sin(2*pi*h*t).
    Frame fromHarmonics (const std::function<double (int)>& amp, int maxHarmonic = N / 2 - 1)
    {
        std::vector<std::complex<double>> bins (N, 0.0);
        for (int h = 1; h <= maxHarmonic; ++h)
        {
            const double a = amp (h);
            if (a == 0.0)
                continue;
            bins[(size_t) h] = std::complex<double> (0.0, -a * N / 2.0);
            bins[(size_t) (N - h)] = std::conj (bins[(size_t) h]);
        }
        fft (bins, true);
        Frame out (N);
        for (int i = 0; i < N; ++i)
            out[(size_t) i] = (float) bins[(size_t) i].real();
        return out;
    }

    Frame fromFunction (const std::function<double (double)>& fn)
    {
        Frame out (N);
        for (int i = 0; i < N; ++i)
            out[(size_t) i] = (float) fn ((double) i / N);
        return out;
    }

    Frame lerp (const Frame& a, const Frame& b, float t)
    {
        Frame out (N);
        for (int i = 0; i < N; ++i)
            out[(size_t) i] = a[(size_t) i] + t * (b[(size_t) i] - a[(size_t) i]);
        return out;
    }

    std::vector<Frame> morphThrough (const std::vector<Frame>& keys, int numFrames)
    {
        std::vector<Frame> frames;
        for (int k = 0; k < numFrames; ++k)
        {
            const float pos = (float) k / (float) (numFrames - 1) * (float) (keys.size() - 1);
            const int i = std::min ((int) pos, (int) keys.size() - 2);
            frames.push_back (lerp (keys[(size_t) i], keys[(size_t) i + 1], pos - (float) i));
        }
        return frames;
    }

    WavetablePtr basicShapes()
    {
        auto sine = fromHarmonics ([] (int h) { return h == 1 ? 1.0 : 0.0; });
        auto tri = fromHarmonics ([] (int h) { return (h % 2) ? ((((h - 1) / 2) % 2) ? -1.0 : 1.0) / (h * h) : 0.0; });
        auto saw = fromHarmonics ([] (int h) { return 1.0 / h; });
        auto square = fromHarmonics ([] (int h) { return (h % 2) ? 1.0 / h : 0.0; });

        // Bring the keys to a similar peak level so the morph does not jump in loudness.
        for (auto* f : { &sine, &tri, &saw, &square })
        {
            float p = 0.0f;
            for (auto s : *f) p = std::max (p, std::abs (s));
            for (auto& s : *f) s /= p;
        }
        return Wavetable::fromFrames ("Basic Shapes", morphThrough ({ sine, tri, saw, square }, kFrames));
    }

    WavetablePtr harmonicSweep()
    {
        std::vector<Frame> frames;
        for (int k = 0; k < kFrames; ++k)
        {
            const int count = 1 + (int) std::round (std::pow ((double) k / (kFrames - 1), 1.7) * 95.0);
            frames.push_back (fromHarmonics ([count] (int h) { return h <= count ? 1.0 / std::sqrt ((double) h) : 0.0; }));
        }
        return Wavetable::fromFrames ("Harmonic Sweep", frames);
    }

    WavetablePtr pwm()
    {
        std::vector<Frame> frames;
        for (int k = 0; k < kFrames; ++k)
        {
            const double width = 0.5 - 0.47 * (double) k / (kFrames - 1);
            frames.push_back (fromHarmonics ([width] (int h) { return std::sin (kPi * h * width) / h; }));
        }
        return Wavetable::fromFrames ("PWM", frames);
    }

    WavetablePtr formant()
    {
        // Formant frequencies (F1, F2, F3) of the vowels a, e, i, o, u, rendered on a 110 Hz harmonic grid.
        const std::array<std::array<double, 3>, 5> vowels { { { 800, 1150, 2900 },
                                                              { 400, 1600, 2700 },
                                                              { 280, 2250, 2900 },
                                                              { 450, 800, 2830 },
                                                              { 325, 700, 2530 } } };
        std::vector<Frame> frames;
        for (int k = 0; k < kFrames; ++k)
        {
            const double pos = (double) k / (kFrames - 1) * (vowels.size() - 1);
            const int i = std::min ((int) pos, (int) vowels.size() - 2);
            const double t = pos - i;
            std::array<double, 3> fmt {};
            for (int j = 0; j < 3; ++j)
                fmt[(size_t) j] = vowels[(size_t) i][(size_t) j] * (1 - t) + vowels[(size_t) i + 1][(size_t) j] * t;

            frames.push_back (fromHarmonics ([fmt] (int h) {
                const double f = 110.0 * h;
                const double env = std::exp (-std::pow ((f - fmt[0]) / 120.0, 2.0))
                                   + 0.7 * std::exp (-std::pow ((f - fmt[1]) / 150.0, 2.0))
                                   + 0.35 * std::exp (-std::pow ((f - fmt[2]) / 200.0, 2.0));
                return (env + 0.01) / std::sqrt ((double) h);
            },
                                             200));
        }
        return Wavetable::fromFrames ("Formant Vox", frames);
    }

    WavetablePtr syncSaw()
    {
        std::vector<Frame> frames;
        for (int k = 0; k < kFrames; ++k)
        {
            const double ratio = 1.0 + 7.0 * (double) k / (kFrames - 1);
            frames.push_back (fromFunction ([ratio] (double t) {
                const double x = t * ratio;
                return 2.0 * (x - std::floor (x)) - 1.0;
            }));
        }
        return Wavetable::fromFrames ("Sync Saw", frames);
    }

    WavetablePtr wavefold()
    {
        std::vector<Frame> frames;
        for (int k = 0; k < kFrames; ++k)
        {
            const double drive = 0.5 + 6.0 * (double) k / (kFrames - 1);
            frames.push_back (fromFunction ([drive] (double t) { return std::sin (kPi * drive * std::sin (2.0 * kPi * t)); }));
        }
        return Wavetable::fromFrames ("Wavefold", frames);
    }

    const std::vector<std::function<WavetablePtr()>>& builders()
    {
        static const std::vector<std::function<WavetablePtr()>> b { basicShapes, harmonicSweep, pwm, formant, syncSaw, wavefold };
        return b;
    }
} // namespace

const std::vector<std::string>& tableNames()
{
    static const std::vector<std::string> names { "Basic Shapes", "Harmonic Sweep", "PWM", "Formant Vox", "Sync Saw", "Wavefold" };
    return names;
}

WavetablePtr table (int index)
{
    static std::mutex mutex;
    static std::vector<WavetablePtr> cache (builders().size());

    index = std::clamp (index, 0, (int) builders().size() - 1);
    std::lock_guard lock (mutex);
    if (! cache[(size_t) index])
        cache[(size_t) index] = builders()[(size_t) index]();
    return cache[(size_t) index];
}

} // namespace factory
} // namespace daw::dsp
