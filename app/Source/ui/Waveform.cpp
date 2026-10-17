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

    g.setColour (ink);
    for (int x = x0; x < x1; ++x)
    {
        const double e = ((double) x - (double) area.getX()) * beatsPerPixel;
        double c = entry + e;
        if (c >= end)
        {
            if (! loop)
                break;
            const double into = c - end;
            c = braceStart + (into - std::floor (into / brace) * brace);
        }
        const double frame = reverse ? sampleStart + (end - c) * fpb - framesPerPixel : sampleStart + c * fpb;
        const int64_t p0 = (int64_t) std::floor (juce::jmax (0.0, frame) / daw::engine::AudioSource::kPeakFrames);
        const int64_t p1 = (int64_t) std::floor (juce::jmax (0.0, frame + framesPerPixel) / daw::engine::AudioSource::kPeakFrames);
        if (frame + framesPerPixel < 0.0 || p0 >= peaksReady)
            continue;
        float lo = 0.0f, hi = 0.0f;
        for (int64_t p = p0; p <= std::min (p1, peaksReady - 1); ++p)
        {
            lo = std::min (lo, src.peaks[(size_t) (p * 2)]);
            hi = std::max (hi, src.peaks[(size_t) (p * 2 + 1)]);
        }
        const float top = mid - juce::jmin (1.0f, hi * gain) * half, bottom = mid - juce::jmax (-1.0f, lo * gain) * half;
        g.fillRect ((float) x, top, 1.0f, juce::jmax (1.0f, bottom - top));
    }
}
} // namespace ui
