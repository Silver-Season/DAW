#include "daw/dsp/TimeStretch.h"

#include <algorithm>
#include <cmath>

namespace daw::dsp
{
std::vector<std::vector<float>> stretchBeats (const std::vector<std::vector<float>>& in, double sampleRate,
                                              const std::vector<KeyFrame>& keyFrames, int64_t outFrames,
                                              const std::vector<int64_t>& onsets, double decay)
{
    const int channels = (int) in.size();
    outFrames = std::max<int64_t> (0, outFrames);
    std::vector<std::vector<float>> out ((size_t) channels, std::vector<float> ((size_t) outFrames, 0.0f));
    const int64_t n = channels > 0 ? (int64_t) in[0].size() : 0;
    if (n == 0 || outFrames == 0)
        return out;

    const auto fade = std::max<int64_t> (1, std::llround (0.003 * sampleRate)); // the cut of a slice played shorter
    const auto minSlice = std::max<int64_t> (fade * 2, std::llround (0.010 * sampleRate));
    decay = std::clamp (decay, 0.0, 1.0);

    // Slice starts: the onsets and the key frames (a marker always starts a slice, so it lands exactly).
    std::vector<int64_t> starts { 0 };
    for (const auto o : onsets)
        starts.push_back (o);
    for (const auto& k : keyFrames)
        starts.push_back (k.first);
    std::sort (starts.begin(), starts.end());
    std::vector<int64_t> slices;
    for (const auto s : starts)
        if (s >= 0 && s < n && (slices.empty() || s - slices.back() >= minSlice))
            slices.push_back (s);
    slices.push_back (n);

    const auto place = [&] (int64_t inputFrame) { return (int64_t) std::llround (mapThroughKeyFrames (keyFrames, (double) inputFrame)); };

    for (size_t k = 0; k + 1 < slices.size(); ++k)
    {
        const int64_t s0 = slices[k], s1 = slices[k + 1], lengthIn = s1 - s0;
        const int64_t o0 = place (s0), o1 = k + 2 == slices.size() ? std::max (place (s1), o0) : place (s1);
        const int64_t lengthOut = o1 - o0;
        if (lengthOut <= 0 || lengthIn <= 0)
            continue;

        // The tail that continues a slice played longer: its last part, at most half of it and 60 ms, back and forth.
        const int64_t window = std::max<int64_t> (1, std::min<int64_t> (lengthIn / 2, std::llround (0.060 * sampleRate)));
        const int64_t gap = std::max<int64_t> (0, lengthOut - lengthIn);
        // Decay 1 holds the tail at full level; below that it dies away over that share of the gap, 0 cuts it at once.
        const double tau = decay >= 1.0 ? 0.0 : std::max (1.0, decay * (double) gap / 3.0);

        for (int64_t j = 0; j < lengthOut; ++j)
        {
            const int64_t o = o0 + j;
            if (o < 0 || o >= outFrames)
                continue;
            int64_t src;
            double g = 1.0;
            if (j < lengthIn)
            {
                src = s0 + j;
                if (lengthOut < lengthIn && j >= lengthOut - fade) // cut short: fade out just before the next slice
                    g = (double) (lengthOut - j) / (double) fade;
            }
            else
            {
                const int64_t t = j - lengthIn, period = 2 * window;
                const int64_t p = t % period;
                src = s1 - 1 - (p < window ? p : period - 1 - p); // backwards from the end, then forwards again
                if (decay < 1.0)
                    g = decay <= 0.0 ? 0.0 : std::exp (-(double) t / tau);
                if (j >= lengthOut - fade) // the tail never runs into the next hit with a jump
                    g *= (double) (lengthOut - j) / (double) fade;
                if (decay <= 0.0 && t < fade) // no tail: the end of the slice still fades instead of stopping dead
                    g = 1.0 - (double) t / (double) fade;
            }
            src = std::clamp<int64_t> (src, 0, n - 1);
            for (int c = 0; c < channels; ++c)
                out[(size_t) c][(size_t) o] += (float) (in[(size_t) c][(size_t) src] * g);
        }
    }
    return out;
}
} // namespace daw::dsp
