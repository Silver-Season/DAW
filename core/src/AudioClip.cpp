#include "daw/engine/AudioClip.h"

#include "daw/dsp/SincInterpolator.h"
#include "daw/engine/Clip.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numbers>

namespace daw::engine
{

AudioSource::AudioSource (int channels, int64_t numFrames, double rate)
    : numChannels (std::clamp (channels, 1, 2)), length (std::max<int64_t> (0, numFrames)), sampleRate (rate > 0.0 ? rate : 44100.0)
{
    // Not zeroed on purpose: a ten-minute file is a few hundred megabytes, and nothing reads past `available`.
    left = std::make_unique_for_overwrite<float[]> ((size_t) std::max<int64_t> (1, length));
    if (numChannels > 1)
        right = std::make_unique_for_overwrite<float[]> ((size_t) std::max<int64_t> (1, length));
    numPeaks = (length + kPeakFrames - 1) / kPeakFrames;
    peaks = std::make_unique<float[]> ((size_t) std::max<int64_t> (2, numPeaks * 2));
}

void AudioSource::publish (int64_t upTo) noexcept
{
    upTo = std::clamp<int64_t> (upTo, 0, length);
    // Only whole peak blocks, except at the very end of the file.
    int64_t peak = peaksAvailable.load (std::memory_order_relaxed);
    while (peak < numPeaks)
    {
        const int64_t from = peak * kPeakFrames, to = std::min (length, from + kPeakFrames);
        if (to > upTo)
            break;
        float lo = 0.0f, hi = 0.0f;
        for (int c = 0; c < numChannels; ++c)
        {
            const float* d = channel (c);
            for (int64_t i = from; i < to; ++i)
            {
                lo = std::min (lo, d[i]);
                hi = std::max (hi, d[i]);
            }
        }
        peaks[(size_t) (peak * 2)] = lo;
        peaks[(size_t) (peak * 2 + 1)] = hi;
        ++peak;
    }
    available.store (upTo, std::memory_order_release);
    peaksAvailable.store (peak, std::memory_order_release);
}

double framesPerBeat (const AudioPart& part, double samplesPerBeat, double engineRate) noexcept
{
    const double fileRate = part.source != nullptr ? part.source->sampleRate : engineRate;
    return samplesPerBeat / std::max (1.0, engineRate) * fileRate * std::max (1.0e-3, part.pitchRatio);
}

double beatsForFrames (double frames, double fileRate, double bpm) noexcept
{
    return frames / std::max (1.0, fileRate) * std::max (1.0, bpm) / 60.0;
}

namespace
{
    constexpr double kSeamSeconds = 0.002;

    float equalPower (double x) noexcept
    {
        return (float) std::sin (std::clamp (x, 0.0, 1.0) * std::numbers::pi * 0.5);
    }

    struct Reader
    {
        const AudioPart& part;
        const AudioSource& src;
        const dsp::SincInterpolator& sinc;
        double end, fpb;
        int64_t avail;
        bool exact; // the file plays at its own rate: whole frames, no interpolation
        AudioRenderStats& stats;

        double frameOf (double contentBeat) const noexcept
        {
            const double f = part.reverse ? part.sampleStart + (end - contentBeat) * fpb : part.sampleStart + contentBeat * fpb;
            return exact ? std::round (f) : f;
        }
        bool inFile (double frame) const noexcept { return frame >= 0.0 && frame < (double) src.length; }

        void read (double contentBeat, float& l, float& r) const noexcept
        {
            const double f = frameOf (contentBeat);
            if (! inFile (f))
            {
                l = r = 0.0f;
                return;
            }
            if (f >= (double) avail)
            {
                ++stats.missedFrames;
                l = r = 0.0f;
                return;
            }
            l = sinc.valueAt (src.channel (0), avail, f);
            r = src.numChannels > 1 ? sinc.valueAt (src.channel (1), avail, f) : l;
        }
    };
} // namespace

void renderAudioClip (const MidiClip& clip, double entry, bool loop, double elapsed, double spb, double engineRate, const AudioFades& fades,
                      float* outL, float* outR, int numSamples, AudioRenderStats& stats) noexcept
{
    if (clip.audio == nullptr || clip.audio->source == nullptr || numSamples <= 0 || spb <= 0.0)
        return;
    const auto& part = *clip.audio;
    const auto& src = *part.source;
    const double end = clip.endBeat(), braceStart = clip.braceStart(), brace = clip.braceBeats();
    const double fpb = framesPerBeat (part, spb, engineRate);
    const bool exact = std::abs (fpb / spb - 1.0) < 1.0e-12;
    const Reader reader { part, src, dsp::sharedSincInterpolator(), end, fpb, src.available.load (std::memory_order_acquire), exact, stats };

    const double seam = std::min (kSeamSeconds * engineRate / spb, brace * 0.5); // in beats
    // Audio before the brace to crossfade into at the seam; without it the tail fades out and the brace fades in.
    const bool hasPreRoll = loop && reader.inFile (reader.frameOf (braceStart - seam)) && reader.inFile (reader.frameOf (braceStart));
    const float gain = part.gain;

    for (int i = 0; i < numSamples; ++i)
    {
        const double e = elapsed + (double) i / spb;
        const double u = entry + e;
        double c = u;
        bool repeated = false;
        if (u >= end)
        {
            if (! loop)
                break;
            const double into = u - end;
            c = braceStart + (into - std::floor (into / brace) * brace);
            repeated = true;
        }

        float g = gain;
        if (fades.fadeIn > 0.0 && e < fades.fadeIn)
            g *= equalPower (e / fades.fadeIn);
        if (fades.length >= 0.0)
        {
            if (e >= fades.length)
                break;
            if (fades.fadeOut > 0.0 && e > fades.length - fades.fadeOut)
                g *= equalPower ((fades.length - e) / fades.fadeOut);
        }
        if (g == 0.0f)
            continue;

        float l = 0.0f, r = 0.0f;
        reader.read (c, l, r);
        if (loop && c > end - seam)
        {
            const double t = (end - c) / seam; // 1 at the start of the seam, 0 at the jump
            const float out = equalPower (t);
            l *= out;
            r *= out;
            if (hasPreRoll)
            {
                float pl = 0.0f, pr = 0.0f;
                reader.read (braceStart - (end - c), pl, pr);
                const float in = equalPower (1.0 - t);
                l += pl * in;
                r += pr * in;
            }
        }
        else if (repeated && ! hasPreRoll && c < braceStart + seam)
        {
            const float in = equalPower ((c - braceStart) / seam);
            l *= in;
            r *= in;
        }
        outL[i] += l * g;
        outR[i] += r * g;
    }
}

double tempoFromFileName (const std::string& name)
{
    std::string lower (name);
    std::transform (lower.begin(), lower.end(), lower.begin(), [] (unsigned char ch) { return (char) std::tolower (ch); });
    for (size_t at = lower.find ("bpm"); at != std::string::npos; at = lower.find ("bpm", at + 3))
    {
        size_t i = at;
        while (i > 0 && (lower[i - 1] == ' ' || lower[i - 1] == '_' || lower[i - 1] == '-'))
            --i;
        size_t digitsEnd = i;
        while (i > 0 && (std::isdigit ((unsigned char) lower[i - 1]) || lower[i - 1] == '.'))
            --i;
        if (i == digitsEnd)
            continue;
        if (i > 0 && std::isalpha ((unsigned char) lower[i - 1]))
            continue; // part of a word, not a number of its own
        const double bpm = std::atof (lower.substr (i, digitsEnd - i).c_str());
        if (bpm >= 40.0 && bpm <= 300.0)
            return bpm;
    }
    return 0.0;
}

} // namespace daw::engine
