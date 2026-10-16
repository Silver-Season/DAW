#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "daw/engine/Clip.h"

#include <array>
#include <atomic>
#include <memory>

// The DAW's real-time core: transport, Session View clip players, per-track instruments
// (built-in Prisma or any hosted AU/VST3), and the mixer.
class AudioEngine final : public juce::AudioIODeviceCallback, private juce::ChangeListener
{
public:
    static constexpr int kNumTracks = 8;
    static constexpr int kNumScenes = 8;

    struct Track
    {
        juce::String name;
        juce::Colour colour;
        std::array<std::unique_ptr<daw::engine::MidiClip>, kNumScenes> clips;

        std::unique_ptr<juce::AudioProcessor> instrument;
        juce::String instrumentName;

        std::atomic<float> volumeDb { 0.0f }, pan { 0.0f };
        std::atomic<bool> mute { false }, solo { false }, arm { false };
        std::atomic<float> meterL { 0.0f }, meterR { 0.0f };

        // audio thread only
        daw::engine::ClipPlayer player;
        juce::AudioBuffer<float> buffer;
        juce::MidiBuffer midi;
        std::vector<daw::MidiEvent> events;
        float lastGainL = 0.0f, lastGainR = 0.0f;
    };

    AudioEngine();
    ~AudioEngine() override;

    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }
    juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }
    juce::KnownPluginList& getKnownPlugins() { return knownPlugins; }
    juce::PropertiesFile* getSettings() { return properties.getUserSettings(); }
    juce::MidiKeyboardState& getKeyboardState() { return keyboardState; }

    // Transport
    void play();
    void stop();
    bool isPlaying() const { return playing.load(); }
    void setTempo (double bpm);
    double getTempo() const { return tempo.load(); }
    double getPositionBeats() const { return positionBeats.load(); }
    void setLaunchQuantise (double beats) { quantise.store (beats); }

    // Session View
    Track& getTrack (int index) { return *tracks[(size_t) index]; }
    void setClip (int track, int scene, std::unique_ptr<daw::engine::MidiClip> clip);
    void launchClip (int track, int scene);
    void stopTrack (int track);
    void launchScene (int scene);
    void stopAllClips();
    int getPlayingScene (int track) const { return playingScene[(size_t) track].load(); }
    int getQueuedScene (int track) const { return queuedScene[(size_t) track].load(); }

    // Instruments
    void setInstrument (int track, std::unique_ptr<juce::AudioProcessor> processor, const juce::String& name);
    void loadPluginInstrument (int track, const juce::PluginDescription& desc, std::function<void (juce::String error)> done);
    void armExclusive (int track);

    std::atomic<float> masterVolumeDb { 0.0f }, masterMeterL { 0.0f }, masterMeterR { 0.0f };

    // juce::AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

private:
    class PlayHead;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void prepareInstrument (juce::AudioProcessor&) const;
    void updateMidiInputs();

    juce::ApplicationProperties properties;
    juce::AudioDeviceManager deviceManager;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    juce::MidiKeyboardState keyboardState;
    juce::MidiMessageCollector midiCollector;

    juce::CriticalSection lock; // guards session structure between the UI and the audio thread
    std::array<std::unique_ptr<Track>, kNumTracks> tracks;
    std::unique_ptr<PlayHead> playHead;

    daw::engine::Transport transport;
    std::atomic<bool> playing { false }, stopRequested { false };
    std::atomic<double> tempo { 120.0 }, positionBeats { 0.0 }, quantise { 4.0 };
    std::array<std::atomic<int>, kNumTracks> playingScene, queuedScene;

    double sampleRate = 44100.0;
    int blockSize = 512;
    juce::MidiBuffer liveMidi;
    juce::AudioBuffer<float> master;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
