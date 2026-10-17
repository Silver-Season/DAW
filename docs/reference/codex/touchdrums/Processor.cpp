#include "Processor.h"
#include "Editor.h"
#include <cmath>

juce::AudioProcessorValueTreeState::ParameterLayout TouchDrumProcessor::parameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    p.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "output", 1 }, "Output", -36.0f, 0.0f, -9.0f));
    p.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "velocity", 1 }, "Touch velocity", 1, 127, 105));
    p.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "pitch", 1 }, "Kit tune", -12.0f, 12.0f, 0.0f));
    for (int i = 0; i < 8; ++i)
        p.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "level" + juce::String (i), 1 },
            juce::String (touchdrums::names[i]) + " level", -36.0f, 6.0f, 0.0f));
    return p;
}
TouchDrumProcessor::TouchDrumProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "PrismaTouch", parameters())
{
    output = state.getRawParameterValue ("output"); velocity = state.getRawParameterValue ("velocity");
    pitch = state.getRawParameterValue ("pitch");
    for (int i = 0; i < 8; ++i) levels[(size_t) i] = state.getRawParameterValue ("level" + juce::String (i));
    buildKit (44100);
    for (size_t pad = 0; pad < 8; ++pad)
        for (size_t x = 0; x < 96; ++x)
        {
            float v = 0;
            const auto start = x * samples[pad].size() / 96, end = (x + 1) * samples[pad].size() / 96;
            for (auto n = start; n < end; ++n) v = juce::jmax (v, std::abs (samples[pad][n]));
            previews[pad][x] = v;
        }
}
void TouchDrumProcessor::buildKit (double sr)
{
    const double pi = juce::MathConstants<double>::pi;
    const double lengths[8] { 1.1, .6, .16, 1.2, .55, 1.0, .22, .55 };
    for (int pad = 0; pad < 8; ++pad)
    {
        auto& dst = samples[(size_t) pad]; dst.resize ((size_t) (sr * lengths[pad]));
        uint32_t rng = 0x12345678u + (uint32_t) pad * 193u;
        double phase = 0, prevNoise = 0, high = 0;
        for (size_t n = 0; n < dst.size(); ++n)
        {
            const double t = (double) n / sr;
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            const double noise = (double) rng / 2147483648.0 - 1.0;
            high = .72 * (high + noise - prevNoise); prevNoise = noise;
            double v = 0;
            switch (pad)
            {
                case 0:
                    phase += 2*pi*(46 + 125*std::exp (-t*42))/sr;
                    v = std::sin (phase)*std::exp (-t*6.8) + .15*high*std::exp (-t*240); break;
                case 1:
                    v = .66*high*std::exp (-t*17) + .32*std::sin (2*pi*180*t)*std::exp (-t*24); break;
                case 2: case 3:
                {
                    double metal = 0;
                    for (double hz : { 4021., 5173., 6137., 7919. }) metal += std::sin (2*pi*hz*t) > 0 ? .10 : -.10;
                    v = (.60*high + metal)*std::exp (-t*(pad == 2 ? 65 : 8.5)); break;
                }
                case 4:
                {
                    double envelope = .36*std::exp (-t*14);
                    for (double onset : { 0., .012, .024 }) if (t >= onset) envelope += .43*std::exp (-(t-onset)*180);
                    v = high*envelope; break;
                }
                case 5:
                    phase += 2*pi*(87 + 65*std::exp (-t*27))/sr;
                    v = .8*std::sin (phase)*std::exp (-t*8) + .05*high*std::exp (-t*100); break;
                case 6:
                    v = (.55*std::sin (2*pi*830*t) + .28*std::sin (2*pi*1730*t))*std::exp (-t*65) + .13*high*std::exp (-t*180); break;
                case 7:
                    v = (.40*std::sin (2*pi*540*t + 2*std::sin (2*pi*810*t)*std::exp (-t*16)))*std::exp (-t*12); break;
                default: break;
            }
            const double fadeIn = juce::jmin (1.0, t * 5000);
            const double fadeOut = juce::jmin (1.0, ((double) dst.size() - 1 - (double) n) / (sr*.005));
            dst[n] = (float) (std::tanh (v*1.25) * fadeIn * fadeOut * .8);
        }
    }
}
void TouchDrumProcessor::prepareToPlay (double sr, int)
{
    rate = sr > 0 ? sr : 44100;
    buildKit (rate);
    for (auto& v : voices) v = {};
    gain.reset (rate, .015); gain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (output->load()));
    peak.store (0);
}
bool TouchDrumProcessor::isBusesLayoutSupported (const BusesLayout& b) const
{
    return b.getMainInputChannelSet().isDisabled() && (b.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        || b.getMainOutputChannelSet() == juce::AudioChannelSet::mono());
}
bool TouchDrumProcessor::triggerFromUI (int pad) noexcept
{
    if (pad < 0 || pad >= 8) return false;
    auto scope = fifo.write (1);
    if (scope.blockSize1 + scope.blockSize2 == 0) { dropped.fetch_add (1); return false; }
    queue[(size_t) (scope.blockSize1 ? scope.startIndex1 : scope.startIndex2)] = { pad, velocity->load() / 127.0f };
    return true;
}
void TouchDrumProcessor::strike (int pad, float vel) noexcept
{
    if (pad == 2) for (auto& v : voices) if (v.pad == 3) v.choking = true;
    auto* dst = &voices[0];
    for (auto& v : voices)
        if (v.pad < 0) { dst = &v; break; } else if (v.age < dst->age) dst = &v;
    *dst = { pad, 0, std::pow (2.0, pitch->load()/12.0),
        vel*vel*juce::Decibels::decibelsToGain (levels[(size_t) pad]->load()), 1, false, ++serial };
    hits[(size_t) pad].fetch_add (1, std::memory_order_relaxed);
}
void TouchDrumProcessor::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    audio.clear();
    if (panic.exchange (false))
    {
        for (auto& v : voices) v = {};
        auto scope = fifo.read (fifo.getNumReady());
        juce::ignoreUnused (scope);
        midi.clear(); peak.store (0); return;
    }
    if (audio.getNumSamples() == 0) return;
    {
        auto scope = fifo.read (fifo.getNumReady());
        for (int j = 0; j < scope.blockSize1; ++j) { const auto h = queue[(size_t) (scope.startIndex1+j)]; strike (h.pad,h.velocity); }
        for (int j = 0; j < scope.blockSize2; ++j) { const auto h = queue[(size_t) (scope.startIndex2+j)]; strike (h.pad,h.velocity); }
    }
    gain.setTargetValue (juce::Decibels::decibelsToGain (output->load()));
    auto it = midi.cbegin(); float blockPeak = 0;
    for (int n = 0; n < audio.getNumSamples(); ++n)
    {
        while (it != midi.cend() && (*it).samplePosition <= n)
        {
            const auto meta = *it; ++it;
            if (meta.numBytes < 1 || meta.numBytes > 3 || meta.data[0] >= 0xf0) continue;
            auto m = meta.getMessage();
            if (m.isAllNotesOff() || m.isAllSoundOff()) for (auto& v : voices) v = {};
            if (m.isNoteOn()) for (int pad = 0; pad < 8; ++pad)
                if (m.getNoteNumber() == touchdrums::notes[pad]) strike (pad, m.getFloatVelocity());
        }
        float sum = 0;
        for (auto& v : voices)
        {
            if (v.pad < 0) continue;
            const auto& sample = samples[(size_t) v.pad]; const auto index = (size_t) v.pos;
            if (index + 1 >= sample.size()) { v.pad = -1; continue; }
            if (v.choking) { v.choke -= (float) (1.0 / (rate*.003)); if (v.choke <= 0) { v.pad = -1; continue; } }
            const float fract = (float) (v.pos - (double) index);
            sum += (sample[index] + fract*(sample[index+1]-sample[index]))*v.gain*v.choke;
            v.pos += v.step;
        }
        const float out = std::tanh (sum*gain.getNextValue());
        blockPeak = juce::jmax (blockPeak, std::abs (out));
        for (int ch = 0; ch < audio.getNumChannels(); ++ch) audio.setSample (ch,n,out);
    }
    peak.store (juce::jmax (blockPeak, peak.load()*.88f), std::memory_order_relaxed);
    midi.clear();
}
void TouchDrumProcessor::getStateInformation (juce::MemoryBlock& data)
{
    if (auto xml = state.copyState().createXml()) copyXmlToBinary (*xml,data);
}
void TouchDrumProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data,size)) if (xml->hasTagName (state.state.getType())) state.replaceState (juce::ValueTree::fromXml (*xml));
}
juce::AudioProcessorEditor* TouchDrumProcessor::createEditor() { return new TouchDrumEditor (*this); }
