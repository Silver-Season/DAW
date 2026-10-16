#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "../Settings.h"
#include "../model/ProjectModel.h"
#include "MidiInputQueue.h"
#include "PlaybackGraph.h"

#include "daw/engine/Clip.h"

#include <atomic>
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

    explicit AudioEngine (Settings&);
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
    void play();
    void stop();
    bool isPlaying() const { return playing.load(); }
    double getTempo() const { return tempo.load(); }
    double getPositionBeats() const { return positionBeats.load(); }

    // Session View commands: queued for the audio thread. They return false if the queue is full.
    bool launchClip (int64_t trackId, int scene);
    bool launchScene (int scene);
    bool stopTrack (int64_t trackId);
    bool stopAllClips();

    // Live MIDI from any thread, with the time (ms, juce::Time::getMillisecondCounterHiRes) it happened.
    // The audio thread turns the time into a sample offset inside the block. Used by the device callbacks
    // and the on-screen keyboard; tests call it with made-up times.
    bool pushLiveMidi (const uint8_t* bytes, int size, bool fromKeyboard, double timeMs);

    // The clock the audio thread reads once per block (default: the hi-res millisecond counter). Tests
    // replace it with a function returning a scripted time.
    using ClockFn = double (*) ();
    void setClockForTesting (ClockFn fn) { clock.store (fn); }

    std::atomic<float> masterMeterL { 0.0f }, masterMeterR { 0.0f };

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

    // juce::ValueTree::Listener (message thread)
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { graphDirty = true; }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { graphDirty = true; }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { graphDirty = true; }
    void valueTreeRedirected (juce::ValueTree&) override { graphDirty = true; }

    // graph building (EngineGraph.cpp)
    void applyTrackProperties (TrackRuntime&, const TrackRef&);
    void applyProjectProperties();
    DeviceInstance* acquireDevice (const DeviceRef&, std::set<int64_t>& used);
    std::shared_ptr<const daw::engine::MidiClip> acquireClip (const ClipRef&, std::set<int64_t>& used);
    std::unique_ptr<juce::AudioProcessor> makeProcessor (const DeviceRef&);
    void prepareDevice (DeviceInstance&) const;
    void retireEverything();

    // audio thread (AudioEngine.cpp)
    void reconcileWithGraph (const PlaybackGraph&);
    void processCommands (const PlaybackGraph&);
    void renderTrack (const PlaybackGraph::Track&, const daw::engine::Transport::Block&, double samplesPerBeat, int numSamples,
                      bool killNotes, bool anySolo, bool anyArmed);

    Settings& settings;
    juce::AudioDeviceManager deviceManager;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
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

    std::atomic<bool> playing { false }, stopRequested { false };
    std::atomic<double> tempo { 120.0 }, positionBeats { 0.0 }, quantise { 4.0 };
    std::atomic<float> masterGain { 1.0f };
    std::atomic<int64_t> selectedTrackId { -1 };
    std::atomic<uint32_t> hardwareMidiCount { 0 };

    std::atomic<double> sampleRate { 44100.0 };
    int blockSize = 512;

    // audio thread only
    daw::engine::Transport transport;
    uint64_t lastGeneration = 0;
    double lastCallbackMs = -1.0;
    std::atomic<ClockFn> clock;
    juce::MidiBuffer hardwareMidi, keyboardMidi, masterMidi, masterFxMidi;
    juce::AudioBuffer<float> master;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
