#include "PrismaProcessor.h"
#include "PrismaEditor.h"

using daw::synth::ParamDescriptor;

PrismaProcessor::PrismaProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Prisma", createParameterLayout())
{
    for (const auto& d : daw::synth::parameterDescriptors())
    {
        auto* raw = apvts.getRawParameterValue (d.id);
        jassert (raw != nullptr);
        rawValues.push_back (raw);
    }
    events.reserve (4096);
}

juce::AudioProcessorValueTreeState::ParameterLayout PrismaProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& d : daw::synth::parameterDescriptors())
    {
        const juce::ParameterID id { d.id, 1 };
        switch (d.kind)
        {
            case ParamDescriptor::Kind::Float:
            {
                juce::NormalisableRange<float> range (d.min, d.max);
                if (d.skewCentre > 0.0f)
                    range.setSkewForCentre (d.skewCentre);
                auto toText = [unit = juce::String (d.unit)] (float v, int) {
                    const int decimals = std::abs (v) >= 100.0f ? 0 : (std::abs (v) >= 10.0f ? 1 : 2);
                    return juce::String (v, decimals) + (unit.isEmpty() ? "" : " " + unit);
                };
                layout.add (std::make_unique<juce::AudioParameterFloat> (
                    id, d.name, range, d.def,
                    juce::AudioParameterFloatAttributes().withLabel (d.unit).withStringFromValueFunction (toText)));
                break;
            }
            case ParamDescriptor::Kind::Int:
                layout.add (std::make_unique<juce::AudioParameterInt> (id, d.name, (int) d.min, (int) d.max, (int) d.def));
                break;
            case ParamDescriptor::Kind::Bool:
                layout.add (std::make_unique<juce::AudioParameterBool> (id, d.name, d.def > 0.5f));
                break;
            case ParamDescriptor::Kind::Choice:
            {
                juce::StringArray choices;
                for (const auto& c : d.choices)
                    choices.add (c);
                layout.add (std::make_unique<juce::AudioParameterChoice> (id, d.name, choices, (int) d.def));
                break;
            }
        }
    }
    return layout;
}

bool PrismaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
           || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

void PrismaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    synth.prepare (sampleRate, samplesPerBlock);
    keyboardState.reset();
}

void PrismaProcessor::pullParameters()
{
    const auto& descriptors = daw::synth::parameterDescriptors();
    for (size_t i = 0; i < descriptors.size(); ++i)
        descriptors[i].ref (params) = rawValues[i]->load (std::memory_order_relaxed);
    synth.setParams (params);
}

void PrismaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);
    pullParameters();
    synth.setTuneA4 (tuneA4.load (std::memory_order_relaxed));

    if (auto* head = getPlayHead())
        if (auto pos = head->getPosition())
            if (auto bpm = pos->getBpm())
                synth.setTempo (*bpm);

    events.clear();
    for (const auto meta : midi)
    {
        const auto* data = meta.data;
        if (meta.numBytes < 1 || events.size() == events.capacity())
            continue;
        daw::MidiEvent e;
        e.sampleOffset = juce::jlimit (0, numSamples - 1, meta.samplePosition);
        e.status = data[0];
        e.data1 = meta.numBytes > 1 ? data[1] : 0;
        e.data2 = meta.numBytes > 2 ? data[2] : 0;
        events.push_back (e);
    }

    if (buffer.getNumChannels() >= 2)
        synth.process (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples, events.data(), (int) events.size());
    else if (buffer.getNumChannels() == 1)
    {
        // Mono output: render the right channel into a scratch buffer and fold down.
        float* l = buffer.getWritePointer (0);
        thread_local std::vector<float> right;
        if ((int) right.size() < numSamples)
            right.resize ((size_t) numSamples); // only grows on the first mono block
        synth.process (l, right.data(), numSamples, events.data(), (int) events.size());
        for (int i = 0; i < numSamples; ++i)
            l[i] = 0.5f * (l[i] + right[(size_t) i]);
    }

    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    activeVoices.store (synth.getNumActiveVoices(), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* PrismaProcessor::createEditor()
{
    return new PrismaEditor (*this);
}

int PrismaProcessor::getNumPrograms()
{
    return (int) daw::synth::factoryPresets().size();
}

const juce::String PrismaProcessor::getProgramName (int index)
{
    const auto& presets = daw::synth::factoryPresets();
    if (index < 0 || index >= (int) presets.size())
        return {};
    return presets[(size_t) index].name;
}

void PrismaProcessor::setCurrentProgram (int index)
{
    const auto& presets = daw::synth::factoryPresets();
    if (index < 0 || index >= (int) presets.size())
        return;
    currentProgram = index;
    const auto& p = presets[(size_t) index].params;
    for (const auto& d : daw::synth::parameterDescriptors())
        if (auto* param = apvts.getParameter (d.id))
            param->setValueNotifyingHost (param->convertTo0to1 (d.get (p)));
}

void PrismaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PrismaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = state.getProperty ("program", 0);
            apvts.replaceState (state);
        }
}
