#include "BuiltInEffectProcessor.h"

#include "../Strings.h"

namespace
{
juce::NormalisableRange<float> rangeOf (const daw::fx::ParamInfo& info)
{
    if (info.logarithmic && info.min > 0.0f)
        return { info.min, info.max,
                 [info] (float, float, float n) { return daw::fx::fromNormalised (info, n); },
                 [info] (float, float, float v) { return daw::fx::toNormalised (info, v); } };
    return { info.min, info.max };
}

juce::String valueText (const daw::fx::ParamInfo& info, float v)
{
    const float a = std::abs (v);
    const int decimals = a >= 100.0f ? 0 : (a >= 10.0f ? 1 : 2);
    if (info.unit == "Hz" && v >= 1000.0f)
        return juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + Strings::fx::kHz;
    return juce::String (v, decimals) + (info.unit.empty() ? juce::String() : " " + Strings::fx::unit (info.unit));
}
} // namespace

BuiltInEffectProcessor::BuiltInEffectProcessor (const juce::String& t)
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      type (t), effect (daw::fx::createEffect (t.toStdString()))
{
    if (effect == nullptr)
        return;
    const auto& infos = effect->params();
    for (size_t i = 0; i < infos.size(); ++i)
    {
        const auto& info = infos[i];
        const juce::ParameterID id { juce::String (info.id), 1 };
        const auto name = Strings::fx::paramName (type, juce::String (info.id), juce::String (info.name));
        std::unique_ptr<juce::RangedAudioParameter> p;
        if (info.stepped())
        {
            juce::StringArray choices;
            for (const auto& c : info.choices)
                choices.add (Strings::fx::choice (juce::String (c)));
            p = std::make_unique<juce::AudioParameterChoice> (id, name, choices, (int) info.def);
        }
        else
            p = std::make_unique<juce::AudioParameterFloat> (
                id, name, rangeOf (info), info.def,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction ([info] (float v, int) { return valueText (info, v); }));
        params.push_back (p.get());
        addParameter (p.release());
    }
    lastSent.assign (params.size(), -1.0f);
}

BuiltInEffectProcessor::~BuiltInEffectProcessor() = default;

const juce::String BuiltInEffectProcessor::getName() const
{
    return Strings::fx::effectName (type);
}

bool BuiltInEffectProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono()) && layouts.getMainInputChannelSet() == out;
}

void BuiltInEffectProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    if (effect == nullptr)
        return;
    effect->prepare (sampleRate, juce::jmax (16, maximumExpectedSamplesPerBlock));
    std::fill (lastSent.begin(), lastSent.end(), -1.0f);
    pushParameters (true);
    effect->reset();
    reportedLatency = effect->latencySamples();
    setLatencySamples (reportedLatency);
}

void BuiltInEffectProcessor::pushParameters (bool all)
{
    for (size_t i = 0; i < params.size(); ++i)
    {
        const float n = params[i]->getValue();
        if (all || ! juce::exactlyEqual (n, lastSent[i]))
        {
            lastSent[i] = n;
            effect->setParam ((int) i, params[i]->convertFrom0to1 (n));
        }
    }
}

void BuiltInEffectProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    if (effect == nullptr)
        return;
    pushParameters (false);
    if (auto* head = getPlayHead())
        if (const auto pos = head->getPosition())
        {
            daw::fx::TransportInfo info;
            info.bpm = pos->getBpm().orFallback (120.0);
            info.ppq = pos->getPpqPosition().orFallback (0.0);
            info.playing = pos->getIsPlaying();
            effect->setTransport (info);
        }
    const int channels = juce::jmin (2, buffer.getNumChannels());
    if (channels < 1)
        return;
    effect->process (buffer.getArrayOfWritePointers(), channels, buffer.getNumSamples(), effect->acceptsSidechain() ? sidechain : nullptr);
    sidechain = nullptr;
    // A changed lookahead changes the latency; the host re-aligns its paths when it sees this (PDC, task 6.6).
    if (const int latency = effect->latencySamples(); latency != reportedLatency)
    {
        reportedLatency = latency;
        setLatencySamples (latency);
    }
}

juce::AudioProcessorEditor* BuiltInEffectProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

void BuiltInEffectProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement xml ("FX");
    xml.setAttribute ("type", type);
    if (effect != nullptr)
        for (size_t i = 0; i < params.size(); ++i)
        {
            auto* p = xml.createNewChildElement ("P");
            p->setAttribute ("id", juce::String (effect->params()[i].id));
            p->setAttribute ("v", (double) params[i]->convertFrom0to1 (params[i]->getValue()));
        }
    copyXmlToBinary (xml, dest);
}

void BuiltInEffectProcessor::setStateInformation (const void* data, int size)
{
    const auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName ("FX") || effect == nullptr)
        return;
    for (auto* p : xml->getChildWithTagNameIterator ("P"))
    {
        const int index = effect->paramIndex (p->getStringAttribute ("id").toStdString());
        if (index >= 0)
            params[(size_t) index]->setValueNotifyingHost (params[(size_t) index]->convertTo0to1 ((float) p->getDoubleAttribute ("v")));
    }
}

std::vector<int> BuiltInEffectProcessor::defaultMacros() const
{
    static const std::map<juce::String, std::vector<const char*>> knobs {
        { "eq8", { "b2gain", "b3gain", "b4gain", "b5gain", "b6gain", "b7gain", "b1on", "b8on" } },
        { "comp", { "threshold", "ratio", "attack", "release", "knee", "makeup", "mix", "output" } },
        { "gate", { "threshold", "hysteresis", "attack", "hold", "release", "floor", "lookahead", "output" } },
        { "limiter", { "ceiling", "gain", "release", "lookahead", "truePeak", "autoRelease", "output" } },
        { "reverb", { "size", "decay", "predelay", "damping", "lowCut", "width", "diffusion", "mix" } },
        { "delay", { "divL", "divR", "feedback", "pingPong", "hp", "lp", "ducking", "mix" } },
        { "drive", { "mode", "drive", "tone", "bias", "input", "mix", "output" } },
        { "crush", { "bits", "rate", "dither", "mix", "output" } },
        { "filter", { "type", "freq", "resonance", "drive", "lfoDepth", "lfoRate", "envDepth", "mix" } },
        { "chorus", { "voices", "rate", "depth", "delay", "width", "feedback", "mix", "output" } },
        { "flanger", { "rate", "depth", "delay", "feedback", "phase", "sync", "div", "mix" } },
        { "phaser", { "stages", "rate", "depth", "center", "feedback", "phase", "sync", "mix" } },
        { "autopan", { "rate", "div", "sync", "depth", "shape", "phase", "offset", "mix" } },
    };
    std::vector<int> result;
    if (effect == nullptr)
        return result;
    if (const auto it = knobs.find (type); it != knobs.end())
        for (const auto* id : it->second)
            if (const int index = effect->paramIndex (id); index >= 0)
                result.push_back (index);
    for (int i = 0; result.size() < 8 && i < (int) params.size(); ++i)
        if (std::find (result.begin(), result.end(), i) == result.end())
            result.push_back (i);
    return result;
}

juce::String BuiltInEffectProcessor::stateBase64 (const juce::String& t, const std::vector<std::pair<juce::String, float>>& values)
{
    BuiltInEffectProcessor p (t);
    for (const auto& [id, v] : values)
        if (p.effect != nullptr)
            if (const int index = p.effect->paramIndex (id.toStdString()); index >= 0)
                p.params[(size_t) index]->setValueNotifyingHost (p.params[(size_t) index]->convertTo0to1 (v));
    juce::MemoryBlock block;
    p.getStateInformation (block);
    return block.toBase64Encoding();
}
