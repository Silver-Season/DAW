#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace daw::engine
{

struct MidiClip;

// The decoded samples of one sound file (docs/spec/05-audio-recording.md, task 5.1), at the file's own sample rate.
// The buffers are allocated in full before the source is shared; a loader thread fills them front to back and publishes
// its progress through `available`, so the audio thread can play a long file while it is still being read and never
// waits: frames at or beyond `available` sound as silence and count as a disk miss.
struct AudioSource
{
    static constexpr int kPeakFrames = 256; // frames per min/max pair of the waveform overview

    AudioSource (int numChannels, int64_t numFrames, double rate);

    int numChannels = 1;  // 1 or 2 (wider files are mixed down to two when they are read)
    int64_t length = 0;   // frames
    double sampleRate = 44100.0;
    std::unique_ptr<float[]> left, right; // right is null for mono

    std::atomic<int64_t> available { 0 }; // frames decoded so far (release by the loader, acquire by readers)
    std::atomic<bool> failed { false };   // the loader gave up; what is available stays playable

    // Waveform overview: min and max of every kPeakFrames frames of both channels together (interleaved min, max),
    // filled by the loader as it goes. peaksAvailable counts pairs.
    std::unique_ptr<float[]> peaks;
    int64_t numPeaks = 0;
    std::atomic<int64_t> peaksAvailable { 0 };

    const float* channel (int index) const noexcept { return index > 0 && right != nullptr ? right.get() : left.get(); }
    bool isComplete() const noexcept { return available.load (std::memory_order_acquire) >= length; }

    // Loader side: computes the peaks of [from, to) (which must start at a peak boundary) and publishes both.
    void publish (int64_t upTo) noexcept;

    AudioSource (const AudioSource&) = delete;
    AudioSource& operator= (const AudioSource&) = delete;
};

// How an audio clip reads its source. Content beats map to file frames through the project tempo at the time of
// playback: there is no warp in this stage, so a file plays at its own speed (times the transposition, which works by
// resampling like Live's Re-Pitch) and a clip keeps its length in beats when the tempo changes.
struct AudioPart
{
    std::shared_ptr<const AudioSource> source; // null: the file is missing; the clip is silent
    double sampleStart = 0.0;                   // the file frame at content beat 0
    float gain = 1.0f;
    double pitchRatio = 1.0;                    // 2^(semitones / 12 + cents / 1200)
    bool reverse = false;
    double fadeIn = 0.0, fadeOut = 0.0;         // beats, at the edges of an arrangement clip
};

// Per-sample fades of one stretch of playback, measured in beats since the stretch began (`elapsed`). The equal-power
// fade in covers [0, fadeIn), the fade out [length - fadeOut, length). length < 0: the stretch does not end (Session).
struct AudioFades
{
    double fadeIn = 0.0, fadeOut = 0.0, length = -1.0;
};

// Counts what could not be played because the loader had not got that far.
struct AudioRenderStats
{
    int missedFrames = 0;
};

// Renders `numSamples` samples of an audio clip and ADDS them to outL / outR. The first sample lies `elapsed` beats after
// the clip entered its content at `entry` (beats from the start of the content); after the end of the content the loop
// brace repeats if `loop`, else the clip is silent. The loop seam gets a 2 ms equal-power crossfade with the audio before
// the brace start (or, if there is none, a 2 ms fade in after the jump), so a loop never clicks. Allocation free.
void renderAudioClip (const MidiClip& clip, double entry, bool loop, double elapsed, double samplesPerBeat, double engineRate,
                      const AudioFades& fades, float* outL, float* outR, int numSamples, AudioRenderStats& stats) noexcept;

// File frames per content beat of an audio clip at the given tempo (in samples per beat of the engine).
double framesPerBeat (const AudioPart&, double samplesPerBeat, double engineRate) noexcept;

// Length in beats of `frames` frames of a file at `fileRate` when played at `bpm` (no transposition).
double beatsForFrames (double frames, double fileRate, double bpm) noexcept;

// The tempo written in a file name ("Drums_124bpm.wav", "loop 90 BPM.aif"), 0 if there is none. 40..300 BPM only.
double tempoFromFileName (const std::string& name);

} // namespace daw::engine
