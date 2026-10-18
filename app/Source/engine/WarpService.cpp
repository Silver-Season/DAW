#include "WarpService.h"

#include "daw/dsp/Onsets.h"
#include "daw/dsp/TimeStretch.h"

namespace
{
daw::dsp::StretchMode modeOf (const juce::String& m)
{
    if (m == "beats")
        return daw::dsp::StretchMode::beats;
    if (m == "tones")
        return daw::dsp::StretchMode::tones;
    if (m == "texture")
        return daw::dsp::StretchMode::texture;
    if (m == "complexpro")
        return daw::dsp::StretchMode::complexPro;
    return daw::dsp::StretchMode::complex;
}
} // namespace

WarpService::WarpService (juce::AudioFormatManager& f) : formats (f) {}

WarpService::~WarpService()
{
    *alive = false;
    pool.removeAllJobs (true, 30000);
}

void WarpService::waitForJobs()
{
    while (pool.getNumJobs() > 0)
        juce::Thread::sleep (5);
}

void WarpService::analyse (const juce::File& file, std::function<void (const Analysis&)> done)
{
    auto flag = alive;
    pool.addJob ([this, file, done, flag] {
        Analysis result;
        if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file)); reader != nullptr && reader->lengthInSamples > 0)
        {
            // Mono, at most 20 minutes: plenty for a tempo, and the memory stays bounded.
            const auto n = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 1200.0));
            juce::AudioBuffer<float> buffer ((int) juce::jlimit (1u, 2u, reader->numChannels), n);
            reader->read (&buffer, 0, n, 0, true, buffer.getNumChannels() > 1);
            if (buffer.getNumChannels() > 1)
            {
                buffer.addFrom (0, 0, buffer, 1, 0, n);
                buffer.applyGain (0, 0, n, 0.5f);
            }
            const auto estimate = daw::dsp::estimateTempo (buffer.getReadPointer (0), n, reader->sampleRate,
                                                           file.getFileNameWithoutExtension().toStdString());
            if (estimate.bpm > 0.0 && estimate.confidence > 0.05)
            {
                result.bpm = estimate.bpm;
                result.firstBeatSeconds = estimate.firstBeatSeconds;
            }
        }
        juce::MessageManager::callAsync ([done, result, flag] {
            if (*flag && done)
                done (result);
        });
    });
}

std::shared_ptr<daw::engine::AudioSource> WarpService::stretched (const std::shared_ptr<daw::engine::AudioSource>& original, double timeRatio,
                                                                  const juce::String& mode)
{
    if (original == nullptr || ! original->isComplete() || original->length <= 0)
        return nullptr;
    const Key key { original.get(), std::lround (timeRatio * 100000.0), mode };
    if (const auto it = cache.find (key); it != cache.end())
    {
        it->second.used = true;
        return it->second.result;
    }
    if (renderInline)
    {
        auto& entry = cache[key];
        entry.original = original;
        entry.result = render (*original, timeRatio, mode);
        return entry.result;
    }
    // One render per file at a time: while the tempo is being dragged, only the last value gets rendered (the finished
    // render triggers a rebuild, which asks again for the then current ratio).
    for (const auto& [k, e] : cache)
        if (k.source == original.get() && e.result == nullptr)
            return nullptr;
    auto& entry = cache[key];
    entry.original = original;
    entry.used = true;

    auto flag = alive;
    pool.addJob ([this, key, original, timeRatio, mode, flag] {
        auto copy = render (*original, timeRatio, mode);
        juce::MessageManager::callAsync ([this, key, copy, flag] {
            if (! *flag)
                return;
            if (const auto it = cache.find (key); it != cache.end())
            {
                it->second.result = copy;
                if (onStretchReady)
                    onStretchReady();
            }
        });
    });
    return nullptr;
}

std::shared_ptr<daw::engine::AudioSource> WarpService::render (const daw::engine::AudioSource& original, double timeRatio, const juce::String& mode)
{
    std::vector<std::vector<float>> in ((size_t) original.numChannels);
    for (int c = 0; c < original.numChannels; ++c)
        in[(size_t) c].assign (original.channel (c), original.channel (c) + original.length);
    const auto out = daw::dsp::stretchOffline (in, original.sampleRate, timeRatio, modeOf (mode));
    const auto frames = out.empty() ? (int64_t) 0 : (int64_t) out[0].size();
    auto copy = std::make_shared<daw::engine::AudioSource> (original.numChannels, frames, original.sampleRate);
    for (int c = 0; c < original.numChannels && frames > 0; ++c)
    {
        float* dest = c == 0 ? copy->left.get() : copy->right.get();
        std::copy (out[(size_t) c].begin(), out[(size_t) c].end(), dest);
    }
    copy->publish (frames);
    return copy;
}

void WarpService::beginUse()
{
    for (auto& [key, entry] : cache)
        entry.used = false;
}

void WarpService::endUse()
{
    // A render still running stays: it will be wanted again in a moment (a tempo being typed in, say).
    for (auto it = cache.begin(); it != cache.end();)
        it = ! it->second.used && it->second.result != nullptr ? cache.erase (it) : std::next (it);
}
