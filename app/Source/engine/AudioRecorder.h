#pragma once

#include "../model/ProjectModel.h"
#include "AudioEngine.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <map>
#include <memory>
#include <mutex>
#include <vector>

// Recording of sound to disk (docs/spec/05-audio-recording.md, task 5.4).
//
// The audio thread copies what a track records into the track's RecordStream; a thread of this class takes it from
// there and writes a WAV file (16 / 24 bit or 32 bit float, as the settings say) in Samples/Recorded of the project.
// While it writes it also keeps a min/max overview of what came in, so the slot or the lane can draw the waveform as it
// grows without reading the file. The recorders (SessionRecorder, ArrangementRecorder) decide what becomes of a file
// once it is complete.
class AudioRecorder final : private juce::Thread
{
public:
    AudioRecorder (AudioEngine&, ProjectModel&);
    ~AudioRecorder() override;

    // Whether the track records sound: an audio track with an input, or a MIDI track whose record source is "audio".
    bool recordsSound (const TrackRef&) const;

    // Opens the file and arms the track's stream (message thread). The stream begins with the next record start of the
    // track (a slot) or of the arrangement recording. False if the track cannot record sound now (no stream yet, or the
    // previous recording is still being written).
    bool begin (int64_t trackId, RecordStream::Mode);
    // Takes back a recording that has not begun; its file is deleted. False if it already began (it will finish).
    bool cancel (int64_t trackId);
    bool isRecording (int64_t trackId) const;

    struct Finished
    {
        bool ok = false;
        juce::File file;
        juce::String name;           // the clip's name: the track name
        int64_t frames = 0;
        int64_t startSample = 0;     // engine sample of frame 0
        int latency = 0;             // frames the content is moved earlier (the clip's sampleStart)
        int channels = 2;
        double sampleRate = 44100.0;
        int64_t droppedFrames = 0;   // lost because the disk did not keep up (silence in the file)
        int64_t firstDropFrame = -1;
    };
    // True once the file of the track is complete and closed; `out` gets it and the recorder forgets it.
    bool takeFinished (int64_t trackId, Finished& out);
    bool isFinished (int64_t trackId) const;

    // Puts a finished file into the pool of the project (inside the caller's undo transaction). Returns the FILE id.
    int64_t addToPool (const Finished&);

    // The waveform so far: min/max pairs (interleaved) of every kPeakFrames frames, both channels together.
    static constexpr int kPeakFrames = 256;
    bool getLivePeaks (int64_t trackId, std::vector<float>& minMax, int64_t& frames, double& rate) const;

    // Where recordings go: Samples/Recorded in the project folder, else a folder in the user's music folder.
    juce::File recordingsFolder() const;

    // Tests: a writer that stands still (the disk does not keep up), and waiting until everything was written.
    void setWriterPausedForTesting (bool paused) { writerPaused = paused; }
    void processNowForTesting() { process(); }

private:
    struct Job
    {
        int64_t trackId = 0;
        std::shared_ptr<RecordStream> stream;
        std::unique_ptr<juce::AudioFormatWriter> writer;
        Finished info;
        std::atomic<bool> finished { false };
        mutable std::mutex peakLock;
        std::vector<float> peaks;
        float peakMin = 0.0f, peakMax = 0.0f;
        int peakFill = 0;
        int64_t framesOnDisk = 0;
    };

    void run() override;
    void process();
    void writeJob (Job&);

    AudioEngine& engine;
    ProjectModel& model;
    mutable std::mutex jobsLock; // message thread and the writer thread; never the audio thread
    std::map<int64_t, std::shared_ptr<Job>> jobs;
    std::atomic<bool> writerPaused { false };
    juce::AudioBuffer<float> scratch { 2, 8192 }; // writer thread
};
