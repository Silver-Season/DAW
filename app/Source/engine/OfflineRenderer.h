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
// Stems (task 4.5) come from the same pass: every track writes its share of the master (after its fader and pan,
// without the master chain) to a 32-bit float file next to the result, and once the song is measured each one is
// written in the final format with the master's normalisation gain, so the balance stays. Loudness (task 4.4) is
// measured on the master: integrated LUFS, loudness range, true peak; LUFS normalisation stops at -1 dBTP.
// 16-bit files get TPDF dither with a fixed seed.
//
// Not yet: other sample rates than the project's (MP3 aside), noise-shaped dither.
class OfflineRenderer final : private juce::Thread
{
public:
    struct Options
    {
        double startBeat = 0.0, endBeat = 0.0; // song beats
        juce::File file;
        juce::String format = "wav";           // wav | aiff | flac | mp3
        int bitDepth = 24;                     // 16, 24, 32 (float, WAV and AIFF only)
        int mp3Kbps = 320;                     // CBR when mp3Vbr < 0
        int mp3Vbr = -1;                       // VBR quality 0..9
        double normalisePeakDb = 1.0;          // > 0 = no normalisation; else the peak the file gets (-0.1, -1)
        bool normaliseLufs = false;            // instead of the peak: the integrated loudness becomes lufsTarget
        double lufsTarget = -14.0;
        bool tail = true;
        // Stems: one file per track (return tracks included) in the folder `stemsFolder`, named "<NN> <track>".
        bool stems = false;
        bool stemsIncludeMaster = true; // the master file is written too
        bool stemsIncludeMuted = false;
        juce::File stemsFolder;
    };

    struct Result
    {
        bool ok = false;
        juce::String error;
        double seconds = 0.0;
        double peakDb = -120.0;  // of the written file
        double sourcePeakDb = -120.0; // before normalisation (clipping warning)
        double lufs = -70.0, rangeLu = 0.0, truePeakDb = -120.0; // of the written master
        bool limitedByPeak = false; // LUFS normalisation stopped at -1 dBTP
        juce::StringArray stemFiles;
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
    bool writeFile (const juce::AudioBuffer<float>&, const juce::File&, juce::String& error) const;
    struct Stem;

    Settings& settings;
    std::unique_ptr<ProjectModel> model;
    std::unique_ptr<AudioEngine> engine;
    Options options;
    double sampleRate = 48000.0;
    std::atomic<double> progress { 0.0 };
    std::function<void (const Result&)> onDone;
    Result result;
};
