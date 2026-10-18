#pragma once

#include "../Settings.h"
#include "../model/ProjectModel.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <functional>
#include <memory>

class AudioEngine;

// Export of the song (docs/spec/04-export.md, tasks 4.1, 4.3, 4.4 in part).
//
// The song is rendered by a SEPARATE engine on a copy of the project, with its own instances of every device (states
// copied), so the user can go on working. prepare() runs on the message thread and waits for hosted plugins to load;
// the render itself runs on a background thread and drives the copy's audio callback as fast as it can, like a device
// would. The arrangement plays from the start of the range to its end, then the tail rings out until the output stays
// below -90 dBFS for a second (at most 10 s). The metronome is off. The result is normalised (if asked) and written.
//
// Not yet: MP3 (needs LAME), other sample rates than the project's, dither, LUFS normalisation, stems.
class OfflineRenderer final : private juce::Thread
{
public:
    struct Options
    {
        double startBeat = 0.0, endBeat = 0.0; // song beats
        juce::File file;
        juce::String format = "wav";           // wav | aiff | flac
        int bitDepth = 24;                     // 16, 24, 32 (float, WAV and AIFF only)
        double normalisePeakDb = 1.0;          // > 0 = no normalisation; else the peak the file gets (-0.1, -1)
        bool tail = true;
    };

    struct Result
    {
        bool ok = false;
        juce::String error;
        double seconds = 0.0;
        double peakDb = -120.0;  // of the written file
        double sourcePeakDb = -120.0; // before normalisation (clipping warning)
    };

    OfflineRenderer (Settings&, const ProjectModel& source, AudioEngine& live, const Options&);
    ~OfflineRenderer() override;

    // Starts: plugin loads are awaited on the message thread, then the render runs in the background. `done` is called
    // on the message thread.
    void start (std::function<void (const Result&)> done);
    void cancel() { signalThreadShouldExit(); }
    double getProgress() const { return progress.load(); }
    bool isRendering() const { return isThreadRunning(); }

    // The rendering itself, synchronous, for tests (the copy's plugins must be ready).
    Result renderNow();

private:
    void run() override;
    void waitForDevices (int attempts);
    bool writeFile (const juce::AudioBuffer<float>&, Result&) const;

    Settings& settings;
    std::unique_ptr<ProjectModel> model;
    std::unique_ptr<AudioEngine> engine;
    Options options;
    double sampleRate = 48000.0;
    std::atomic<double> progress { 0.0 };
    std::function<void (const Result&)> onDone;
    Result result;
};
