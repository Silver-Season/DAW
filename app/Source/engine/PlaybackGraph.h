#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "daw/engine/Clip.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

// The engine's real-time data model. The message thread builds these objects from the ProjectModel; the
// audio thread only reads an immutable PlaybackGraph and writes the atomics and audio-only members of the
// objects it points to. See docs/spec/00-rules.md section 2 and 01-layout-project.md task 1.3.

constexpr int kMaxChannels = 8; // widest plugin bus layout a track buffer can host

enum class TrackInputMode : int
{
    allMidi = 0, // hardware MIDI and the computer keyboard
    keyboard,    // computer keyboard only
    midiDevice,  // hardware MIDI only (per-device routing comes with the MIDI settings)
    none
};

// Lives as long as its track exists. Created and destroyed on the message thread; destruction is deferred
// until the audio thread can no longer see a graph that contains it.
struct TrackRuntime
{
    explicit TrackRuntime (int64_t id) : trackId (id) {}

    // Allocates everything the audio thread needs. Message thread, before the runtime is published.
    void prepare (int maxBlock)
    {
        buffer.setSize (kMaxChannels, maxBlock, false, true, false);
        midi.ensureSize (4096);
        fxMidi.ensureSize (64);
        events.reserve (4096);
    }

    const int64_t trackId;

    // written by the message thread from the model
    std::atomic<float> volumeGain { 1.0f }, pan { 0.0f };
    std::atomic<bool> mute { false }, solo { false }, arm { false };
    std::atomic<int> inputMode { (int) TrackInputMode::allMidi };
    std::atomic<int> transpose { 0 }; // semitones added to every note that reaches the instrument

    // written by the audio thread, read by the UI timer
    std::atomic<float> meterL { 0.0f }, meterR { 0.0f };
    std::atomic<int> playingSlot { -1 }, queuedSlot { -1 };
    std::atomic<double> playingStartBeat { 0.0 }; // lets the UI draw the progress bar of the playing clip

    // audio thread only
    daw::engine::ClipPlayer player;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi, fxMidi;
    std::vector<daw::MidiEvent> events;
    float lastGainL = 0.0f, lastGainR = 0.0f;
    int resumeSlot = -1; // the clip that played when the transport was stopped; Play starts it again
    // The note a pitch was sounded as (transposed + 1, 0 = nothing, kDropped = out of range) so that its note-off
    // finds it even if the transposition changed in between. One map for the clips, one for live playing.
    static constexpr uint8_t kDropped = 255;
    std::array<uint8_t, 128> clipSounding {}, liveSounding {};
};

// A plugin or Prisma. Survives graph rebuilds; only a change of the device itself replaces it.
struct DeviceInstance
{
    int64_t deviceId = -1;
    std::unique_ptr<juce::AudioProcessor> processor;
    std::atomic<bool> enabled { true };
    std::atomic<int> numChannels { 2 }; // channels of the view handed to processBlock

    juce::String sourceKey; // message thread only: what the instance was created from (kind + plugin description)
};

// Immutable after publication.
struct PlaybackGraph
{
    PlaybackGraph() { liveGraphs().fetch_add (1); }
    ~PlaybackGraph() { liveGraphs().fetch_sub (1); }
    PlaybackGraph (const PlaybackGraph&) = delete;
    PlaybackGraph& operator= (const PlaybackGraph&) = delete;

    // How many graphs exist right now (published, waiting for deletion). Tests use it to check that
    // retired graphs really get deleted.
    static int liveCount() { return liveGraphs().load(); }

    uint64_t generation = 0;

    struct Track
    {
        TrackRuntime* runtime = nullptr;
        std::vector<DeviceInstance*> chain;                              // first: instrument, then effects
        std::vector<std::shared_ptr<const daw::engine::MidiClip>> slots; // by scene, nullptr = empty
    };
    std::vector<Track> tracks;
    std::vector<DeviceInstance*> masterChain;

private:
    static std::atomic<int>& liveGraphs()
    {
        static std::atomic<int> count { 0 };
        return count;
    }
};

// Message thread -> audio thread, through a fixed-size SPSC queue.
struct EngineCommand
{
    enum class Type : uint8_t
    {
        launchClip,
        launchScene,
        resume,
        stopTrack,
        stopAll
    };

    Type type = Type::stopAll;
    int64_t trackId = -1;
    int sceneIndex = -1;
};
