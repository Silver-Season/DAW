#include "AudioEngine.h"

//==============================================================================
// Gives hosted instruments the tempo and position (LFO sync, arpeggiators, delays).
class AudioEngine::PlayHead final : public juce::AudioPlayHead
{
public:
    explicit PlayHead (AudioEngine& e) : engine (e) {}

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        const double beats = engine.positionBeats.load();
        info.setBpm (engine.tempo.load());
        info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        info.setPpqPosition (beats);
        info.setPpqPositionOfLastBarStart (std::floor (beats / 4.0) * 4.0);
        info.setTimeInSeconds (beats * 60.0 / engine.tempo.load());
        info.setTimeInSamples ((int64_t) (beats * 60.0 / engine.tempo.load() * engine.sampleRate));
        info.setIsPlaying (engine.playing.load());
        return info;
    }

private:
    AudioEngine& engine;
};

//==============================================================================
AudioEngine::AudioEngine()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "DAW";
    options.filenameSuffix = ".settings";
    options.folderName = "DAW";
    options.osxLibrarySubFolder = "Application Support";
    properties.setStorageParameters (options);

    formatManager.addDefaultFormats();

    if (auto xml = getSettings()->getXmlValue ("knownPlugins"))
        knownPlugins.recreateFromXml (*xml);
    knownPlugins.addChangeListener (this);

    playHead = std::make_unique<PlayHead> (*this);

    const juce::Colour palette[] { juce::Colour (0xffa78bfa), juce::Colour (0xff4fd1c5), juce::Colour (0xfff6ad55), juce::Colour (0xfffc8181),
                                   juce::Colour (0xff63b3ed), juce::Colour (0xff68d391), juce::Colour (0xfff687b3), juce::Colour (0xffecc94b) };
    for (int i = 0; i < kNumTracks; ++i)
    {
        tracks[(size_t) i] = std::make_unique<Track>();
        tracks[(size_t) i]->name = "Track " + juce::String (i + 1);
        tracks[(size_t) i]->colour = palette[i];
        tracks[(size_t) i]->events.reserve (1024);
        tracks[(size_t) i]->midi.ensureSize (4096);
        playingScene[(size_t) i] = -1;
        queuedScene[(size_t) i] = -1;
    }
    liveMidi.ensureSize (4096);

    auto savedDevice = getSettings()->getXmlValue ("audioDevice");
    const auto error = deviceManager.initialise (0, 2, savedDevice.get(), true);
    if (error.isNotEmpty())
    {
        DBG ("Audio device error: " << error);
    }
    deviceManager.addChangeListener (this);

    updateMidiInputs();
    deviceManager.addMidiInputDeviceCallback ({}, &midiCollector);
    deviceManager.addAudioCallback (this);
}

AudioEngine::~AudioEngine()
{
    deviceManager.removeAudioCallback (this);
    deviceManager.removeMidiInputDeviceCallback ({}, &midiCollector);
    deviceManager.closeAudioDevice();
    knownPlugins.removeChangeListener (this);
    deviceManager.removeChangeListener (this);
    for (auto& t : tracks)
        t->instrument.reset();
}

void AudioEngine::updateMidiInputs()
{
    // Every connected MIDI keyboard/controller plays the armed track.
    for (const auto& input : juce::MidiInput::getAvailableDevices())
        if (! deviceManager.isMidiInputDeviceEnabled (input.identifier))
            deviceManager.setMidiInputDeviceEnabled (input.identifier, true);
}

void AudioEngine::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &knownPlugins)
    {
        if (auto xml = knownPlugins.createXml())
            getSettings()->setValue ("knownPlugins", xml.get());
    }
    else if (source == &deviceManager)
    {
        if (auto xml = deviceManager.createStateXml())
            getSettings()->setValue ("audioDevice", xml.get());
    }
    getSettings()->saveIfNeeded();
}

//==============================================================================
void AudioEngine::play()
{
    playing = true;
}

void AudioEngine::stop()
{
    playing = false;
    stopRequested = true;
}

void AudioEngine::setTempo (double bpm)
{
    tempo = juce::jlimit (20.0, 999.0, bpm);
}

void AudioEngine::setClip (int trackIndex, int scene, std::unique_ptr<daw::engine::MidiClip> clip)
{
    auto& t = getTrack (trackIndex);
    {
        const juce::ScopedLock sl (lock);
        std::swap (t.clips[(size_t) scene], clip);
        t.player.clipChanged (scene, t.clips[(size_t) scene].get());
    }
    // the previous clip (if any) is destroyed here, outside the lock
}

void AudioEngine::launchClip (int trackIndex, int scene)
{
    auto& t = getTrack (trackIndex);
    const juce::ScopedLock sl (lock);
    if (t.clips[(size_t) scene] == nullptr)
        return;
    const bool wasPlaying = playing.load();
    t.player.launch (scene, t.clips[(size_t) scene].get(), transport.getPositionBeats(), wasPlaying ? quantise.load() : 0.0);
    playing = true; // launching a clip starts the transport, like in Live
}

void AudioEngine::stopTrack (int trackIndex)
{
    const juce::ScopedLock sl (lock);
    getTrack (trackIndex).player.stop (transport.getPositionBeats(), playing.load() ? quantise.load() : 0.0);
}

void AudioEngine::launchScene (int scene)
{
    for (int i = 0; i < kNumTracks; ++i)
    {
        if (getTrack (i).clips[(size_t) scene] != nullptr)
            launchClip (i, scene);
        else
            stopTrack (i);
    }
}

void AudioEngine::stopAllClips()
{
    for (int i = 0; i < kNumTracks; ++i)
        stopTrack (i);
}

void AudioEngine::armExclusive (int trackIndex)
{
    for (int i = 0; i < kNumTracks; ++i)
        getTrack (i).arm = (i == trackIndex);
}

//==============================================================================
void AudioEngine::prepareInstrument (juce::AudioProcessor& p) const
{
    p.setPlayHead (playHead.get());
    p.enableAllBuses();
    p.setRateAndBufferSizeDetails (sampleRate, blockSize);
    p.prepareToPlay (sampleRate, blockSize);
}

void AudioEngine::setInstrument (int trackIndex, std::unique_ptr<juce::AudioProcessor> processor, const juce::String& name)
{
    auto& t = getTrack (trackIndex);
    if (processor != nullptr)
        prepareInstrument (*processor);

    const int channels = processor != nullptr
                             ? juce::jmax (2, processor->getTotalNumInputChannels(), processor->getTotalNumOutputChannels())
                             : 2;
    {
        const juce::ScopedLock sl (lock);
        std::swap (t.instrument, processor);
        t.instrumentName = name;
        t.buffer.setSize (channels, blockSize, false, true, false);
    }

    if (processor != nullptr)
        processor->releaseResources(); // old instrument, destroyed when this function returns
}

void AudioEngine::loadPluginInstrument (int trackIndex, const juce::PluginDescription& desc, std::function<void (juce::String)> done)
{
    formatManager.createPluginInstanceAsync (desc, sampleRate, blockSize,
                                             [this, trackIndex, desc, done] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error) {
                                                 if (instance == nullptr)
                                                 {
                                                     done (error.isNotEmpty() ? error : "Could not load " + desc.name);
                                                     return;
                                                 }
                                                 setInstrument (trackIndex, std::move (instance), desc.name);
                                                 done ({});
                                             });
}

//==============================================================================
void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const juce::ScopedLock sl (lock);
    sampleRate = device->getCurrentSampleRate();
    blockSize = juce::jmax (16, device->getCurrentBufferSizeSamples());
    transport.prepare (sampleRate);
    midiCollector.reset (sampleRate);
    master.setSize (2, blockSize);

    for (auto& t : tracks)
    {
        int channels = 2;
        if (t->instrument != nullptr)
        {
            prepareInstrument (*t->instrument);
            channels = juce::jmax (2, t->instrument->getTotalNumInputChannels(), t->instrument->getTotalNumOutputChannels());
        }
        t->buffer.setSize (channels, blockSize);
    }
}

void AudioEngine::audioDeviceStopped()
{
    const juce::ScopedLock sl (lock);
    for (auto& t : tracks)
        if (t->instrument != nullptr)
            t->instrument->releaseResources();
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs, int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    if (numSamples > blockSize || numSamples > master.getNumSamples())
        return; // device changed its buffer size without a restart; skip until it is prepared again

    liveMidi.clear();
    midiCollector.removeNextBlockOfMessages (liveMidi, numSamples);
    keyboardState.processNextMidiBuffer (liveMidi, 0, numSamples, true);

    const juce::ScopedLock sl (lock);

    const bool killNotes = stopRequested.exchange (false);
    if (killNotes)
    {
        for (auto& t : tracks)
            t->player.stopImmediately();
        transport.stop();
    }
    if (! juce::exactlyEqual (transport.getTempo(), tempo.load()))
        transport.setTempo (tempo.load());
    if (playing.load() && ! transport.isPlaying())
        transport.play();

    const auto block = transport.advance (numSamples);
    const double spb = transport.samplesPerBeat();

    master.clear();
    bool anySolo = false;
    for (auto& t : tracks)
        anySolo = anySolo || t->solo.load();

    for (size_t ti = 0; ti < tracks.size(); ++ti)
    {
        auto& t = *tracks[ti];

        t.events.clear();
        t.player.process (block.startBeat, block.endBeat, spb, numSamples, t.events);
        daw::engine::sortEvents (t.events);

        playingScene[ti] = t.player.playingSlot();
        queuedScene[ti] = t.player.queuedSlot();

        t.midi.clear();
        if (killNotes)
            for (int ch = 1; ch <= 16; ++ch)
                t.midi.addEvent (juce::MidiMessage::allNotesOff (ch), 0);
        for (const auto& e : t.events)
            t.midi.addEvent (juce::MidiMessage (e.status, e.data1, e.data2), e.sampleOffset);
        if (t.arm.load())
            t.midi.addEvents (liveMidi, 0, numSamples, 0);

        if (t.instrument == nullptr)
            continue;

        const int channels = t.buffer.getNumChannels();
        juce::AudioBuffer<float> view (t.buffer.getArrayOfWritePointers(), channels, numSamples);
        view.clear();
        t.instrument->processBlock (view, t.midi);

        const bool audible = ! t.mute.load() && (! anySolo || t.solo.load());
        const float vol = t.volumeDb.load() <= -60.0f ? 0.0f : juce::Decibels::decibelsToGain (t.volumeDb.load());
        const float pan = juce::jlimit (-1.0f, 1.0f, t.pan.load());
        const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const float gainL = audible ? vol * std::cos (angle) * juce::MathConstants<float>::sqrt2 : 0.0f;
        const float gainR = audible ? vol * std::sin (angle) * juce::MathConstants<float>::sqrt2 : 0.0f;

        const float* srcL = view.getReadPointer (0);
        const float* srcR = view.getReadPointer (juce::jmin (1, channels - 1));
        master.addFromWithRamp (0, 0, srcL, numSamples, t.lastGainL, gainL);
        master.addFromWithRamp (1, 0, srcR, numSamples, t.lastGainR, gainR);
        t.lastGainL = gainL;
        t.lastGainR = gainR;

        const auto range = [&] (const float* d) {
            auto r = juce::FloatVectorOperations::findMinAndMax (d, numSamples);
            return juce::jmax (std::abs (r.getStart()), std::abs (r.getEnd()));
        };
        t.meterL = juce::jmax (t.meterL.load(), range (srcL) * gainL);
        t.meterR = juce::jmax (t.meterR.load(), range (srcR) * gainR);
    }

    const float mg = juce::Decibels::decibelsToGain (masterVolumeDb.load(), -60.0f);
    master.applyGain (mg);
    masterMeterL = juce::jmax (masterMeterL.load(), master.getMagnitude (0, 0, numSamples));
    masterMeterR = juce::jmax (masterMeterR.load(), master.getMagnitude (1, 0, numSamples));

    for (int ch = 0; ch < juce::jmin (2, numOutputs); ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::copy (outputs[ch], master.getReadPointer (ch), numSamples);

    positionBeats = transport.getPositionBeats();
}
