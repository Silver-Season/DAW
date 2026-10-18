#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "daw/drums/DrumKit.h"

#include <vector>

// The built-in drum machine: 16 pads on notes C1..D#2, each playing one sample (a sound we synthesise ourselves or the
// user's file). DEVICE kind="drums"; the state is the kit as text (daw::drums::KitState::toText).
//
// The kit is edited on the message thread (loadKit, setPadFile, setPadParams); samples are rendered or decoded there and
// handed to the engine without locks. A 30 Hz timer frees the samples the audio thread no longer plays.
class DrumKitProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    DrumKitProcessor();
    ~DrumKitProcessor() override;

    // The output trim of the machine: -10 dB, so a factory groove peaks near -6 dBFS and sits with the synth presets.
    static constexpr float kOutputGain = 0.31622777f;

    // Message thread
    void loadKit (const daw::drums::KitState&);
    const daw::drums::KitState& getKit() const { return kit; }
    bool setPadFile (int pad, const juce::File&); // false if the file cannot be read
    void setPadParams (int pad, const daw::drums::PadParams&);
    float padActivity (int pad) const { return engine.padActivity (pad); }
    // Bumped whenever the kit changes, so views can refresh.
    uint32_t getKitVersion() const { return kitVersion; }

    // juce::AudioProcessor
    const juce::String getName() const override { return "Drum Kit"; }
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    double getTailLengthSeconds() const override { return 2.0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return juce::String (kit.name); }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // The state text of a factory kit, for new devices.
    static juce::String factoryStateBase64 (const juce::String& kitName);

private:
    void timerCallback() override { engine.collectGarbage(); }
    void loadSamples();
    daw::drums::SamplePtr resolve (const std::string& source) const;

    daw::drums::DrumKitEngine engine;
    daw::drums::KitState kit;
    double rate = 0.0;
    uint32_t kitVersion = 0;
    juce::AudioFormatManager formats;
    std::vector<daw::MidiEvent> events; // audio thread, reserved
};
