#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "daw/fx/Effect.h"

#include <atomic>
#include <memory>
#include <vector>

// A built-in effect (docs/spec/06-effects-mixing.md, task 6.1) as a juce::AudioProcessor, so that the chain, the knobs, the
// device window and the saved state treat it like any hosted plugin. In the model it is DEVICE kind="fx" whose pluginDesc
// holds the effect's type id ("eq8").
//
// Parameters are plugin parameters made from the effect's ParamInfo (log ranges and choices included); processBlock hands
// every value that changed to the effect, which smooths it. The state is XML: <FX type="..."><P id="..." v="..."/></FX>.
class BuiltInEffectProcessor final : public juce::AudioProcessor
{
public:
    // Null effect (unknown type): the processor passes the sound through.
    explicit BuiltInEffectProcessor (const juce::String& type);
    ~BuiltInEffectProcessor() override;

    const juce::String& getType() const { return type; }
    daw::fx::Effect* getEffect() const { return effect.get(); }

    // The key signal of compressor and gate for the next block (two channels, or null for none). Audio thread, set by the
    // engine right before processBlock.
    void setSidechain (const float* const* channels) { sidechain = channels; }

    // A display value of the effect (gain reduction, ...), see daw::fx::Effect::readout.
    float readout (int index) const { return effect != nullptr ? effect->readout (index) : 0.0f; }

    // juce::AudioProcessor
    const juce::String getName() const override;
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    double getTailLengthSeconds() const override { return effect != nullptr ? effect->tailSeconds() : 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // The default eight knobs of the compact view for this type: parameter indices.
    std::vector<int> defaultMacros() const;

private:
    void pushParameters (bool all);

    juce::String type;
    std::unique_ptr<daw::fx::Effect> effect;
    std::vector<juce::RangedAudioParameter*> params; // in the effect's order
    std::vector<float> lastSent;                     // audio thread: the normalised value last given to the effect
    const float* const* sidechain = nullptr;
    int reportedLatency = 0;
};
