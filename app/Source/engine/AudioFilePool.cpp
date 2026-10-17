#include "AudioFilePool.h"

#include "daw/dsp/SincInterpolator.h"

AudioFilePool::AudioFilePool()
{
    formatManager.registerBasicFormats();
    daw::dsp::sharedSincInterpolator(); // built here, never on the audio thread
}

AudioFilePool::~AudioFilePool()
{
    alive->store (false);
    loader.removeAllJobs (true, 10000);
}

std::shared_ptr<daw::engine::AudioSource> AudioFilePool::find (const juce::File& file) const
{
    const auto it = cache.find (file.getFullPathName());
    return it != cache.end() ? it->second.source.lock() : nullptr;
}

std::shared_ptr<daw::engine::AudioSource> AudioFilePool::acquire (const juce::File& file)
{
    if (! file.existsAsFile())
        return nullptr;
    const auto key = file.getFullPathName();
    const auto modified = file.getLastModificationTime();
    const auto size = file.getSize();
    if (const auto it = cache.find (key); it != cache.end())
        if (auto source = it->second.source.lock(); source != nullptr && it->second.modified == modified && it->second.size == size)
            return source;

    std::shared_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return nullptr;

    auto source = std::make_shared<daw::engine::AudioSource> ((int) juce::jmin (2u, reader->numChannels), reader->lengthInSamples,
                                                              reader->sampleRate);
    cache[key] = { source, modified, size };
    ++pending;
    // The job owns the reader from here on; nobody else touches it.
    loader.addJob ([this, flag = alive, source, reader] {
        if (flag->load())
            load (*source, *reader, *flag);
        --pending;
    });
    return source;
}

void AudioFilePool::load (daw::engine::AudioSource& source, juce::AudioFormatReader& reader, const std::atomic<bool>& keepGoing)
{
    juce::AudioBuffer<float> chunk ((int) juce::jmax (1u, reader.numChannels), kChunkFrames);
    for (int64_t pos = 0; pos < source.length && keepGoing.load();)
    {
        const int n = (int) juce::jmin ((int64_t) kChunkFrames, source.length - pos);
        if (! reader.read (&chunk, 0, n, pos, true, true))
        {
            source.failed = true;
            return;
        }
        for (int c = 0; c < source.numChannels; ++c)
            std::memcpy (c == 0 ? source.left.get() + pos : source.right.get() + pos, chunk.getReadPointer (juce::jmin (c, chunk.getNumChannels() - 1)),
                         sizeof (float) * (size_t) n);
        pos += n;
        if (const int delay = readDelayMs.load(); delay > 0)
            juce::Thread::sleep (delay);
        source.publish (pos);
    }
}

void AudioFilePool::waitForLoads()
{
    while (pending.load() > 0)
        juce::Thread::sleep (1);
}
