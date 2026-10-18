#include "daw/dsp/Onsets.h"

#include "daw/dsp/FFT.h"
#include "daw/engine/AudioClip.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <complex>
#include <numbers>

namespace daw::dsp
{
namespace
{
constexpr int kFrame = 1024, kHop = 256;
constexpr double kMinBpm = 60.0, kMaxBpm = 200.0;
// Log-normal preference over tempo that favours 90..150. It settles ambiguous metrical levels and weighs the
// bar-length alternatives of short loops.
constexpr double kPreferredCentre = 125.0;
constexpr double kPreferredWidth = 0.5; // octaves
constexpr double kPreferredLow = 90.0, kPreferredHigh = 150.0;

// Octave decision (see oddShare in estimateTempo): synthetic and real patterns put a real double-time level at 0.58
// and above (house, techno, drum & bass at 174 over 87) and a hip-hop beat at 2T at 0.48 and below.
constexpr double kOddShareForDouble = 0.53, kOddShareIfPreferred = 0.35;
// Above 150 the faster level has to be clearer still: a 100 BPM loop with loud off-beat hats must not read as 200,
// while drum & bass at 174 (snares on the in-between beats of 87) stays well above it.
constexpr double kOddShareAboveFast = 0.6;

struct Flux
{
    std::vector<float> full; // spectral flux of frame k (centred on sample k * kHop) over all bins
    std::vector<float> low;  // the same below 200 Hz: kick drums, used to find the downbeat
    std::vector<float> body; // the same below 2.5 kHz: kicks, snares, bass and chords, but hardly any hats
    double frameRate = 0.0;
};

Flux spectralFlux (const float* x, int64_t n, double rate)
{
    Flux f;
    f.frameRate = rate / kHop;
    const auto numFrames = (size_t) (n / kHop + 1);
    f.full.assign (numFrames, 0.0f);
    f.low.assign (numFrames, 0.0f);
    f.body.assign (numFrames, 0.0f);

    std::vector<double> window (kFrame);
    double windowSum = 0.0;
    for (int i = 0; i < kFrame; ++i)
    {
        window[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / kFrame);
        windowSum += window[(size_t) i];
    }

    constexpr int bins = kFrame / 2;
    const int lowBins = std::clamp ((int) std::ceil (200.0 * kFrame / rate), 1, bins);
    const int bodyBins = std::clamp ((int) std::ceil (2500.0 * kFrame / rate), lowBins, bins);
    const double scale = 2.0 / windowSum; // a full-scale sine reads 1
    constexpr double compression = 1000.0;

    std::vector<std::complex<double>> buf (kFrame);
    std::vector<float> prev (bins + 1, 0.0f), magA (bins + 1), magB (bins + 1);

    const auto sampleAt = [&] (int64_t frame, int i) -> double {
        const int64_t s = frame * kHop - kFrame / 2 + i;
        return s >= 0 && s < n ? (double) x[s] * window[(size_t) i] : 0.0;
    };

    const auto accumulate = [&] (size_t frame, std::vector<float>& mag) {
        float sum = 0.0f, lowSum = 0.0f, bodySum = 0.0f;
        for (int j = 1; j <= bins; ++j)
        {
            const float d = mag[(size_t) j] - prev[(size_t) j];
            if (d > 0.0f)
            {
                sum += d;
                if (j <= lowBins)
                    lowSum += d;
                if (j <= bodyBins)
                    bodySum += d;
            }
        }
        f.full[frame] = sum;
        f.low[frame] = lowSum;
        f.body[frame] = bodySum;
        std::swap (prev, mag);
    };

    // Two real frames per complex transform: frame k in the real part, frame k + 1 in the imaginary part.
    for (size_t k = 0; k < numFrames; k += 2)
    {
        const bool pair = k + 1 < numFrames;
        for (int i = 0; i < kFrame; ++i)
            buf[(size_t) i] = { sampleAt ((int64_t) k, i), pair ? sampleAt ((int64_t) k + 1, i) : 0.0 };
        fft (buf, false);

        for (int j = 1; j <= bins; ++j)
        {
            const auto a = buf[(size_t) j];
            const auto b = std::conj (buf[(size_t) ((kFrame - j) % kFrame)]);
            magA[(size_t) j] = (float) std::log1p (compression * scale * 0.5 * std::abs (a + b));
            magB[(size_t) j] = (float) std::log1p (compression * scale * 0.5 * std::abs (a - b));
        }

        accumulate (k, magA);
        if (pair)
            accumulate (k + 1, magB);
    }
    return f;
}

struct Peak
{
    double time;
    float strength;
};

// Onsets with their flux strength. Frame peaks are found on the flux, then each is moved to the sharpest rise of
// short-time energy near it: the flux frame only says "somewhere in this 23 ms window".
std::vector<Peak> findOnsets (const float* x, int64_t n, double rate, const Flux& f, float sensitivity)
{
    std::vector<Peak> out;
    const auto& flux = f.full;
    const auto numFrames = (int64_t) flux.size();
    if (numFrames < 3)
        return out;

    double mean = 0.0;
    float peak = 0.0f;
    for (auto v : flux)
    {
        mean += v;
        peak = std::max (peak, v);
    }
    mean /= (double) numFrames;
    if (peak <= 0.0f)
        return out;

    const double multiplier = std::pow (2.0, (0.5 - std::clamp ((double) sensitivity, 0.0, 1.0)) * 2.0);
    const double offset = 0.5 * mean + 0.02 * peak;

    std::vector<int64_t> frames;
    float window[11];
    for (int64_t k = 0; k < numFrames; ++k)
    {
        int count = 0;
        for (int64_t j = k - 5; j <= k + 5; ++j)
            if (j >= 0 && j < numFrames)
                window[count++] = flux[(size_t) j];
        std::nth_element (window, window + count / 2, window + count);
        const double threshold = multiplier * (1.5 * window[count / 2] + offset);

        const float v = flux[(size_t) k];
        if (v <= threshold)
            continue;
        bool isMax = true;
        for (int64_t j = std::max<int64_t> (0, k - 2); j <= std::min (numFrames - 1, k + 2) && isMax; ++j)
            if (j != k && (flux[(size_t) j] > v || (j < k && flux[(size_t) j] == v)))
                isMax = false;
        if (isMax)
            frames.push_back (k);
    }

    // Cumulative energy for the time-domain refinement, pre-emphasised: the slow swell of a bass sine inside 2 ms
    // windows would otherwise look like a rise and pull a quiet hat onto the kick's waveform.
    std::vector<double> energy ((size_t) n + 1, 0.0);
    for (int64_t i = 0; i < n; ++i)
    {
        const double v = (double) x[i] - (i > 0 ? 0.97 * (double) x[i - 1] : 0.0);
        energy[(size_t) i + 1] = energy[(size_t) i] + v * v;
    }
    const auto sumOf = [&] (int64_t a, int64_t b) {
        return energy[(size_t) std::clamp<int64_t> (b, 0, n)] - energy[(size_t) std::clamp<int64_t> (a, 0, n)];
    };
    const auto w = std::max<int64_t> (8, (int64_t) std::lround (0.002 * rate));

    for (size_t i = 0; i < frames.size(); ++i)
    {
        const int64_t centre = frames[i] * kHop;
        int64_t lo = centre - kFrame / 2 - kHop, hi = centre + kFrame / 2;
        if (i > 0)
            lo = std::max (lo, (frames[i - 1] * kHop + centre) / 2);
        if (i + 1 < frames.size())
            hi = std::min (hi, (frames[i + 1] * kHop + centre) / 2);
        lo = std::max<int64_t> (0, lo);
        hi = std::min (n - 1, hi);

        int64_t best = std::clamp<int64_t> (centre, 0, std::max<int64_t> (0, n - 1));
        double bestRise = -1.0e300;
        for (int64_t t = lo; t <= hi; ++t)
        {
            const double rise = sumOf (t, t + w) - sumOf (t - w, t);
            if (rise > bestRise)
            {
                bestRise = rise;
                best = t;
            }
        }

        const Peak p { (double) best / rate, flux[(size_t) frames[i]] };
        if (! out.empty() && p.time - out.back().time < 0.05)
        {
            if (p.strength > out.back().strength)
                out.back() = p;
            continue;
        }
        out.push_back (p);
    }
    return out;
}

void gaussianSmooth (std::vector<float>& v, double sigma)
{
    if (sigma <= 0.0 || v.empty())
        return;
    const int radius = std::max (1, (int) std::ceil (3.0 * sigma));
    std::vector<double> kernel ((size_t) (2 * radius + 1));
    double total = 0.0;
    for (int i = -radius; i <= radius; ++i)
        total += kernel[(size_t) (i + radius)] = std::exp (-0.5 * (i / sigma) * (i / sigma));
    std::vector<float> out (v.size());
    const auto size = (int64_t) v.size();
    for (int64_t k = 0; k < size; ++k)
    {
        double s = 0.0;
        for (int i = -radius; i <= radius; ++i)
            if (k + i >= 0 && k + i < size)
                s += kernel[(size_t) (i + radius)] * v[(size_t) (k + i)];
        out[(size_t) k] = (float) (s / total);
    }
    v.swap (out);
}

// Onset strength for tempo and beats. Summed over bins, a broadband hat outweighs a kick, and the grid then locks onto
// the off-beats; adding the low band on equal footing puts the kick back in charge. The moving average is removed
// (half-wave rectified) so slow swells of pads and fades carry no periodicity.
// `withoutHats` builds it from the band below 2.5 kHz instead of all bins: what is left of an off-beat hat there is small.
std::vector<float> beatEnvelope (const Flux& f, bool withoutHats = false)
{
    const auto& band = withoutHats ? f.body : f.full;
    const auto size = band.size();
    double fullMean = 0.0, lowMean = 0.0;
    for (size_t k = 0; k < size; ++k)
    {
        fullMean += band[k];
        lowMean += f.low[k];
    }
    std::vector<float> env (size, 0.0f);
    if (fullMean <= 0.0)
        return env;
    for (size_t k = 0; k < size; ++k)
        env[k] = (float) (band[k] * (double) size / fullMean + (lowMean > 0.0 ? f.low[k] * (double) size / lowMean : 0.0));
    gaussianSmooth (env, 1.0);

    const auto radius = (int64_t) std::lround (0.4 * f.frameRate);
    std::vector<double> sum (size + 1, 0.0);
    for (size_t k = 0; k < size; ++k)
        sum[k + 1] = sum[k] + env[k];
    std::vector<float> out (size);
    for (int64_t k = 0; k < (int64_t) size; ++k)
    {
        const auto a = (size_t) std::max<int64_t> (0, k - radius), b = (size_t) std::min<int64_t> ((int64_t) size, k + radius + 1);
        out[(size_t) k] = std::max (0.0f, env[(size_t) k] - (float) ((sum[b] - sum[a]) / (double) (b - a)));
    }
    return out;
}

double preference (double bpm)
{
    const double d = std::log2 (bpm / kPreferredCentre) / kPreferredWidth;
    return std::exp (-0.5 * d * d);
}

// Normalised autocorrelation (1 at lag 0, unbiased) of the mean-removed onset envelope, via FFT.
std::vector<double> autocorrelation (const std::vector<float>& env, size_t maxLag)
{
    const size_t size = env.size();
    double mean = 0.0;
    for (auto v : env)
        mean += v;
    mean /= (double) size;

    size_t fftSize = 1;
    while (fftSize < 2 * size)
        fftSize <<= 1;
    std::vector<std::complex<double>> buf (fftSize);
    for (size_t i = 0; i < size; ++i)
        buf[i] = env[i] - mean;
    fft (buf, false);
    for (auto& c : buf)
        c = std::norm (c);
    fft (buf, true);

    std::vector<double> r (maxLag + 1, 0.0);
    const double zero = buf[0].real() / (double) size;
    if (zero <= 0.0)
        return r;
    for (size_t l = 0; l <= maxLag && l < size; ++l)
        r[l] = buf[l].real() / (double) (size - l) / zero;
    return r;
}

double lagValue (const std::vector<double>& r, double lag)
{
    const auto i = (size_t) lag;
    if (i + 1 >= r.size())
        return 0.0;
    const double frac = lag - (double) i;
    return r[i] * (1.0 - frac) + r[i + 1] * frac;
}

// Mean autocorrelation at the multiples of the beat period: periodicity of the whole beat grid, not of one lag.
double combScore (const std::vector<double>& r, double frameRate, double bpm)
{
    const double period = 60.0 * frameRate / bpm;
    const double maxLag = (double) r.size() - 2.0;
    const int multiples = std::clamp ((int) (maxLag / period), 1, 8);
    if (period > maxLag)
        return 0.0;
    double s = 0.0;
    for (int m = 1; m <= multiples; ++m)
        s += lagValue (r, m * period);
    return s / multiples;
}

// Ellis, "Beat Tracking by Dynamic Programming" (2007): the beat sequence maximising onset strength at the beats
// minus a penalty for inter-beat intervals that stray from the period. Returns beat frames, ascending.
std::vector<int64_t> trackBeats (const std::vector<float>& env, double period)
{
    std::vector<int64_t> beats;
    const auto size = (int64_t) env.size();
    if (size == 0 || period < 2.0)
        return beats;

    std::vector<float> local (env);
    double mean = 0.0, sq = 0.0;
    for (auto v : local)
    {
        mean += v;
        sq += (double) v * v;
    }
    mean /= (double) size;
    const double sd = std::sqrt (std::max (1.0e-12, sq / (double) size - mean * mean));
    for (auto& v : local)
        v = (float) (v / sd);
    gaussianSmooth (local, period / 32.0);

    constexpr double tightness = 100.0;
    std::vector<double> score ((size_t) size, 0.0);
    std::vector<int64_t> back ((size_t) size, -1);
    for (int64_t t = 0; t < size; ++t)
    {
        const auto from = t - (int64_t) std::lround (2.0 * period), to = t - (int64_t) std::lround (period / 2.0);
        double best = 0.0;
        int64_t bestAt = -1;
        for (int64_t tau = std::max<int64_t> (0, from); tau <= to; ++tau)
        {
            const double ratio = std::log ((double) (t - tau) / period);
            const double s = score[(size_t) tau] - tightness * ratio * ratio;
            if (bestAt < 0 || s > best)
            {
                best = s;
                bestAt = tau;
            }
        }
        if (bestAt >= 0 && best > 0.0)
        {
            score[(size_t) t] = local[(size_t) t] + best;
            back[(size_t) t] = bestAt;
        }
        else
        {
            score[(size_t) t] = local[(size_t) t];
        }
    }

    int64_t end = size - 1;
    for (int64_t t = std::max<int64_t> (0, size - (int64_t) std::ceil (period)); t < size; ++t)
        if (score[(size_t) t] > score[(size_t) end])
            end = t;
    for (int64_t t = end; t >= 0; t = back[(size_t) t])
        beats.push_back (t);
    std::reverse (beats.begin(), beats.end());

    // The chain runs on through silence at either end; keep beats from the first to the last with real onsets.
    std::vector<float> strength;
    for (auto b : beats)
    {
        float s = 0.0f;
        for (int64_t j = std::max<int64_t> (0, b - 2); j <= std::min (size - 1, b + 2); ++j)
            s = std::max (s, local[(size_t) j]);
        strength.push_back (s);
    }
    std::vector<float> sorted (strength);
    std::sort (sorted.begin(), sorted.end());
    const float floor = sorted.empty() ? 0.0f : 0.15f * sorted[sorted.size() / 2];
    size_t first = 0, last = beats.size();
    while (first < last && strength[first] <= floor)
        ++first;
    while (last > first && strength[last - 1] <= floor)
        --last;
    return { beats.begin() + (std::ptrdiff_t) first, beats.begin() + (std::ptrdiff_t) last };
}

struct Grid
{
    std::vector<double> times;
    std::vector<bool> onOnset; // the beat sits on a detected onset (its time is the onset's)
};

// Beat frames to seconds: beats near an onset take its precise time, the rest move by the typical frame-to-onset offset.
Grid placeBeats (const std::vector<int64_t>& frames, double frameRate, const std::vector<Peak>& onsets, double period)
{
    Grid g;
    const auto nearest = [&] (double t) -> const Peak* {
        auto it = std::lower_bound (onsets.begin(), onsets.end(), t, [] (const Peak& p, double v) { return p.time < v; });
        const Peak* best = nullptr;
        if (it != onsets.end())
            best = &*it;
        if (it != onsets.begin() && (best == nullptr || t - std::prev (it)->time < best->time - t))
            best = &*std::prev (it);
        return best;
    };

    std::vector<double> offsets;
    for (auto f : frames)
        if (auto* p = nearest ((double) f / frameRate); p != nullptr && std::abs (p->time - (double) f / frameRate) < 0.05)
            offsets.push_back (p->time - (double) f / frameRate);
    double offset = 0.0;
    if (! offsets.empty())
    {
        std::nth_element (offsets.begin(), offsets.begin() + (std::ptrdiff_t) (offsets.size() / 2), offsets.end());
        offset = offsets[offsets.size() / 2];
    }

    const double tolerance = std::min (0.025, 0.2 * period / frameRate);
    for (auto f : frames)
    {
        const double t = (double) f / frameRate + offset;
        const Peak* p = nearest (t);
        const bool snap = p != nullptr && std::abs (p->time - t) < tolerance;
        g.times.push_back (snap ? p->time : std::max (0.0, t));
        g.onOnset.push_back (snap);
    }
    return g;
}

// Least-squares beat period through the beats that sit on onsets; 0 if there are too few.
double regressPeriod (const Grid& g, double period)
{
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    int count = 0;
    double origin = -1.0;
    for (size_t i = 0; i < g.times.size(); ++i)
    {
        if (! g.onOnset[i])
            continue;
        if (origin < 0.0)
            origin = g.times[i];
        const double x = std::round ((g.times[i] - origin) / period), y = g.times[i] - origin;
        sx += x;
        sy += y;
        sxx += x * x;
        sxy += x * y;
        ++count;
    }
    const double den = count * sxx - sx * sx;
    if (count < 6 || den <= 0.0)
        return 0.0;
    return (count * sxy - sx * sy) / den;
}

// The beat period, in seconds, whose grid catches the most onset strength over the whole file, searched within 2 %
// of `period`. Each onset adds its strength (square root, so a few loud hits do not decide alone) times a Gaussian of
// its distance to the nearest grid line (8 ms wide) at the best phase. Unlike the beat tracker, one stretch of
// syncopated or missing beats cannot pull it off: over a whole track a period error of 0.1 % already moves the end
// of the grid off the hits.
double gridScore (const std::vector<Peak>& onsets, double period, double* phase = nullptr)
{
    constexpr double binSeconds = 0.001, sigma = 0.008;
    const auto bins = (int) std::ceil (period / binSeconds);
    if (bins < 8)
        return 0.0;
    std::vector<double> hist ((size_t) bins, 0.0);
    for (const auto& o : onsets)
    {
        const double r = std::fmod (o.time, period) / period * bins;
        hist[(size_t) std::clamp ((int) r, 0, bins - 1)] += std::sqrt (std::max (0.0f, o.strength));
    }
    const int reach = (int) std::ceil (3.0 * sigma / binSeconds);
    std::vector<double> kernel ((size_t) (2 * reach + 1));
    for (int k = -reach; k <= reach; ++k)
        kernel[(size_t) (k + reach)] = std::exp (-0.5 * std::pow (k * binSeconds / sigma, 2.0));
    double best = 0.0;
    for (int b = 0; b < bins; ++b)
    {
        double s = 0.0;
        for (int k = -reach; k <= reach; ++k)
            s += hist[(size_t) (((b + k) % bins + bins) % bins)] * kernel[(size_t) (k + reach)];
        if (s > best)
        {
            best = s;
            if (phase != nullptr)
                *phase = (b + 0.5) * period / bins;
        }
    }
    return best;
}

double fitPeriod (const std::vector<Peak>& onsets, double period)
{
    if (onsets.size() < 8 || period <= 0.0)
        return period;
    double best = period, bestScore = -1.0;
    for (double f = 0.98; f <= 1.02 + 1.0e-12; f += 0.0001)
        if (const double s = gridScore (onsets, period * f); s > bestScore)
        {
            bestScore = s;
            best = period * f;
        }
    return best;
}

// A tempo written as a bare number before a dash or underscore ("124-Am loop.wav", "124_kick.wav").
double tempoFromLeadingNumber (const std::string& name)
{
    for (size_t i = 0; i < name.size(); ++i)
    {
        if (! std::isdigit ((unsigned char) name[i]) || (i > 0 && std::isalnum ((unsigned char) name[i - 1])))
            continue;
        size_t j = i;
        while (j < name.size() && std::isdigit ((unsigned char) name[j]))
            ++j;
        if (j < name.size() && (name[j] == '-' || name[j] == '_') && j - i >= 2 && j - i <= 3)
        {
            const double bpm = std::atof (name.substr (i, j - i).c_str());
            if (bpm >= kMinBpm && bpm <= kMaxBpm)
                return bpm;
        }
        i = j;
    }
    return 0.0;
}

double tempoFromName (const std::string& fileName)
{
    if (const double bpm = engine::tempoFromFileName (fileName); bpm > 0.0)
        return bpm;
    return tempoFromLeadingNumber (fileName);
}

// An integer tempo within 1 % of `bpm` at which the file is a whole number of 4/4 bars (1..64), or 0.
double tempoFromLength (double seconds, double bpm)
{
    double best = 0.0;
    for (int candidate = (int) std::floor (bpm * 0.99); candidate <= (int) std::ceil (bpm * 1.01); ++candidate)
    {
        if (candidate <= 0 || std::abs (candidate - bpm) > 0.01 * bpm)
            continue;
        const double barSeconds = 240.0 / candidate;
        const double bars = std::round (seconds / barSeconds);
        if (bars < 1.0 || bars > 64.0 || std::abs (seconds - bars * barSeconds) > 0.003)
            continue;
        if (best == 0.0 || std::abs (candidate - bpm) < std::abs (best - bpm))
            best = candidate;
    }
    return best;
}

} // namespace

std::vector<double> detectOnsets (const float* mono, int64_t numSamples, double sampleRate, float sensitivity)
{
    std::vector<double> times;
    if (mono == nullptr || numSamples <= 0 || sampleRate <= 0.0)
        return times;
    const auto flux = spectralFlux (mono, numSamples, sampleRate);
    for (const auto& p : findOnsets (mono, numSamples, sampleRate, flux, sensitivity))
        times.push_back (p.time);
    return times;
}

TempoEstimate estimateTempo (const float* mono, int64_t numSamples, double sampleRate, const std::string& fileName)
{
    TempoEstimate result;
    if (mono == nullptr || numSamples < 2 * kFrame || sampleRate <= 0.0)
        return result;

    const auto flux = spectralFlux (mono, numSamples, sampleRate);
    const auto onsets = findOnsets (mono, numSamples, sampleRate, flux, 0.5f);
    const double frameRate = flux.frameRate;

    const auto env = beatEnvelope (flux);
    const auto bodyEnv = beatEnvelope (flux, true);
    const auto maxLag = (size_t) std::min ((double) env.size() * 0.75, 8.0 * frameRate);
    const auto r = autocorrelation (env, maxLag);

    // The strongest periodicity first, unweighted: it is the root of a family of metrical levels (beat, half note,
    // bar) related by octaves, and keeping to that family rules out 3:2 readings of dense eighth-note patterns.
    std::vector<double> raw;
    double root = 0.0, rootScore = 0.0;
    for (double b = kMinBpm; b <= kMaxBpm + 1.0e-9; b += 0.1)
    {
        const double s = combScore (r, frameRate, b);
        raw.push_back (s);
        if (s > rootScore)
        {
            rootScore = s;
            root = b;
        }
    }

    double bpm = 0.0;
    if (root > 0.0)
    {
        const auto localBest = [&] (double around) {
            double best = around, bestScore = -1.0e9;
            for (double b = around * 0.98; b <= around * 1.02; b += 0.02)
                if (const double s = combScore (r, frameRate, b); s > bestScore)
                {
                    bestScore = s;
                    best = b;
                }
            return best;
        };
        // Onset strength on the odd beats of a grid at `b` against the even ones (the smaller over the larger, so the
        // grid's phase does not matter): near 1 when every beat is a real beat (four on the floor, snares on 2 and 4),
        // low when the in-between beats only carry off-beat hats. The autocorrelation cannot tell these apart: it
        // sees the same eighth-note lattice in both. The strength is read below 2.5 kHz: in the log spectrum a noisy
        // hat spreads over so many bins that over all of them it weighs as much as a kick.
        const auto oddShare = [&] (double b) {
            const auto beats = trackBeats (env, 60.0 * frameRate / b);
            double sums[2] = {};
            int counts[2] = {};
            for (size_t i = 0; i < beats.size(); ++i)
            {
                float v = 0.0f;
                for (int64_t j = std::max<int64_t> (0, beats[i] - 2); j <= std::min ((int64_t) bodyEnv.size() - 1, beats[i] + 2); ++j)
                    v = std::max (v, bodyEnv[(size_t) j]);
                sums[i % 2] += v;
                ++counts[i % 2];
            }
            if (counts[0] < 2 || counts[1] < 2)
                return 0.0;
            const double a = sums[0] / counts[0], c = sums[1] / counts[1];
            return std::max (a, c) > 0.0 ? std::min (a, c) / std::max (a, c) : 0.0;
        };

        bpm = root;
        while (bpm / 2.0 >= kMinBpm)
            bpm = localBest (bpm / 2.0);

        // Octave errors: climb from the slowest level to 2T, 4T... while the faster level's in-between beats are
        // real; a faster level inside 90..150 needs less evidence.
        while (bpm * 2.0 <= kMaxBpm)
        {
            const double faster = localBest (bpm * 2.0);
            const double share = oddShare (faster);
            const bool preferred = bpm < kPreferredLow && faster >= kPreferredLow && faster <= kPreferredHigh;
            const double needed = faster > kPreferredHigh ? kOddShareAboveFast : kOddShareForDouble;
            if (share >= needed || (preferred && share >= kOddShareIfPreferred))
                bpm = faster;
            else
                break;
        }

        std::vector<double> sorted (raw);
        std::sort (sorted.begin(), sorted.end());
        const double median = sorted[sorted.size() / 2];
        const double top = combScore (r, frameRate, bpm);
        result.confidence = std::clamp ((top - median) / std::max (1.0e-9, 1.0 - median), 0.0, 1.0);
    }

    // Beat tracking, then the tempo from the beat positions: far finer than the 5.8 ms frame grid.
    Grid grid;
    const auto track = [&] (double tempo) { grid = placeBeats (trackBeats (bodyEnv, 60.0 * frameRate / tempo), frameRate, onsets, 60.0 * frameRate / tempo); };
    if (bpm > 0.0)
    {
        track (bpm);
        if (const double p = regressPeriod (grid, 60.0 / bpm); p > 0.0 && std::abs (60.0 / p / bpm - 1.0) < 0.03)
            bpm = 60.0 / p;

        // Then the grid over the whole file settles the period; tracks made at an integer tempo snap to it when its
        // grid fits as well (within 2 % of the score).
        const double fitted = 60.0 / fitPeriod (onsets, 60.0 / bpm);
        const double fittedScore = gridScore (onsets, 60.0 / fitted);
        bpm = fitted;
        double snapped = 0.0;
        for (double whole = std::ceil (fitted * 0.995); whole <= std::floor (fitted * 1.005); whole += 1.0)
            if (gridScore (onsets, 60.0 / whole) >= 0.98 * fittedScore && (snapped == 0.0 || std::abs (whole - fitted) < std::abs (snapped - fitted)))
                snapped = whole;
        if (snapped > 0.0)
            bpm = snapped;
    }

    const double tracked = bpm;
    const double seconds = (double) numSamples / sampleRate;
    if (const double named = tempoFromName (fileName); named > 0.0)
    {
        bool related = bpm <= 0.0;
        for (double ratio : { 1.0, 2.0, 0.5, 1.5, 2.0 / 3.0 })
            related = related || std::abs (bpm * ratio / named - 1.0) < 0.04;
        if (related || result.confidence < 0.5)
        {
            bpm = named;
            result.confidence = 1.0;
        }
    }
    else if (bpm > 0.0)
    {
        // Only a confident estimate snaps: any length is a whole number of bars at some tempo.
        double fromLength = result.confidence >= 0.25 ? tempoFromLength (seconds, bpm) : 0.0;

        // A loop of a few bars gives the autocorrelation little to go on, and syncopated patterns then read at a
        // related tempo (3:2, 4:3, octaves). When the estimate is not a whole number of bars but a related tempo is,
        // and that tempo scores nearly as well, the length decides.
        if (fromLength == 0.0 && result.confidence >= 0.25 && seconds * bpm / 240.0 <= 8.5)
        {
            const double own = combScore (r, frameRate, bpm) * preference (bpm);
            double bestScore = 0.0;
            for (double ratio : { 2.0, 0.5, 1.5, 2.0 / 3.0, 4.0 / 3.0, 0.75 })
            {
                const double candidate = tempoFromLength (seconds, bpm * ratio);
                if (candidate < kMinBpm || candidate > kMaxBpm)
                    continue;
                const double score = combScore (r, frameRate, candidate) * preference (candidate);
                if (score >= 0.7 * own && score > bestScore)
                {
                    bestScore = score;
                    fromLength = candidate;
                }
            }
        }

        if (fromLength > 0.0)
        {
            bpm = fromLength;
            result.confidence = std::max (result.confidence, 0.9);
        }
    }

    if (bpm <= 0.0)
        return result;
    if (std::abs (bpm / tracked - 1.0) > 0.0005 || grid.times.empty())
        track (bpm);

    result.bpm = bpm;
    result.beats = grid.times;

    // A track at a steady tempo: nearly all of its onsets sit on the eighth-note lattice of the fitted grid. Its beats
    // are then that grid itself (on the onset where there is one), on whichever of the two eighth phases carries the
    // kicks: the beat tracker can slip to the off-beats through a break and stay there.
    if (const double period = 60.0 / bpm; onsets.size() >= 8)
    {
        double phase = 0.0;
        gridScore (onsets, period, &phase);
        const auto offLattice = [&] (double t, double step) {
            const double d = std::fmod (std::abs (t - phase), step);
            return std::min (d, step - d);
        };
        double on = 0.0, total = 0.0;
        for (const auto& o : onsets)
        {
            const double w = std::sqrt (std::max (0.0f, o.strength));
            total += w;
            on += offLattice (o.time, period / 2.0) < 0.015 ? w : 0.0;
        }
        if (total > 0.0 && on >= 0.8 * total)
        {
            const double first = onsets.front().time, last = onsets.back().time;
            const auto lines = [&] (double start) {
                std::vector<double> out;
                for (double t = start - std::floor ((start - first + 0.012) / period) * period; t <= last + 0.012; t += period)
                    if (t >= first - 0.012)
                        out.push_back (t);
                return out;
            };
            const auto kick = [&] (const std::vector<double>& ts) {
                double sum = 0.0;
                for (auto t : ts)
                {
                    const auto frame = (int64_t) std::lround (t * frameRate);
                    float v = 0.0f;
                    for (int64_t j = std::max<int64_t> (0, frame - 1); j <= std::min ((int64_t) flux.low.size() - 1, frame + 3); ++j)
                        v = std::max (v, flux.low[(size_t) j]);
                    sum += v;
                }
                return ts.empty() ? 0.0 : sum / (double) ts.size();
            };
            auto beats = lines (phase);
            if (const auto other = lines (phase + period / 2.0); kick (other) > 1.1 * kick (beats))
                beats = other;
            for (auto& t : beats)
            {
                auto it = std::lower_bound (onsets.begin(), onsets.end(), t - 0.012, [] (const Peak& p, double v) { return p.time < v; });
                if (it != onsets.end() && it->time <= t + 0.012)
                    t = it->time;
                t = std::max (0.0, t);
            }
            result.beats = beats;
        }
    }

    // Downbeat: the beat phase (of four) with the most low-frequency attack; bar 1 wins near-ties since exported
    // tracks usually start on it.
    if (result.beats.size() >= 4)
    {
        double phaseScore[4] = {};
        int phaseCount[4] = {};
        for (size_t i = 0; i < result.beats.size(); ++i)
        {
            const auto frame = (int64_t) std::lround (result.beats[i] * frameRate);
            float s = 0.0f;
            for (int64_t j = std::max<int64_t> (0, frame - 1); j <= std::min ((int64_t) flux.low.size() - 1, frame + 3); ++j)
                s = std::max (s, flux.low[(size_t) j]);
            phaseScore[i % 4] += s;
            ++phaseCount[i % 4];
        }
        int phase = 0;
        double best = 0.0;
        for (int p = 0; p < 4; ++p)
        {
            phaseScore[p] /= std::max (1, phaseCount[p]);
            best = std::max (best, phaseScore[p]);
        }
        if (phaseScore[0] < 0.85 * best)
            for (int p = 1; p < 4; ++p)
                if (phaseScore[p] == best)
                    phase = p;
        result.firstBeatSeconds = result.beats[(size_t) phase];
    }
    else if (! result.beats.empty())
    {
        result.firstBeatSeconds = result.beats.front();
    }
    else if (! onsets.empty())
    {
        result.firstBeatSeconds = onsets.front().time;
    }
    return result;
}

} // namespace daw::dsp
