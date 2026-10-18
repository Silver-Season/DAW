#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include "daw/engine/AudioClip.h"

#include <functional>
#include <map>
#include <memory>
#include <mutex>

// Warping of audio clips (docs/spec/07-warp.md), the part that runs in the background:
//  - analysis: the tempo and the first downbeat of a newly imported file (daw::dsp::estimateTempo);
//  - stretching: a copy of a file played at another tempo without changing its pitch, rendered whole by Rubber Band
//    (daw::dsp::stretchOffline) and kept while a clip uses it. The engine plays the original, resampled, until the copy
//    is ready, then swaps it in on the next graph build. Rendering ahead of the cursor in a ring buffer (as the spec
//    describes) is the later refinement; a whole copy is simpler and cannot drop out.
class WarpService
{
public:
    struct Analysis
    {
        double bpm = 0.0, firstBeatSeconds = 0.0;
    };

    explicit WarpService (juce::AudioFormatManager&);
    ~WarpService();

    // Message thread. `done` runs on the message thread with the result (bpm 0 when nothing was found).
    void analyse (const juce::File&, std::function<void (const Analysis&)> done);

    // Message thread. The stretched copy if it is ready; otherwise null, and the render starts (once). `timeRatio` is
    // output length / input length. `ready` is called on the message thread when a render finishes.
    std::shared_ptr<daw::engine::AudioSource> stretched (const std::shared_ptr<daw::engine::AudioSource>& original, double timeRatio,
                                                         const juce::String& mode);
    std::function<void()> onStretchReady;

    // Message thread, after a graph build: copies nobody asked for in that build are dropped.
    void beginUse();
    void endUse();

    // Tests and export: wait until every queued job has finished.
    void waitForJobs();

private:
    struct Key
    {
        const daw::engine::AudioSource* source;
        long ratioKey;
        juce::String mode;
        bool operator< (const Key& o) const
        {
            if (source != o.source)
                return source < o.source;
            if (ratioKey != o.ratioKey)
                return ratioKey < o.ratioKey;
            return mode < o.mode;
        }
    };
    struct Entry
    {
        std::shared_ptr<daw::engine::AudioSource> result; // null while rendering
        std::shared_ptr<daw::engine::AudioSource> original;
        bool used = true;
    };

    juce::AudioFormatManager& formats;
    juce::ThreadPool pool { 2 };
    std::map<Key, Entry> cache;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
