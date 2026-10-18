#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "../Settings.h"
#include "../model/ProjectModel.h"
#include "AudioFilePool.h"
#include "WarpService.h"
#include "AudioPreview.h"
#include "Metronome.h"
#include "MidiInputQueue.h"
#include "PlaybackGraph.h"
#include "PreviewPlayer.h"

#include "daw/engine/Capture.h"
#include "daw/engine/Clip.h"

#include <atomic>
#include <limits>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>

// The DAW's real-time core: transport, Session View clip players, per-track device chains (built-in
// Prisma or any hosted AU/VST3) and the mixer.
//
// The engine listens to a ProjectModel and publishes an immutable PlaybackGraph that the audio thread
// reads without locks. The model never knows about the engine.
class AudioEngine final : public juce::AudioIODeviceCallback,
                          private juce::ChangeListener,
                          private juce::ValueTree::Listener,
                          private juce::Timer,
                          private juce::MidiInputCallback,
                          private juce::MidiKeyboardState::Listener
{
public:
    using DeviceFactory = std::function<std::unique_ptr<juce::AudioProcessor> (const DeviceRef&)>;

    // openDevices = false leaves the sound card and MIDI inputs closed, for an engine whose callback is
    // driven by hand (tests, DAW_NULL_AUDIO): a real device would call it concurrently from its own thread.
    explicit AudioEngine (Settings&, bool openDevices = true);
    ~AudioEngine() override;

    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }
    juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }
    juce::KnownPluginList& getKnownPlugins() { return knownPlugins; }
    Settings& getSettings() { return settings; }
    juce::MidiKeyboardState& getKeyboardState() { return keyboardState; }
    double getSampleRate() const { return sampleRate.load(); }
    int getBlockSize() const { return blockSize; }

    //==========================================================================
    // Model. Everything below that talks about tracks and devices by id refers to the attached model.
    // Attaching builds and publishes a graph straight away; null detaches (the engine goes silent).
    void setModel (ProjectModel*);
    ProjectModel* getModel() const { return model; }

    // How devices are made from DEVICE nodes. The default makes Prisma (with the saved state) for
    // kind="prisma" and nothing for plugins, which arrive through registerProcessor. Tests replace it.
    void setDeviceFactory (DeviceFactory);

    // Hands over a plugin loaded asynchronously by the UI for a DEVICE node of the model.
    void registerProcessor (int64_t deviceId, std::unique_ptr<juce::AudioProcessor>);

    // Edits that remove devices. Each first writes the live state of every affected processor into
    // DEVICE.state (outside the undo history, so that undoing the removal does not undo it), then removes the node, so that undo brings the device back with its parameters. The caller
    // opens the undo transaction.
    void removeDevice (const DeviceRef&);
    void deleteTrack (int64_t trackId);
    void captureDeviceStates (const juce::ValueTree& subtree); // all DEVICE nodes at or below `subtree`

    // Loads a hosted plugin asynchronously and, when it is ready, replaces the track's first device with it
    // (one undo transaction), registers the instance and publishes the graph. `done` gets an error text
    // (empty on success) and the new device id. This is the path the UI uses.
    void loadPluginInstrument (int64_t trackId, const juce::PluginDescription&, std::function<void (juce::String error, int64_t deviceId)> done);

    // Third-party plugins are created from DEVICE.pluginDesc whenever a DEVICE node has no instance: after a
    // project is loaded, and when undo brings a deleted device back. The load runs asynchronously
    // (createPluginInstanceAsync); until it is done the device passes the sound through. If the plugin is
    // gone or fails to load, the node becomes kind="missing" and keeps its state.
    bool isDeviceLoading (int64_t deviceId) const;
    bool isAnyDeviceLoading() const;
    // After a project is opened: devices that were "missing" when it was saved but whose plugin is installed
    // now become plugins again (and load). Devices whose plugin is still absent stay "missing".
    void reviveMissingDevices();
    // Bumped when a plugin load finishes (either way), so that views can refresh themselves.
    uint32_t getDeviceEpoch() const { return deviceEpoch.load(); }

    //==========================================================================
    // Audio files (docs/spec/05-audio-recording.md, task 5.1). The project folder resolves the relative paths of the pool.
    void setProjectFolder (const juce::File&);
    juce::File getProjectFolder() const { return projectFolder; }
    AudioFilePool& getAudioPool() { return audioPool; }
    WarpService& getWarp() { return warp; }
    // Finds the tempo of a newly imported file in the background, then lets its clips follow the project tempo
    // (docs/spec/07-warp.md, task 7.2). Not undoable: it completes what the import started.
    void analyseAudioFile (int64_t fileId);
    // Where a FILE of the pool is: inside the project folder if its relative path is found there, else its absolute path.
    juce::File resolveAudioFile (const AudioFileRef&) const;
    // The decoded file of a FILE id (loading in the background), null if it is missing. Message thread.
    std::shared_ptr<daw::engine::AudioSource> audioSourceFor (int64_t fileId);
    // The same, cheap enough to call from paint(): remembers what it found (and what was missing) until the next graph
    // rebuild, so it does not look at the disk again for every repaint.
    std::shared_ptr<daw::engine::AudioSource> peekAudioSource (int64_t fileId);
    // Blocks of the audio thread in which an audio clip wanted samples the loader had not decoded yet. The transport
    // shows a disk symbol while it grows.
    uint32_t getDiskMisses() const { return diskMisses.load (std::memory_order_relaxed); }

    // Message thread only; the pointers stay valid until the next graph rebuild that drops them.
    juce::AudioProcessor* findProcessor (int64_t deviceId) const;
    TrackRuntime* findRuntime (int64_t trackId) const;

    //==========================================================================
    // Graph publication. The timer calls rebuildGraphIfNeeded() and collectGarbage() at 60 Hz; tests call
    // them directly. All of these are for the message thread (or whatever thread plays its role).
    void rebuildGraphIfNeeded();
    void rebuildGraph();
    void collectGarbage();
    bool isGraphDirty() const { return graphDirty.load(); }
    uint64_t getPublishedGeneration() const { return generationCounter.load(); }
    size_t getNumRetiredObjects() const; // graphs + runtimes + devices waiting for deletion

    //==========================================================================
    // Transport
    // Play starts the song at the arrangement play start (model property arrangementPlayStart) and brings back the Session
    // clips that were playing when the transport stopped.
    void play();
    // Shift+Space: like play(), but the song continues where it stopped.
    void playFromPosition();
    void stop();
    bool isPlaying() const { return playing.load(); }
    double getTempo() const { return tempo.load(); }
    // The monotonic clock of Session View: 0 at every start of the transport, never jumps (negative while a count-in
    // plays, -4 .. 0 for one bar of 4/4). Clip launches, clip progress and session recording are measured on it.
    double getPositionBeats() const { return positionBeats.load(); }
    // The position in the song, i.e. in the Arrangement: starts at the play start, moves with seek and loop jumps. While
    // stopped it is where the song stopped. The transport display and the arrangement playhead follow this one.
    double getSongPositionBeats() const { return songPositionBeats.load(); }
    bool isCountingIn() const { return playing.load() && positionBeats.load() < 0.0; }
    double getBeatsPerBar() const { return beatsPerBar.load(); }

    // Session View commands: queued for the audio thread. They return false if the queue is full.
    bool launchClip (int64_t trackId, int scene);
    bool launchScene (int scene);
    bool stopTrack (int64_t trackId);
    bool stopAllClips();

    // Arrangement commands (docs/spec/03-arrangement.md, tasks 3.2 and 3.3), queued for the audio thread.
    // seek: the song position jumps there at once, playing or not (while stopped it moves the point Shift+Space continues from).
    bool seek (double songBeat);
    // While playing the jump waits for the next launch-quantisation boundary and is exact to the sample; while stopped
    // it is the same as seek.
    bool seekQuantised (double songBeat);
    // The song position goes to 0 (a second press of stop). The caller also sets the model's arrangementPlayStart if the
    // play start should go back, too.
    bool rewindSong();
    // The orange "Back to arrangement" button: at the next launch-quantisation boundary the session clips stop and every
    // track plays its arrangement again; notes under the position are chased. While stopped it happens at once.
    // Every track (-1) or one track gives up its Session clips and plays the timeline again, at the next launch boundary.
    bool backToArrangement (int64_t trackId = -1);
    // The track switches to Session View: the clip of that slot starts on the next bar line, whatever the launch quantisation.
    bool launchClipOnBar (int64_t trackId, int scene);
    // True when at least one track plays (or was stopped in) Session View instead of the arrangement: the button lights up.
    bool anyTrackInSession() const { return anyInSession.load(); }

    // Recording (docs/spec/02, task 2.2). Normally called through SessionRecorder, which collects the notes.
    // startRecord: from a stopped transport it starts the transport (with the count-in of the project) and the
    // recording begins at beat 0; from a playing one at the next launch-quantisation boundary. A track that
    // plays the clip of the slot while OVR is on is overdubbed instead. stopRecord ends a recording at the next bar
    // line (an overdub at once) and cancels one that has not begun. launchAligned plays a clip that was just
    // recorded as if it had been launched at `beat`, which may lie a few milliseconds in the past.
    bool startRecord (int64_t trackId, int scene, bool noOverdub = false);
    bool stopRecord (int64_t trackId);
    bool launchAligned (int64_t trackId, int scene, double beat);

    // Arrangement recording (docs/spec/03-arrangement.md, task 3.4). Normally called through ArrangementRecorder.
    // start: from a stopped transport it starts the transport with the count-in of the project, the recording begins at
    // monotonic beat 0; from a playing one at the next launch-quantisation boundary. While it runs the audio thread logs
    // the song position jumps and the Session View launches as EngineEvents, and the arrangement of the tracks that
    // record (and do not overdub) is silent where the recording replaces it. stop ends it at once.
    bool startArrangementRecord();
    bool stopArrangementRecord();
    enum ArrangementRecordState : int
    {
        arrangementIdle = 0,
        arrangementWaiting = 1,  // armed: the count-in or the quantisation boundary has not come yet
        arrangementRunning = 2,
        arrangementFinished = 3  // ended (stop, or the transport stopped); the message thread collects it and calls acknowledge
    };
    int getArrangementRecordState() const { return arrRecState.load(); }
    double getArrangementRecordStartBeat() const { return arrRecStart.load(); } // monotonic clock
    double getArrangementRecordEndBeat() const { return arrRecEnd.load(); }
    void acknowledgeArrangementRecord() { arrRecState.store (arrangementIdle); }
    // The log of an arrangement recording, in order. Message thread. Returns how many were appended.
    size_t popEngineEvents (std::vector<EngineEvent>& out);
    uint32_t getEngineEventDrops() const { return engineEventDrops.load (std::memory_order_relaxed); }

    // Live input events that reached a track (or none), for the recorder and the capture buffer. Message thread.
    // Returns how many were appended to `out`.
    size_t popInputEvents (std::vector<daw::engine::InputEvent>& out);
    uint32_t getInputEventDrops() const { return inputDrops.load (std::memory_order_relaxed); }
    // Samples the audio thread has processed since the engine was created (the clock of InputEvent::engineSample).
    int64_t getEngineSample() const { return engineSampleCount.load (std::memory_order_relaxed); }

    // How far earlier than the block position a live event is placed: output latency + half a block + the manual
    // correction of the settings (spec 2.2, "MIDI delay"). In samples.
    int getRecordCompensationSamples() const { return recordCompensation.load(); }
    void setRecordCorrectionMs (double ms); // -50..+50, also stored in the settings
    double getRecordCorrectionMs() const { return correctionMs.load(); }

    //==========================================================================
    // Sound input (docs/spec/05-audio-recording.md, tasks 5.3 and 5.4)
    // How many frames a recorded input lags behind what the engine played at the same moment: the driver's input and
    // output latency, one buffer each way, and the manual correction of the settings. Recordings are moved earlier by it.
    int getAudioRecordLatency() const { return audioRecordLatency.load(); }
    int getDriverRoundTrip() const { return driverRoundTrip.load(); }
    void setAudioLatencyCorrectionMs (double ms); // -100..+100, also stored in the settings
    double getAudioLatencyCorrectionMs() const { return audioCorrectionMs.load(); }
    // Number of input channels the device has open, and their names ("1", "2" or the driver's). Message thread.
    juce::StringArray getInputChannelNames() const;

    // Loopback test (settings, Recording, "Measure latency"): the engine plays 10 clicks 100 ms apart on the first two
    // outputs and records `inputChannel`. Message thread; poll isLatencyTestDone(), then takeLatencyTestResult().
    bool startLatencyTest (int inputChannel);
    bool isLatencyTestRunning() const { return latencyTestState.load() == 1; }
    bool isLatencyTestDone() const { return latencyTestState.load() == 2; }
    // The measured round trip in samples (ok = false if the clicks did not come back); resets the test.
    struct LatencyTestResult
    {
        bool ok = false;
        int roundTripSamples = 0;
        int driverSamples = 0;
        double correctionMs = 0.0; // what the manual correction should be so that recordings land on the beat
    };
    LatencyTestResult takeLatencyTestResult();
    static constexpr int kLatencyClicks = 10;

    // Live MIDI from any thread, with the time (ms, juce::Time::getMillisecondCounterHiRes) it happened.
    // The audio thread turns the time into a sample offset inside the block. Used by the device callbacks
    // and the on-screen keyboard; tests call it with made-up times.
    bool pushLiveMidi (const uint8_t* bytes, int size, bool fromKeyboard, double timeMs);

    // The clock the audio thread reads once per block (default: the hi-res millisecond counter). Tests
    // replace it with a function returning a scripted time.
    using ClockFn = double (*) ();
    void setClockForTesting (ClockFn fn) { clock.store (fn); }

    std::atomic<float> masterMeterL { 0.0f }, masterMeterR { 0.0f };

    // The last kAnalysisSize samples of the master (mono), for the spectrum of the top bar. The audio thread overwrites
    // the ring; a reader copies the newest `n` samples (a block may tear at the edge, which a display does not mind).
    static constexpr int kAnalysisSize = 8192;
    void copyAnalysis (float* dest, int n) const;

    //==========================================================================
    // Pre-listening of Prisma presets from the browser (message thread). It plays into the master after the
    // master chain, with the volume of the settings.
    void previewFactoryPreset (int program);
    void previewPresetState (const juce::MemoryBlock& prismaState);
    void stopPreview();
    void setPreviewVolumeDb (float db)
    {
        preview.setGainDb (db);
        audioPreview.setGain (db <= -60.0f ? 0.0f : juce::Decibels::decibelsToGain (db));
    }
    bool isPreviewSounding() const { return preview.isSounding() || audioPreview.isSounding(); }
    // A sound file of the browser plays once from its start (task 5.6). False if it cannot be read.
    bool previewAudioFile (const juce::File&);

    // 440 Hz at -20 dBFS for one second on the first two outputs (settings, Sound).
    void playTestTone();
    bool isTestToneSounding() const { return toneSamplesLeft.load() > 0; }

    // Opens the MIDI inputs that are switched on in the settings ("plays tracks") and closes the others. Called
    // by the settings window and, on its own, when a device is plugged in or out. Does nothing without devices.
    void refreshMidiInputs();

    // Number of events received from MIDI hardware so far; the transport's MIDI indicator blinks when it changes.
    uint32_t getHardwareMidiCount() const { return hardwareMidiCount.load (std::memory_order_relaxed); }

    // juce::AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

private:
    class PlayHead;
    friend class PlayHead;

    template <typename T>
    struct Retired
    {
        std::unique_ptr<T> object;
        uint64_t generation; // deletable once the audio thread has seen a graph at least this new
    };

    // juce::ChangeListener / juce::Timer / MIDI
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void handleNoteOn (juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void updateMidiInputs();
    bool devicesOpened = false;
    juce::MidiDeviceListConnection midiListConnection;

    // juce::ValueTree::Listener (message thread)
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { graphDirty = true; }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { graphDirty = true; }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { graphDirty = true; }
    void valueTreeRedirected (juce::ValueTree&) override;

    // graph building (EngineGraph.cpp)
    void applyTrackProperties (TrackRuntime&, const TrackRef&);
    void applyProjectProperties();
    DeviceInstance* acquireDevice (const DeviceRef&, std::set<int64_t>& used);
    // The automation lanes of a track or the master, with their parameters resolved (the devices must be acquired).
    std::vector<PlaybackGraph::Track::Lane> automationOf (const TrackRef&) const;
    std::shared_ptr<const daw::engine::MidiClip> acquireClip (const ClipRef&, std::set<int64_t>& used);
    std::unique_ptr<juce::AudioProcessor> makeProcessor (const DeviceRef&);
    void startPluginLoad (const DeviceRef&);
    void finishPluginLoad (int64_t deviceId, uint64_t epoch, std::unique_ptr<juce::AudioPluginInstance>, const juce::String& error,
                           const juce::String& stateBase64);
    void markDeviceMissing (int64_t deviceId);
    void prepareDevice (DeviceInstance&) const;
    void retireEverything();

    // audio thread (AudioEngine.cpp)
    void reconcileWithGraph (const PlaybackGraph&);
    void processCommands (const PlaybackGraph&);
    void finishRecordingsAtStop (const PlaybackGraph&);
    void updateRecordStates (const PlaybackGraph&, double blockEndBeat);
    void routeInputEvents (const PlaybackGraph&, const daw::engine::Transport::Block&, double samplesPerBeat, int64_t blockEngineStart, bool anyArmed);
    bool acceptsLiveMidi (const TrackRuntime&, bool fromKeyboard, bool anyArmed) const;
    void updateRecordCompensation();
    void startArrangementRecording (const PlaybackGraph&);
    void updateArrangementRecord (double blockEndBeat);
    void logSongWindows (const daw::engine::Transport::Block&, double samplesPerBeat);
    void pushEngineEvent (const EngineEvent&);
    bool isRecordTarget (const TrackRuntime&, bool anyArmed) const;
    void renderSessionAudio (const PlaybackGraph::Track&, const daw::engine::MidiClip* before, double startBefore,
                             const daw::engine::Transport::Block&, double samplesPerBeat, int numSamples);
    void renderArrangementAudio (const PlaybackGraph::Track&, double startBeat, double endBeat, int sampleOffset, int numSamples,
                                 double samplesPerBeat);
    void renderTrack (const PlaybackGraph::Track&, const daw::engine::Transport::Block&, double samplesPerBeat, int numSamples,
                      bool killNotes, bool anySolo, bool anyArmed);
    void readInput (TrackRuntime&, int numSamples);
    void feedRecordStream (const PlaybackGraph::Track&, const juce::AudioBuffer<float>& source, const daw::engine::Transport::Block&,
                           double samplesPerBeat, int numSamples, int latency);
    void runLatencyTest (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int numSamples);
    void updateAudioRecordLatency();

    Settings& settings;
    juce::AudioDeviceManager deviceManager;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    AudioFilePool audioPool;
    WarpService warp { audioPool.formats() };
    juce::File projectFolder;
    std::map<int64_t, std::shared_ptr<daw::engine::AudioSource>> peekedSources; // by FILE id, null = missing; cleared by rebuilds
    juce::MidiKeyboardState keyboardState;
    MidiInputQueue midiQueue;
    std::unique_ptr<PlayHead> playHead;

    //==========================================================================
    // Message-thread state, guarded by buildLock (never taken by the audio thread).
    mutable std::recursive_mutex buildLock;
    ProjectModel* model = nullptr;
    DeviceFactory deviceFactory;
    std::map<int64_t, std::unique_ptr<TrackRuntime>> runtimes;
    std::map<int64_t, std::unique_ptr<DeviceInstance>> devices;
    std::map<int64_t, std::unique_ptr<juce::AudioProcessor>> pendingProcessors;
    std::set<int64_t> loadingDevices;       // plugin loads in flight
    uint64_t modelEpoch = 0;                // bumped when the project is replaced, so that late loads are dropped
    std::atomic<uint32_t> deviceEpoch { 0 };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true); // seen by async callbacks
    struct ClipCacheEntry
    {
        uint64_t hash = 0;
        std::shared_ptr<const daw::engine::MidiClip> clip;
    };
    std::map<int64_t, ClipCacheEntry> clipCache;
    std::unique_ptr<PlaybackGraph> currentOwner;
    std::vector<Retired<PlaybackGraph>> retiredGraphs;
    std::vector<Retired<TrackRuntime>> retiredTracks;
    std::vector<Retired<DeviceInstance>> retiredDevices;
    std::atomic<uint64_t> generationCounter { 0 };
    std::atomic<bool> graphDirty { false };
    std::atomic<bool> audioRunning { false };

    //==========================================================================
    // Shared with the audio thread
    std::atomic<const PlaybackGraph*> current { nullptr };
    std::atomic<uint64_t> seenGeneration { 0 };

    static constexpr int kCommandQueueSize = 256;
    juce::AbstractFifo commandFifo { kCommandQueueSize };
    std::array<EngineCommand, kCommandQueueSize> commands;
    bool pushCommand (const EngineCommand&);

    PreviewPlayer preview;
    AudioPreview audioPreview;
    std::atomic<int> toneSamplesLeft { 0 };
    int toneSamplesTotal = 0;   // audio thread
    double tonePhase = 0.0;     // audio thread

    // Transport preferences and recording settings, copied from the model
    std::atomic<bool> metronomeOn { false }, metronomeOnlyRecording { false }, metronomeWood { false }, overdubOn { false };
    std::atomic<float> metronomeGain { 0.5f };
    std::atomic<int> countInBars { 1 }, recordFixedBars { 0 };
    std::atomic<double> beatsPerBar { 4.0 };
    std::atomic<int> sigNum { 4 }, sigDen { 4 }; // for the plugins' play head
    std::atomic<int> recordCompensation { 0 }, autoCompensation { 0 };
    std::atomic<double> correctionMs { 0.0 };

    // Live input events, audio thread -> message thread (single producer, single consumer)
    static constexpr int kInputQueueSize = 16384;
    juce::AbstractFifo inputFifo { kInputQueueSize };
    std::vector<daw::engine::InputEvent> inputEvents = std::vector<daw::engine::InputEvent> ((size_t) kInputQueueSize);
    std::atomic<uint32_t> inputDrops { 0 };
    std::atomic<int64_t> engineSampleCount { 0 };
    int64_t engineSamples = 0; // audio thread

    // launchAligned commands whose clip is not in the graph yet (the graph is published a moment after the model
    // changed); retried for a while. Audio thread.
    struct Deferred
    {
        EngineCommand command;
        int age = 0;
    };
    std::array<Deferred, 16> deferred;
    int numDeferred = 0;
    Metronome metronome; // audio thread

    std::atomic<bool> playing { false }, stopRequested { false };
    std::atomic<double> tempo { 120.0 }, positionBeats { 0.0 }, songPositionBeats { 0.0 }, quantise { 4.0 };
    // The arrangement properties of the model, copied by applyProjectProperties
    std::atomic<double> arrLoopStart { 0.0 }, arrLoopEnd { 16.0 }, arrPlayStart { 0.0 };
    std::atomic<bool> arrLoopOn { false }, anyInSession { false };
    std::atomic<bool> punchInOn { false }, punchOutOn { false };

    // Arrangement recording: the state machine is written by the audio thread (and acknowledged by the message thread),
    // the log travels through a fixed SPSC queue.
    std::atomic<int> arrRecState { arrangementIdle };
    std::atomic<double> arrRecStart { 0.0 }, arrRecEnd { TrackRuntime::kNoEnd };
    static constexpr int kEngineEventQueueSize = 8192;
    juce::AbstractFifo engineEventFifo { kEngineEventQueueSize };
    std::vector<EngineEvent> engineEvents = std::vector<EngineEvent> ((size_t) kEngineEventQueueSize);
    std::atomic<uint32_t> engineEventDrops { 0 };
    double expectedSongBeat = std::numeric_limits<double>::quiet_NaN(); // audio thread: where the next window should begin
    bool logEvents = false;                                              // audio thread: the current block is logged
    bool continueSong = false; // audio thread: the next start of the transport continues where the song stopped
    std::atomic<float> masterGain { 1.0f };
    float lastMasterGainL = 1.0f, lastMasterGainR = 1.0f; // audio thread: the master gain at the end of the last block
    std::atomic<float> tuneA4 { 440.0f }; // project tuning, given to every Prisma
    std::atomic<int64_t> selectedTrackId { -1 };
    std::atomic<uint32_t> hardwareMidiCount { 0 };
    std::atomic<uint32_t> diskMisses { 0 };
    daw::engine::AudioRenderStats audioStats; // audio thread
    static constexpr double kAutoFadeSeconds = 0.004;

    std::atomic<double> sampleRate { 44100.0 };
    int blockSize = 512;

    // Sound input. The callback gets the active input channels packed; inputIndex maps a device channel to its place
    // there (-1 = not open). Written in audioDeviceAboutToStart, read by the audio thread.
    static constexpr int kMaxInputChannels = 64;
    std::array<int, kMaxInputChannels> inputIndex {};
    std::atomic<int> numOpenInputs { 0 };
    const float* const* blockInputs = nullptr; // audio thread: the inputs of the block being processed
    int blockNumInputs = 0;
    int64_t blockStartSample = 0;              // audio thread: engine sample of the block's first frame
    juce::AudioBuffer<float> lastMaster;       // audio thread: the master of the previous block, the "Resample" input
    std::array<float, kAnalysisSize> analysis {};
    std::atomic<uint32_t> analysisWrite { 0 };
    const PlaybackGraph* currentGraph = nullptr; // audio thread: the graph of the block being rendered (sends find their returns)
    std::atomic<int> audioRecordLatency { 0 }, driverRoundTrip { 0 };
    std::atomic<double> audioCorrectionMs { 0.0 };

    // Loopback test: 0 idle, 1 running, 2 done. The buffer is allocated before the state goes to 1.
    std::atomic<int> latencyTestState { 0 };
    std::vector<float> latencyRecording;
    std::atomic<int> latencyInput { 0 };
    int latencyPos = 0;                        // audio thread
    static constexpr int kLatencySpacingMs = 100;

    // audio thread only
    daw::engine::Transport transport;
    uint64_t lastGeneration = 0;
    double lastCallbackMs = -1.0;
    std::atomic<ClockFn> clock;
    juce::MidiBuffer hardwareMidi, keyboardMidi, masterMidi, masterFxMidi;
    juce::AudioBuffer<float> master;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
