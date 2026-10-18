#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include "daw/engine/AudioClip.h"

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

// Warping of audio clips (docs/spec/07-warp.md), the part that runs in the background:
//  - analysis: the tempo and the first downbeat of a newly imported file (daw::dsp::estimateTempo);
//  - stretching: a copy of a file played at another tempo without changing its pitch, rendered whole by Rubber Band
//    (daw::dsp::stretchOffline) and kept while a clip uses it. The engine plays the original, resampled, until the copy
//    is ready, then swaps it in on the next graph build. Rendering ahead of the cursor in a ring buffer (as the spec
//    describes) is the later refinement; a whole copy is simpler and cannot drop out.
class WarpService
{
public:
    // What the analysis found in a file (07-warp.md, task 7.2): times in seconds of the file.
    struct Analysis
    {
        double bpm = 0.0, firstBeatSeconds = 0.0, confidence = 0.0;
        std::vector<double> onsets, beats;
    };

    explicit WarpService (juce::AudioFormatManager&);
    ~WarpService();

    // Where analyses are kept between runs (one small XML per file content hash); empty keeps them in memory only.
    void setCacheFolder (const juce::File& f) { cacheFolder = f; }

    // Message thread. `done` runs on the message thread with the result (bpm 0 when nothing was found). A file with a
    // content hash is read from the cache when it was analysed before.
    void analyse (const juce::File&, const juce::String& hash, std::function<void (const Analysis&)> done);

    // Message thread: the analysis of a file for the views (onsets and beats drawn on the waveform), null until it is
    // ready; the first call starts it in the background and onAnalysisReady fires when it is done.
    std::shared_ptr<const Analysis> analysisFor (const juce::File&, const juce::String& hash);
    std::function<void()> onAnalysisReady;

    // Message thread. The stretched copy if it is ready; otherwise null, and the render starts (once). `timeRatio` is
    // output length / input length. `ready` is called on the message thread when a render finishes.
    std::shared_ptr<daw::engine::AudioSource> stretched (const std::shared_ptr<daw::engine::AudioSource>& original, double timeRatio,
                                                         const juce::String& mode);
    // The same along warp markers: input frame -> output frame key frames (daw::dsp::stretchOfflineMapped), the copy
    // exactly outFrames long. `beatsDecay` is the Beats mode setting.
    std::shared_ptr<daw::engine::AudioSource> stretched (const std::shared_ptr<daw::engine::AudioSource>& original,
                                                         std::vector<std::pair<int64_t, int64_t>> keyFrames, int64_t outFrames,
                                                         const juce::String& mode, double beatsDecay);
    std::function<void()> onStretchReady;

    // Message thread, after a graph build: copies nobody asked for in that build are dropped.
    void beginUse();
    void endUse();

    // Tests and export: wait until every queued job has finished.
    void waitForJobs();
    // Export: stretch on the calling thread, so the copy is there in the same graph build (nothing plays in real time).
    void setRenderInline (bool v) { renderInline = v; }

private:
    struct Key
    {
        const daw::engine::AudioSource* source;
        uint64_t ratioKey; // a hash of the key frames, the length and the decay
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

    struct Job
    {
        std::vector<std::pair<int64_t, int64_t>> keyFrames;
        int64_t outFrames = 0;
        juce::String mode;
        double decay = 1.0;
    };
    static std::shared_ptr<daw::engine::AudioSource> render (const daw::engine::AudioSource& original, const Job&);

    Analysis compute (const juce::File&) const;
    bool readCache (const juce::String& hash, Analysis&) const;
    void writeCache (const juce::String& hash, const Analysis&) const;

    juce::AudioFormatManager& formats;
    juce::File cacheFolder;
    std::map<juce::String, std::shared_ptr<const Analysis>> analyses; // by hash or path; null while running
    bool renderInline = false;
    juce::ThreadPool pool { 2 };
    std::map<Key, Entry> cache;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
