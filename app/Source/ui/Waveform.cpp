#include "Waveform.h"

namespace ui
{
void drawAudioWaveform (juce::Graphics& g, juce::Rectangle<float> area, const daw::engine::AudioSource& src, const ClipRef& clip, double entry,
                        bool loop, double spanBeats, double bpm, juce::Colour ink)
{
    if (area.getWidth() < 2.0f || area.getHeight() < 2.0f || spanBeats <= 0.0 || src.numPeaks <= 0)
        return;
    const int64_t peaksReady = src.peaksAvailable.load (std::memory_order_acquire);
    if (peaksReady <= 0)
        return;
    const double end = std::max (daw::engine::MidiClip::kMinLength, clip.lengthBeats());
    const double braceStart = juce::jlimit (0.0, end - daw::engine::MidiClip::kMinLength, clip.loopStart());
    const double brace = end - braceStart;
    const double fpb = src.sampleRate * 60.0 / juce::jmax (1.0, bpm) * clip.pitchRatio();
    const double sampleStart = clip.sampleStart();
    const bool reverse = clip.reverse();
    const float gain = juce::jlimit (0.0f, 4.0f, std::pow (10.0f, (float) clip.gainDb() / 20.0f));

    const auto clipBox = g.getClipBounds().toFloat().getIntersection (area);
    const int x0 = (int) std::floor (clipBox.getX()), x1 = (int) std::ceil (clipBox.getRight());
    const double beatsPerPixel = spanBeats / (double) area.getWidth();
    const double framesPerPixel = beatsPerPixel * fpb;
    const float mid = area.getCentreY(), half = area.getHeight() * 0.5f;

    // Zoomed out, a pixel covers whole blocks of the overview. Zoomed in further than one block per pixel the overview
    // would draw steps, so the samples themselves are read: their min / max per pixel, and below a sample per pixel a
    // line through them.
    const bool fromSamples = framesPerPixel < (double) daw::engine::AudioSource::kPeakFrames;
    const int64_t framesReady = src.available.load (std::memory_order_acquire);
    const float* l = src.channel (0);
    const float* r = src.channel (1);
    const auto sampleAt = [&] (double f) {
        const auto i = (int64_t) std::floor (f);
        if (i < 0 || i + 1 >= framesReady)
            return 0.0f;
        const float t = (float) (f - (double) i);
        const float a = 0.5f * (l[i] + r[i]), b = 0.5f * (l[i + 1] + r[i + 1]);
        return a + (b - a) * t;
    };
    const auto frameAt = [&] (int x) {
        double c = entry + ((double) x - (double) area.getX()) * beatsPerPixel;
        if (c >= end)
        {
            if (! loop)
                return -1.0e18;
            const double into = c - end;
            c = braceStart + (into - std::floor (into / brace) * brace);
        }
        return reverse ? sampleStart + (end - c) * fpb - framesPerPixel : sampleStart + c * fpb;
    };

    g.setColour (ink);
    if (fromSamples && framesPerPixel < 1.0)
    {
        juce::Path line;
        bool started = false;
        for (int x = x0; x <= x1; ++x)
        {
            const double frame = frameAt (x);
            if (frame < -1.0e17)
                break;
            const float y = mid - juce::jlimit (-1.0f, 1.0f, sampleAt (reverse ? frame + framesPerPixel : frame) * gain) * half;
            if (! started)
                line.startNewSubPath ((float) x, y);
            else
                line.lineTo ((float) x, y);
            started = true;
        }
        g.strokePath (line, juce::PathStrokeType (1.25f, juce::PathStrokeType::curved));
        return;
    }
    for (int x = x0; x < x1; ++x)
    {
        const double frame = frameAt (x);
        if (frame < -1.0e17)
            break;
        if (frame + framesPerPixel < 0.0)
            continue;
        float lo = 0.0f, hi = 0.0f;
        if (fromSamples)
        {
            const auto f0 = (int64_t) std::floor (juce::jmax (0.0, frame));
            const auto f1 = std::min (framesReady, (int64_t) std::ceil (frame + framesPerPixel) + 1);
            if (f0 >= framesReady)
                continue;
            for (int64_t f = f0; f < f1; ++f)
            {
                lo = std::min (lo, std::min (l[f], r[f]));
                hi = std::max (hi, std::max (l[f], r[f]));
            }
        }
        else
        {
            const int64_t p0 = (int64_t) std::floor (juce::jmax (0.0, frame) / daw::engine::AudioSource::kPeakFrames);
            const int64_t p1 = (int64_t) std::floor (juce::jmax (0.0, frame + framesPerPixel) / daw::engine::AudioSource::kPeakFrames);
            if (p0 >= peaksReady)
                continue;
            for (int64_t p = p0; p <= std::min (p1, peaksReady - 1); ++p)
            {
                lo = std::min (lo, src.peaks[(size_t) (p * 2)]);
                hi = std::max (hi, src.peaks[(size_t) (p * 2 + 1)]);
            }
        }
        const float top = mid - juce::jmin (1.0f, hi * gain) * half, bottom = mid - juce::jmax (-1.0f, lo * gain) * half;
        g.fillRect ((float) x, top, 1.0f, juce::jmax (1.0f, bottom - top));
    }
}
} // namespace ui
