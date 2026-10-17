#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "daw/synth/PrismaSynth.h"

// JUCE wrapper around the framework-free Prisma engine. Used both as the AU/VST3/Standalone plugin
// and directly by the DAW as its built-in instrument.
class PrismaProcessor final : public juce::AudioProcessor
{
public:
    PrismaProcessor();

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Prisma"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    const daw::dsp::Wavetable* getTable (int index) const { return synth.getTable (index); }
    int getNumActiveVoices() const { return activeVoices.load(); }

    // Frequency of A4 in Hz. A project setting the DAW gives every Prisma it hosts: it is not a plugin parameter,
    // so it is neither automated nor saved in presets or in the plugin state.
    void setTuneA4 (float hz) noexcept { tuneA4.store (hz, std::memory_order_relaxed); }
    float getTuneA4() const noexcept { return tuneA4.load (std::memory_order_relaxed); }

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState; // on-screen keyboard in the editor

private:
    void pullParameters();

    daw::synth::PrismaSynth synth;
    daw::synth::PrismaParams params;
    std::vector<std::atomic<float>*> rawValues; // same order as parameterDescriptors()
    std::vector<daw::MidiEvent> events;
    int currentProgram = 0;
    std::atomic<int> activeVoices { 0 };
    std::atomic<float> tuneA4 { 440.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrismaProcessor)
};
