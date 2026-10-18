#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "../model/AudioInput.h"
#include "RecordStream.h"

#include "daw/engine/Arrangement.h"
#include "daw/engine/Automation.h"
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
        inputBuffer.setSize (2, maxBlock, false, true, false);
        returnBus.setSize (2, maxBlock, false, true, false);
        keyBuffer.setSize (2, maxBlock, false, true, false);
    }

    const int64_t trackId;

    // written by the message thread from the model
    std::atomic<float> volumeGain { 1.0f }, pan { 0.0f };
    std::atomic<bool> mute { false }, solo { false }, arm { false };
    std::atomic<int> inputMode { (int) TrackInputMode::allMidi };
    std::atomic<int> transpose { 0 }; // semitones added to every note that reaches the instrument

    // Sound input of an audio track (task 5.3). Device channels count from 0; inputRight < 0 is a mono input.
    std::atomic<int> audioInput { (int) AudioInputKind::none };
    std::atomic<int> inputLeft { 0 }, inputRight { 1 };
    std::atomic<int> monitorMode { (int) MonitorMode::automatic };
    std::atomic<float> sendGain[2] { { 0.0f }, { 0.0f } }; // to return A and B, post-fader
    // The recording stream of the track, for the message thread (the audio thread uses the graph's pointer). Replaced
    // only while it is idle.
    std::shared_ptr<RecordStream> recordStream;

    // written by the audio thread: the peak of the input (shown before anything records) and whether it went over
    // -0.5 dBFS (red until the user clicks the meter, which stores false); whether the input is heard now
    std::atomic<float> inputMeter { 0.0f };
    std::atomic<bool> inputClipped { false }, monitoring { false };

    // written by the audio thread, read by the UI timer
    std::atomic<float> meterL { 0.0f }, meterR { 0.0f };
    std::atomic<int> playingSlot { -1 }, queuedSlot { -1 };
    std::atomic<double> playingStartBeat { 0.0 }; // lets the UI draw the progress bar of the playing clip

    // The track plays its Session View clips instead of its Arrangement: a session clip was launched or the track's stop
    // button was pressed (docs/spec/03-arrangement.md, "Session against arrangement"). Its arrangement clips are silent
    // until "Back to arrangement". Written by the audio thread only; the UI reads it (grey bar over the clips).
    std::atomic<bool> inSession { false };

    // Recording (docs/spec/02, task 2.2). Written by the audio thread when it handles a record command and as the
    // transport passes the start and end beats; the recorder on the message thread reads them (and sets the state
    // back to idle once it has taken a finished recording). The state is stored last, so a reader that sees a state
    // sees the other values that go with it.
    enum RecordState : int
    {
        recordIdle = 0,
        recordWaiting = 1,   // armed for the start beat (a count-in or a launch quantisation is running)
        recordRunning = 2,
        recordFinished = 3   // the end beat has passed; the message thread has to collect the notes
    };
    static constexpr double kNoEnd = 1.0e18;
    std::atomic<int> recordState { recordIdle };
    std::atomic<int> recordScene { -1 };
    std::atomic<double> recordStartBeat { 0.0 }, recordEndBeat { kNoEnd };
    std::atomic<bool> recordOverdub { false };

    // audio thread only
    daw::engine::ClipPlayer player;
    daw::engine::ArrangementPlayer arrangement;
    uint64_t arrangementSignature = 0;  // of the arrangement the player has seen; a different one means the clips changed
    bool backPending = false;           // "Back to arrangement" waits for its quantisation boundary
    double backBeat = 0.0;              // that boundary, on the monotonic clock
    juce::AudioBuffer<float> buffer;
    juce::AudioBuffer<float> inputBuffer; // the sound input of this block (audio tracks)
    juce::AudioBuffer<float> returnBus;   // a return track: what the sends gave it in this block
    juce::AudioBuffer<float> keyBuffer;   // the track's sound after its chain, before the fader: the key of a sidechain
    float lastSendGain[2] {};
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
        bool audio = false;                                              // an audio track: clips play audio, the chain is effects only
        std::vector<DeviceInstance*> chain;                              // first: instrument, then effects
        std::vector<std::shared_ptr<const daw::engine::MidiClip>> slots; // by scene, nullptr = empty
        daw::engine::ArrangementClips arrangement; // sorted by start, not overlapping
        uint64_t arrangementSignature = 0;          // identifies the clips and their content; 0 = no arrangement clips
        // Recording of sound (task 5.4): the stream the track records into, null if it cannot record sound. An audio
        // track records its input; a MIDI track whose record source is "audio" records what its instrument plays.
        std::shared_ptr<RecordStream> stream;
        bool recordsOutput = false;
        bool isReturn = false;
        // Automation of the arrangement (task 6.7), applied while the track plays its arrangement.
        struct Lane
        {
            enum Kind : int
            {
                volume,
                pan,
                parameter
            } kind = volume;
            juce::AudioProcessorParameter* param = nullptr;
            std::vector<daw::engine::AutomationPoint> points;
        };
        std::vector<Lane> automation;
        // Per device of `chain`: the track whose keyBuffer feeds its sidechain (null = none). A source later in the
        // order is heard one block late, which is inaudible for ducking and needs no ordering of the graph.
        std::vector<TrackRuntime*> sidechains;
    };
    std::vector<Track> tracks;           // normal tracks first, then the return tracks
    std::array<TrackRuntime*, 2> returns {}; // return A and B (the first two return tracks), null if missing
    std::vector<DeviceInstance*> masterChain;
    // Automation of the master (volume, pan, its effects), applied while the transport plays.
    std::vector<Track::Lane> masterAutomation;

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
        launchClip,    // trackId, sceneIndex; beat > 0: launch on that grid instead of the launch quantisation
        launchScene,
        resume,
        stopTrack,
        stopAll,
        startRecord,   // trackId, sceneIndex: record into that slot
        stopRecord,    // trackId
        launchAligned, // trackId, sceneIndex, beat: play the clip as if it had been launched at `beat`
        resumeContinue, // like resume, but the song continues where it stopped instead of at the play start
        seek,           // beat: the song position jumps there now
        seekQuantised,  // beat: the song position jumps there at the next launch-quantisation boundary (now if stopped)
        rewind,         // the song position goes to 0
        backToArrangement, // trackId (-1 = all): stops the session clips and gives the track back to the arrangement at the next boundary
        startArrangementRecord, // record onto the timeline (task 3.4): beat = nothing, the engine picks the start
        stopArrangementRecord
    };

    Type type = Type::stopAll;
    int64_t trackId = -1;
    int sceneIndex = -1;
    double beat = 0.0;
    bool noOverdub = false; // startRecord: always a new clip (a recording of sound never overdubs)
};

// Audio thread -> message thread, through a fixed-size SPSC queue, while an arrangement recording runs (task 3.4).
// It is the log from which the recorder turns live notes and Session View launches into arrangement clips.
struct EngineEvent
{
    enum class Type : uint8_t
    {
        anchor,   // the song position does not continue where it was (the start, a seek): from monoBeat on it is `beat`
        loopJump, // the same, but caused by the loop of the arrangement: a new pass of the loop begins
        launch,   // a session clip of (trackId, slot) began to play; its phase counts from clipStartMono
        stop      // the clip of (trackId, slot) stopped
    };

    Type type = Type::anchor;
    int64_t trackId = 0;
    int slot = -1;
    double beat = 0.0;          // the song beat at which it happened
    double monoBeat = 0.0;      // the same moment on the monotonic transport clock (the clock of InputEvent::beat)
    double clipStartMono = 0.0; // launch only: the monotonic beat at which the clip's cycle began (may be before monoBeat)
};
