#include "PreviewPlayer.h"

#include "../../../plugins/prisma/Source/PrismaProcessor.h"

PreviewPlayer::PreviewPlayer()
{
    // C minor arpeggio, eighth notes, two bars: C Eb G C' G Eb G Eb | G, C Eb G Eb C Eb C
    const int pitches[16] { 60, 63, 67, 72, 67, 63, 67, 63, 55, 60, 63, 67, 63, 60, 63, 60 };
    for (int i = 0; i < 16; ++i)
        phrase[(size_t) i] = { i * 0.5, 0.45, pitches[i] };
}

PreviewPlayer::~PreviewPlayer() = default;

void PreviewPlayer::prepareVoice (Voice& v)
{
    if (v.synth == nullptr)
        v.synth = std::make_unique<PrismaProcessor>();
    v.synth->setRateAndBufferSizeDetails (sampleRate, maxBlock);
    v.synth->prepareToPlay (sampleRate, maxBlock);
    v.buffer.setSize (2, maxBlock, false, true, false);
    v.midi.ensureSize (1024);
    v.running = false;
    v.level = 0.0f;
}

void PreviewPlayer::prepare (double rate, int blockSize)
{
    // The instances are made on first use, which keeps the start of the audio device (and every test that
    // starts a fake one) free of two extra synths.
    sampleRate = rate;
    maxBlock = blockSize;
    for (auto& v : voices)
        if (v.synth != nullptr)
            prepareVoice (v);
    sounding = false;
}

void PreviewPlayer::release()
{
    for (auto& v : voices)
        if (v.synth != nullptr)
            v.synth->releaseResources();
}

void PreviewPlayer::request (int voice)
{
    requestedVoice.store (voice);
    requestSerial.fetch_add (1);
}

void PreviewPlayer::playFactoryProgram (int program)
{
    auto& v = voices[(size_t) idleVoice];
    if (v.synth == nullptr)
        prepareVoice (v);
    v.synth->setCurrentProgram (program);
    request (idleVoice);
    idleVoice = 1 - idleVoice;
}

void PreviewPlayer::playState (const void* data, int size)
{
    auto& v = voices[(size_t) idleVoice];
    if (v.synth == nullptr)
        prepareVoice (v);
    v.synth->setStateInformation (data, size);
    request (idleVoice);
    idleVoice = 1 - idleVoice;
}

void PreviewPlayer::stop()
{
    request (-1);
}

//==============================================================================
void PreviewPlayer::startPhrase (Voice& v)
{
    v.beat = 0.0;
    v.tailLeft = 3.0; // how long the release may ring on after the phrase before the voice is given up
    v.level = 1.0f;
    v.running = true;
    v.fadingOut = false;
}

void PreviewPlayer::process (juce::AudioBuffer<float>& out, int numSamples, double bpm)
{
    const auto serial = requestSerial.load();
    if (serial != seenSerial)
    {
        seenSerial = serial;
        const int wanted = requestedVoice.load();
        for (int i = 0; i < 2; ++i)
        {
            auto& v = voices[(size_t) i];
            if (i == wanted)
                startPhrase (v);
            else if (v.running)
            {
                v.fadingOut = true; // the old phrase: notes off, then a short fade
                for (int ch = 1; ch <= 16; ++ch)
                    v.midi.addEvent (juce::MidiMessage::allNotesOff (ch), 0);
            }
        }
    }

    const float userGain = gain.load();
    bool any = false;
    for (auto& v : voices)
        if (v.running && v.synth != nullptr)
        {
            renderVoice (v, out, numSamples, bpm, userGain, lastUserGain);
            any = true;
        }
    lastUserGain = userGain;
    sounding.store (any, std::memory_order_relaxed);
}

void PreviewPlayer::renderVoice (Voice& v, juce::AudioBuffer<float>& out, int numSamples, double bpm, float userGain, float userGainStart)
{
    const double samplesPerBeat = sampleRate * 60.0 / juce::jmax (1.0, bpm);
    const double blockBeats = numSamples / samplesPerBeat;
    const double from = v.beat, to = v.beat + blockBeats;

    if (! v.fadingOut)
    {
        for (const auto& n : phrase)
        {
            const double end = n.start + n.length;
            if (n.start >= from && n.start < to)
                v.midi.addEvent (juce::MidiMessage::noteOn (1, n.pitch, (juce::uint8) 100), juce::jlimit (0, numSamples - 1, (int) ((n.start - from) * samplesPerBeat)));
            if (end >= from && end < to)
                v.midi.addEvent (juce::MidiMessage::noteOff (1, n.pitch), juce::jlimit (0, numSamples - 1, (int) ((end - from) * samplesPerBeat)));
        }
        v.beat = to;
    }

    auto view = juce::AudioBuffer<float> (v.buffer.getArrayOfWritePointers(), 2, numSamples);
    view.clear();
    v.synth->processBlock (view, v.midi);
    v.midi.clear();

    // The fade of an interrupted phrase: about 10 ms, linear.
    const float step = (float) (1.0 / (0.010 * sampleRate));
    const float startLevel = v.level;
    if (v.fadingOut)
        v.level = juce::jmax (0.0f, v.level - step * (float) numSamples);
    for (int ch = 0; ch < 2; ++ch)
    {
        view.applyGainRamp (ch, 0, numSamples, startLevel, v.level);
        out.addFromWithRamp (ch, 0, view.getReadPointer (ch), numSamples, userGainStart, userGain);
    }

    // A voice stays in the loop until its release has died away (or three seconds passed), so that nothing
    // is left ringing for the next phrase that uses this instance.
    const bool phraseOver = v.fadingOut ? v.level <= 0.0f : v.beat >= kPhraseBeats;
    if (phraseOver)
    {
        v.tailLeft -= numSamples / sampleRate;
        if (v.tailLeft <= 0.0 || v.synth->getNumActiveVoices() == 0)
            v.running = false;
    }
}
