#include "DrumKitProcessor.h"

#include "daw/drums/DrumSynth.h"

DrumKitProcessor::DrumKitProcessor() : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    formats.registerBasicFormats();
    events.reserve (daw::drums::DrumKitEngine::kMaxEventsPerBlock);
    kit = daw::drums::factoryKitState (daw::drums::factoryKits().front().name);
    startTimerHz (30);
}

DrumKitProcessor::~DrumKitProcessor()
{
    stopTimer();
}

daw::drums::SamplePtr DrumKitProcessor::resolve (const std::string& source) const
{
    if (source.empty() || rate <= 0.0)
        return nullptr;
    std::string kitName;
    int pad = 0;
    if (daw::drums::parseFactorySource (source, kitName, pad))
        return daw::drums::renderFactorySound (kitName, pad, rate);

    const juce::File file { juce::String (source) };
    std::unique_ptr<juce::AudioFormatReader> reader (const_cast<juce::AudioFormatManager&> (formats).createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return nullptr;
    // A pad is a one-shot: anything longer than 30 s is cut.
    const int frames = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 30.0));
    juce::AudioBuffer<float> buffer ((int) juce::jmax (1u, juce::jmin (2u, reader->numChannels)), frames);
    reader->read (&buffer, 0, frames, 0, true, buffer.getNumChannels() > 1);
    auto sample = std::make_shared<daw::drums::Sample>();
    sample->rate = reader->sampleRate;
    sample->channels = buffer.getNumChannels();
    sample->left.assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + frames);
    if (sample->channels > 1)
        sample->right.assign (buffer.getReadPointer (1), buffer.getReadPointer (1) + frames);
    return sample;
}

void DrumKitProcessor::loadSamples()
{
    for (int pad = 0; pad < daw::drums::kNumPads; ++pad)
    {
        engine.setPadSample (pad, resolve (kit.pads[(size_t) pad].source));
        engine.setPadParams (pad, kit.pads[(size_t) pad].params);
    }
    ++kitVersion;
}

void DrumKitProcessor::loadKit (const daw::drums::KitState& newKit)
{
    kit = newKit;
    loadSamples();
}

bool DrumKitProcessor::setPadFile (int pad, const juce::File& file)
{
    if (pad < 0 || pad >= daw::drums::kNumPads)
        return false;
    const auto source = file.getFullPathName().toStdString();
    auto sample = resolve (source);
    if (sample == nullptr)
        return false;
    kit.pads[(size_t) pad].source = source;
    kit.pads[(size_t) pad].name = file.getFileNameWithoutExtension().toStdString();
    engine.setPadSample (pad, std::move (sample));
    ++kitVersion;
    return true;
}

void DrumKitProcessor::setPadParams (int pad, const daw::drums::PadParams& p)
{
    if (pad < 0 || pad >= daw::drums::kNumPads)
        return;
    kit.pads[(size_t) pad].params = p;
    engine.setPadParams (pad, p);
    ++kitVersion;
}

void DrumKitProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    const bool rateChanged = std::abs (sampleRate - rate) > 0.5;
    rate = sampleRate;
    engine.prepare (sampleRate, juce::jmax (16, maximumExpectedSamplesPerBlock));
    if (rateChanged || engine.getPadSample (0) == nullptr)
        loadSamples(); // the factory sounds are rendered at the device rate, so nothing is resampled
}

void DrumKitProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    events.clear();
    for (const auto meta : midi)
    {
        if (meta.numBytes < 2 || events.size() >= events.capacity())
            continue;
        daw::MidiEvent e;
        e.sampleOffset = juce::jlimit (0, buffer.getNumSamples() - 1, meta.samplePosition);
        e.status = meta.data[0];
        e.data1 = meta.data[1];
        e.data2 = meta.numBytes > 2 ? meta.data[2] : 0;
        events.push_back (e);
    }
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    if (right == nullptr)
        return;
    engine.process (buffer.getWritePointer (0), right, buffer.getNumSamples(), events.data(), (int) events.size());
}

void DrumKitProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    const auto text = kit.toText();
    dest.replaceAll (text.data(), text.size());
}

void DrumKitProcessor::setStateInformation (const void* data, int size)
{
    daw::drums::KitState parsed;
    if (size > 0 && daw::drums::KitState::fromText (std::string (static_cast<const char*> (data), (size_t) size), parsed))
        loadKit (parsed);
}

juce::String DrumKitProcessor::factoryStateBase64 (const juce::String& kitName)
{
    const auto text = daw::drums::factoryKitState (kitName.toStdString()).toText();
    juce::MemoryBlock block (text.data(), text.size());
    return block.toBase64Encoding();
}
