#include "AudioPreview.h"

#include "daw/dsp/SincInterpolator.h"

#include <algorithm>

void AudioPreview::play (std::shared_ptr<const daw::engine::AudioSource> source)
{
    const auto serial = requestSerial.load() + 1;
    // A source of request s is read by the audio thread until it has seen request s + 2 (at s + 1 it still fades out).
    const auto seen = seenSerial.load();
    held.erase (std::remove_if (held.begin(), held.end(), [seen] (const Held& h) { return h.serial + 2 <= seen; }), held.end());
    held.push_back ({ source, serial });
    requested.store (source.get());
    requestSerial.store (serial);
}

void AudioPreview::stop()
{
    play (nullptr);
}

void AudioPreview::renderVoice (Voice& v, juce::AudioBuffer<float>& out, int numSamples, double engineRate, float target) noexcept
{
    if (v.source == nullptr)
        return;
    const auto& src = *v.source;
    const auto& sinc = daw::dsp::sharedSincInterpolator();
    const int64_t avail = src.available.load (std::memory_order_acquire);
    const double step = src.sampleRate / engineRate;
    const float rampStep = 1.0f / (float) juce::jmax (1.0, 0.01 * engineRate); // 10 ms
    auto* l = out.getWritePointer (0);
    auto* r = out.getWritePointer (1);
    for (int i = 0; i < numSamples; ++i)
    {
        if (v.position >= (double) src.length || (v.fading && v.level <= 0.0f))
        {
            v.source = nullptr;
            return;
        }
        v.level = v.fading ? juce::jmax (0.0f, v.level - rampStep) : juce::jmin (1.0f, v.level + rampStep * 5.0f);
        if (v.position < (double) avail)
        {
            const float a = sinc.valueAt (src.channel (0), avail, v.position);
            const float b = src.numChannels > 1 ? sinc.valueAt (src.channel (1), avail, v.position) : a;
            l[i] += a * v.level * target;
            r[i] += b * v.level * target;
        }
        v.position += step;
    }
}

void AudioPreview::process (juce::AudioBuffer<float>& out, int numSamples, double engineRate) noexcept
{
    if (const auto serial = requestSerial.load(); serial != handledSerial)
    {
        handledSerial = serial;
        // The voice that played fades out; a third one still fading is cut (it is at most 10 ms from silence anyway).
        previous = current;
        previous.fading = true;
        current = {};
        current.source = requested.load();
        seenSerial.store (serial);
    }
    const float g = gain.load();
    renderVoice (previous, out, numSamples, engineRate, g);
    renderVoice (current, out, numSamples, engineRate, g);
    sounding.store (current.source != nullptr || previous.source != nullptr, std::memory_order_relaxed);
}
