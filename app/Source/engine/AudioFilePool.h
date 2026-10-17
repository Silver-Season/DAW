#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "daw/engine/AudioClip.h"

#include <atomic>
#include <map>
#include <memory>

// The decoded sound files of the open project (docs/spec/05-audio-recording.md, task 5.1).
//
// Every file is decoded once into memory (float, at its own rate) and shared by all the clips that play it. Decoding
// runs on background threads, front to back: a long file can play while it is still being read, and the audio thread
// never waits for the disk (what is not decoded yet sounds as silence and counts as a miss). A source lives as long as a
// clip of some playback graph holds it; the pool only keeps weak references, so an unused file goes away with the last
// graph that played it.
//
// Deviation from the spec: long files are not streamed with a 3 s read-ahead but decoded in full in the background. It
// keeps the audio thread trivially lock free and lets any number of clips play one file at different positions; the
// cost is memory (about 230 MB for ten minutes of 48 kHz stereo).
class AudioFilePool
{
public:
    AudioFilePool();
    ~AudioFilePool();

    juce::AudioFormatManager& formats() { return formatManager; }

    // Message thread. The decoded file, loading in the background if it was not loaded yet; null if the file does not
    // exist or no format can read it. A file that changed on disk since it was loaded is loaded again.
    std::shared_ptr<daw::engine::AudioSource> acquire (const juce::File&);
    // Whether the file is already decoded or decoding (does not start a load).
    std::shared_ptr<daw::engine::AudioSource> find (const juce::File&) const;

    // Blocks until every load has finished (tests, export).
    void waitForLoads();
    bool isLoading() const { return pending.load() > 0; }

    // Tests: every chunk the loader reads takes this many extra milliseconds (a slow disk).
    void setReadDelayForTesting (int ms) { readDelayMs = ms; }

    static constexpr int kChunkFrames = 32768;

private:
    struct Entry
    {
        std::weak_ptr<daw::engine::AudioSource> source;
        juce::Time modified;
        juce::int64 size = 0;
    };

    void load (daw::engine::AudioSource&, juce::AudioFormatReader&, const std::atomic<bool>& keepGoing);

    juce::AudioFormatManager formatManager;
    std::map<juce::String, Entry> cache; // by full path
    juce::ThreadPool loader { 2 };
    std::atomic<int> pending { 0 };
    std::atomic<int> readDelayMs { 0 };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
