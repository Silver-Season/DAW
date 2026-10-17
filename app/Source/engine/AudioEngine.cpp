#include "AudioEngine.h"

#include "RtCheck.h"

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
        const double bpm = engine.tempo.load();
        info.setBpm (bpm);
        info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        info.setPpqPosition (beats);
        info.setPpqPositionOfLastBarStart (std::floor (beats / 4.0) * 4.0);
        info.setTimeInSeconds (beats * 60.0 / bpm);
        info.setTimeInSamples ((int64_t) (beats * 60.0 / bpm * engine.sampleRate.load()));
        info.setIsPlaying (engine.playing.load());
        return info;
    }

private:
    AudioEngine& engine;
};

//==============================================================================
namespace
{
double defaultClock() { return juce::Time::getMillisecondCounterHiRes(); }
} // namespace

AudioEngine::AudioEngine (Settings& s, bool openDevices) : settings (s), clock (&defaultClock)
{
    formatManager.addDefaultFormats();

    if (auto xml = settings.knownPlugins())
        knownPlugins.recreateFromXml (*xml);
    knownPlugins.addChangeListener (this);

    playHead = std::make_unique<PlayHead> (*this);

    master.setSize (kMaxChannels, blockSize);
    hardwareMidi.ensureSize (4096);
    keyboardMidi.ensureSize (4096);

    if (openDevices)
    {
        auto savedDevice = settings.audioDeviceState();
        const auto error = deviceManager.initialise (0, 2, savedDevice.get(), true);
        if (error.isNotEmpty())
        {
            DBG ("Audio device error: " << error);
        }
        deviceManager.addChangeListener (this);

        devicesOpened = true;
        updateMidiInputs();
        deviceManager.addMidiInputDeviceCallback ({}, this);
        midiListConnection = juce::MidiDeviceListConnection::make ([this] { updateMidiInputs(); }); // plugged in or out
        deviceManager.addAudioCallback (this);
    }
    keyboardState.addListener (this);
    startTimerHz (60);
}

AudioEngine::~AudioEngine()
{
    alive->store (false);
    midiListConnection = {};
    stopTimer();
    deviceManager.removeAudioCallback (this);
    deviceManager.removeMidiInputDeviceCallback ({}, this);
    keyboardState.removeListener (this);
    deviceManager.closeAudioDevice();
    knownPlugins.removeChangeListener (this);
    deviceManager.removeChangeListener (this);
    setModel (nullptr);
}

void AudioEngine::updateMidiInputs()
{
    // Every connected MIDI keyboard/controller plays the armed track, unless the user switched it off.
    for (const auto& input : juce::MidiInput::getAvailableDevices())
    {
        const bool wanted = settings.midiInputPlays (input.identifier);
        if (deviceManager.isMidiInputDeviceEnabled (input.identifier) != wanted)
            deviceManager.setMidiInputDeviceEnabled (input.identifier, wanted);
    }
}

void AudioEngine::refreshMidiInputs()
{
    if (devicesOpened)
        updateMidiInputs();
}

void AudioEngine::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &knownPlugins)
    {
        if (auto xml = knownPlugins.createXml())
            settings.setKnownPlugins (*xml);
    }
    else if (source == &deviceManager)
    {
        if (auto xml = deviceManager.createStateXml())
            settings.setAudioDeviceState (*xml);
    }
    settings.save();
}

void AudioEngine::timerCallback()
{
    rebuildGraphIfNeeded();
    collectGarbage();

    // Live key events are forwarded to the audio thread by listener callbacks; this only empties the
    // keyboard state's own event list, which nobody else reads.
    juce::MidiBuffer scratch;
    keyboardState.processNextMidiBuffer (scratch, 0, 1, false);
}

//==============================================================================
// Live MIDI input: pushed lock-free, from any thread, to the audio thread.
bool AudioEngine::pushLiveMidi (const uint8_t* bytes, int size, bool fromKeyboard, double timeMs)
{
    if (size < 1 || size > 3)
        return false; // sysex and the like are not routed to tracks
    MidiInputQueue::Event e;
    for (int i = 0; i < size; ++i)
        e.bytes[i] = bytes[i];
    e.size = (uint8_t) size;
    e.source = fromKeyboard ? MidiInputQueue::keyboard : MidiInputQueue::hardware;
    if (! fromKeyboard)
        hardwareMidiCount.fetch_add (1, std::memory_order_relaxed);
    e.timeMs = timeMs;
    return midiQueue.push (e);
}

void AudioEngine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    // JUCE stamps device messages in seconds on the same clock as getMillisecondCounterHiRes
    const double stamp = m.getTimeStamp() * 1000.0;
    pushLiveMidi (m.getRawData(), m.getRawDataSize(), false, stamp > 0.0 ? stamp : juce::Time::getMillisecondCounterHiRes());
}

void AudioEngine::handleNoteOn (juce::MidiKeyboardState*, int channel, int note, float velocity)
{
    const uint8_t bytes[3] { (uint8_t) (0x90 | juce::jlimit (0, 15, channel - 1)), (uint8_t) juce::jlimit (0, 127, note),
                             (uint8_t) juce::jlimit (1, 127, juce::roundToInt (velocity * 127.0f)) };
    pushLiveMidi (bytes, 3, true, juce::Time::getMillisecondCounterHiRes());
}

void AudioEngine::handleNoteOff (juce::MidiKeyboardState*, int channel, int note, float)
{
    const uint8_t bytes[3] { (uint8_t) (0x80 | juce::jlimit (0, 15, channel - 1)), (uint8_t) juce::jlimit (0, 127, note), 0 };
    pushLiveMidi (bytes, 3, true, juce::Time::getMillisecondCounterHiRes());
}

//==============================================================================
void AudioEngine::play()
{
    // Like Live: Play brings back the Session clips that were playing when the transport was stopped.
    if (! pushCommand ({ EngineCommand::Type::resume, -1, -1 }))
        playing = true;
}

void AudioEngine::stop()
{
    playing = false;
    stopRequested = true;
}

bool AudioEngine::pushCommand (const EngineCommand& c)
{
    const auto scope = commandFifo.write (1);
    if (scope.blockSize1 + scope.blockSize2 < 1)
        return false;
    commands[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)] = c;
    return true;
}

bool AudioEngine::launchClip (int64_t trackId, int scene)
{
    return pushCommand ({ EngineCommand::Type::launchClip, trackId, scene });
}

bool AudioEngine::launchScene (int scene)
{
    return pushCommand ({ EngineCommand::Type::launchScene, -1, scene });
}

bool AudioEngine::stopTrack (int64_t trackId)
{
    return pushCommand ({ EngineCommand::Type::stopTrack, trackId, -1 });
}

bool AudioEngine::stopAllClips()
{
    return pushCommand ({ EngineCommand::Type::stopAll, -1, -1 });
}

void AudioEngine::previewFactoryPreset (int program)
{
    preview.playFactoryProgram (program);
}

void AudioEngine::previewPresetState (const juce::MemoryBlock& state)
{
    preview.playState (state.getData(), (int) state.getSize());
}

void AudioEngine::stopPreview()
{
    preview.stop();
}

void AudioEngine::playTestTone()
{
    toneSamplesLeft.store (juce::roundToInt (sampleRate.load()));
}

void AudioEngine::prepareDevice (DeviceInstance& d) const
{
    auto& p = *d.processor;
    p.setPlayHead (playHead.get());
    p.enableAllBuses();
    p.setRateAndBufferSizeDetails (sampleRate.load(), blockSize);
    p.prepareToPlay (sampleRate.load(), blockSize);
    d.numChannels = juce::jlimit (2, kMaxChannels, juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()));
}

//==============================================================================
void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    std::lock_guard<std::recursive_mutex> sl (buildLock);
    sampleRate = device->getCurrentSampleRate();
    blockSize = juce::jmax (16, device->getCurrentBufferSizeSamples());
    transport.prepare (sampleRate);
    master.setSize (kMaxChannels, blockSize);
    preview.prepare (sampleRate, blockSize);

    for (auto& [id, rt] : runtimes)
        rt->prepare (blockSize);
    for (auto& r : retiredTracks)
        r.object->prepare (blockSize);
    for (auto& [id, d] : devices)
        prepareDevice (*d);
    for (auto& r : retiredDevices)
        prepareDevice (*r.object);

    audioRunning = true;
}

void AudioEngine::audioDeviceStopped()
{
    std::lock_guard<std::recursive_mutex> sl (buildLock);
    audioRunning = false;
    preview.release();
    for (auto& [id, d] : devices)
        d->processor->releaseResources();
    collectGarbage(); // nobody can see the old graphs any more
}

//==============================================================================
// Audio thread
//==============================================================================
void AudioEngine::reconcileWithGraph (const PlaybackGraph& graph)
{
    // A new graph arrived. Clips that were edited are new objects, and scenes may have moved, so the
    // players are re-pointed by clip id: a playing clip keeps playing (nothing restarts, no note is lost).
    // The old clips are still alive here: seenGeneration is only advanced after this function.
    for (const auto& t : graph.tracks)
    {
        auto& player = t.runtime->player;
        const auto findById = [&t] (const daw::engine::MidiClip* old, int fallbackSlot, const daw::engine::MidiClip*& clipOut) {
            int slot = fallbackSlot;
            clipOut = nullptr;
            if (old != nullptr)
                for (int i = 0; i < (int) t.slots.size(); ++i)
                    if (t.slots[(size_t) i] != nullptr && t.slots[(size_t) i]->id == old->id)
                    {
                        slot = i;
                        clipOut = t.slots[(size_t) i].get();
                        break;
                    }
            return slot; // clip gone: the old slot index with a null clip stops the player
        };

        const daw::engine::MidiClip *newPlayingClip = nullptr, *newQueuedClip = nullptr;
        const int oldPlaying = player.playingSlot(), oldQueued = player.queuedSlot();
        const int newPlaying = oldPlaying == daw::engine::ClipPlayer::kNoClip
                                   ? oldPlaying
                                   : findById (player.playingClipPtr(), oldPlaying, newPlayingClip);
        const int newQueued = oldQueued == daw::engine::ClipPlayer::kNoClip
                                  ? oldQueued
                                  : findById (player.queuedClipPtr(), oldQueued, newQueuedClip);

        player.relocate (newPlaying, newQueued);
        if (newPlaying != daw::engine::ClipPlayer::kNoClip && newPlayingClip != player.playingClipPtr())
            player.clipChanged (newPlaying, newPlayingClip);
        if (newQueued != daw::engine::ClipPlayer::kNoClip && newQueuedClip != player.queuedClipPtr())
            player.clipChanged (newQueued, newQueuedClip);
    }
}

void AudioEngine::processCommands (const PlaybackGraph& graph)
{
    const auto findTrack = [&graph] (int64_t id) -> const PlaybackGraph::Track* {
        for (const auto& t : graph.tracks)
            if (t.runtime->trackId == id)
                return &t;
        return nullptr;
    };
    const auto slotClip = [] (const PlaybackGraph::Track& t, int scene) -> const daw::engine::MidiClip* {
        return scene >= 0 && scene < (int) t.slots.size() ? t.slots[(size_t) scene].get() : nullptr;
    };

    const double now = transport.getPositionBeats();
    const double q = playing.load() ? quantise.load() : 0.0;

    const auto scope = commandFifo.read (commandFifo.getNumReady());
    const auto handle = [&] (const EngineCommand& c) {
        switch (c.type)
        {
            case EngineCommand::Type::launchClip:
                if (auto* t = findTrack (c.trackId))
                    if (auto* clip = slotClip (*t, c.sceneIndex))
                    {
                        t->runtime->player.launch (c.sceneIndex, clip, now, q);
                        playing = true; // launching a clip starts the transport, like in Live
                    }
                break;
            case EngineCommand::Type::launchScene:
                for (const auto& t : graph.tracks)
                {
                    if (auto* clip = slotClip (t, c.sceneIndex))
                    {
                        t.runtime->player.launch (c.sceneIndex, clip, now, q);
                        playing = true;
                    }
                    else
                        t.runtime->player.stop (now, q);
                }
                break;
            case EngineCommand::Type::resume:
                for (const auto& t : graph.tracks)
                    if (t.runtime->resumeSlot >= 0 && t.runtime->player.playingSlot() < 0)
                        if (auto* clip = slotClip (t, t.runtime->resumeSlot))
                            t.runtime->player.launch (t.runtime->resumeSlot, clip, now, 0.0);
                playing = true;
                break;
            case EngineCommand::Type::stopTrack:
                if (auto* t = findTrack (c.trackId))
                    t->runtime->player.stop (now, q);
                break;
            case EngineCommand::Type::stopAll:
                for (const auto& t : graph.tracks)
                    t.runtime->player.stop (now, q);
                break;
        }
    };
    for (int i = 0; i < scope.blockSize1; ++i)
        handle (commands[(size_t) (scope.startIndex1 + i)]);
    for (int i = 0; i < scope.blockSize2; ++i)
        handle (commands[(size_t) (scope.startIndex2 + i)]);
}

namespace
{
// Runs a device chain in place on `view`. The first device is the instrument and gets the MIDI; the others
// are effects and get an empty MIDI buffer. A disabled effect lets the sound through; a disabled
// instrument is silent.
void processChain (const std::vector<DeviceInstance*>& chain, juce::AudioBuffer<float>& storage, int numSamples,
                   juce::MidiBuffer& midi, juce::MidiBuffer& emptyMidi, bool firstIsInstrument)
{
    for (size_t i = 0; i < chain.size(); ++i)
    {
        auto* d = chain[i];
        const bool isInstrument = firstIsInstrument && i == 0;
        if (d == nullptr || d->processor == nullptr)
            continue;

        const int channels = d->numChannels.load (std::memory_order_relaxed);
        juce::AudioBuffer<float> view (storage.getArrayOfWritePointers(), channels, numSamples);
        if (! d->enabled.load (std::memory_order_relaxed))
        {
            if (isInstrument)
                view.clear();
            continue;
        }
        if (isInstrument)
        {
            view.clear();
            d->processor->processBlock (view, midi);
        }
        else
        {
            for (int ch = 2; ch < channels; ++ch) // only the main stereo bus carries signal
                view.clear (ch, 0, numSamples);
            emptyMidi.clear();
            d->processor->processBlock (view, emptyMidi);
        }
    }
}
} // namespace

void AudioEngine::renderTrack (const PlaybackGraph::Track& t, const daw::engine::Transport::Block& block, double spb, int numSamples,
                               bool killNotes, bool anySolo, bool anyArmed)
{
    auto& rt = *t.runtime;

    rt.events.clear();
    rt.player.process (block.startBeat, block.endBeat, spb, numSamples, rt.events);
    daw::engine::sortEvents (rt.events);

    rt.playingSlot.store (rt.player.playingSlot(), std::memory_order_relaxed);
    rt.queuedSlot.store (rt.player.queuedSlot(), std::memory_order_relaxed);
    rt.playingStartBeat.store (rt.player.playingStartBeat(), std::memory_order_relaxed);

    rt.midi.clear();
    if (killNotes)
        for (int ch = 1; ch <= 16; ++ch)
            rt.midi.addEvent (juce::MidiMessage::allNotesOff (ch), 0);
    for (const auto& e : rt.events)
    {
        const uint8_t bytes[3] { e.status, e.data1, e.data2 };
        rt.midi.addEvent (bytes, 3, e.sampleOffset);
    }

    // Live MIDI goes to armed tracks; with no armed track the selected one is armed automatically.
    const bool armed = rt.arm.load() || (! anyArmed && rt.trackId == selectedTrackId.load());
    if (armed)
        switch ((TrackInputMode) rt.inputMode.load())
        {
            case TrackInputMode::allMidi:
                rt.midi.addEvents (hardwareMidi, 0, numSamples, 0);
                rt.midi.addEvents (keyboardMidi, 0, numSamples, 0);
                break;
            case TrackInputMode::keyboard: rt.midi.addEvents (keyboardMidi, 0, numSamples, 0); break;
            case TrackInputMode::midiDevice: rt.midi.addEvents (hardwareMidi, 0, numSamples, 0); break;
            case TrackInputMode::none: break;
        }

    if (t.chain.empty())
        return;

    // Start from silence so that a missing or disabled instrument cannot feed old audio into the effects.
    rt.buffer.clear (0, 0, numSamples);
    rt.buffer.clear (1, 0, numSamples);
    processChain (t.chain, rt.buffer, numSamples, rt.midi, rt.fxMidi, true);

    const bool audible = ! rt.mute.load() && (! anySolo || rt.solo.load());
    const float vol = audible ? rt.volumeGain.load() : 0.0f;
    const float pan = juce::jlimit (-1.0f, 1.0f, rt.pan.load());
    const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f; // equal-power pan law
    const float gainL = vol * std::cos (angle) * juce::MathConstants<float>::sqrt2;
    const float gainR = vol * std::sin (angle) * juce::MathConstants<float>::sqrt2;

    const float* srcL = rt.buffer.getReadPointer (0);
    const float* srcR = rt.buffer.getReadPointer (1);
    master.addFromWithRamp (0, 0, srcL, numSamples, rt.lastGainL, gainL);
    master.addFromWithRamp (1, 0, srcR, numSamples, rt.lastGainR, gainR);
    rt.lastGainL = gainL;
    rt.lastGainR = gainR;

    const auto peak = [numSamples] (const float* d) {
        const auto r = juce::FloatVectorOperations::findMinAndMax (d, numSamples);
        return juce::jmax (std::abs (r.getStart()), std::abs (r.getEnd()));
    };
    rt.meterL.store (juce::jmax (rt.meterL.load(), peak (srcL) * gainL), std::memory_order_relaxed);
    rt.meterR.store (juce::jmax (rt.meterR.load(), peak (srcR) * gainR), std::memory_order_relaxed);
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs, int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    const daw::rt::ScopedAudioThread rtGuard;
    juce::ScopedNoDenormals noDenormals;

    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    const PlaybackGraph* graph = current.load (std::memory_order_acquire);
    if (graph == nullptr)
        return;

    if (numSamples > blockSize || numSamples > master.getNumSamples())
    {
        seenGeneration.store (graph->generation, std::memory_order_release);
        return; // device changed its buffer size without a restart; skip until it is prepared again
    }

    if (graph->generation != lastGeneration)
    {
        reconcileWithGraph (*graph);
        lastGeneration = graph->generation;
    }
    // Only now may the message thread delete what older graphs held.
    seenGeneration.store (graph->generation, std::memory_order_release);

    processCommands (*graph);

    // Live MIDI
    hardwareMidi.clear();
    keyboardMidi.clear();
    // Like MidiMessageCollector: events that happened since the previous callback are spread over this
    // block by their time; future events go to the end of the block, too old ones to the start.
    const double nowMs = clock.load (std::memory_order_relaxed)();
    const double rate = sampleRate.load();
    const double blockMs = numSamples * 1000.0 / rate;
    double refMs = lastCallbackMs < 0.0 ? nowMs - blockMs : lastCallbackMs;
    if (nowMs - refMs > 2.0 * blockMs || nowMs < refMs)
        refMs = nowMs - blockMs; // after a pause the old reference would push everything to the end
    lastCallbackMs = nowMs;
    MidiInputQueue::Event e;
    while (midiQueue.pop (e))
    {
        const int offset = juce::jlimit (0, numSamples - 1, (int) std::lround ((e.timeMs - refMs) * rate / 1000.0));
        (e.source == MidiInputQueue::keyboard ? keyboardMidi : hardwareMidi).addEvent (e.bytes, e.size, offset);
    }

    const bool killNotes = stopRequested.exchange (false);
    if (killNotes)
    {
        for (const auto& t : graph->tracks)
        {
            t.runtime->resumeSlot = t.runtime->player.playingSlot();
            t.runtime->player.stopImmediately();
        }
        transport.stop();
    }
    if (! juce::exactlyEqual (transport.getTempo(), tempo.load()))
        transport.setTempo (tempo.load());
    if (playing.load() && ! transport.isPlaying())
        transport.play();

    const auto block = transport.advance (numSamples);
    const double spb = transport.samplesPerBeat();

    master.clear();
    bool anySolo = false, anyArmed = false;
    for (const auto& t : graph->tracks)
    {
        anySolo = anySolo || t.runtime->solo.load();
        anyArmed = anyArmed || t.runtime->arm.load();
    }

    for (const auto& t : graph->tracks)
        renderTrack (t, block, spb, numSamples, killNotes, anySolo, anyArmed);

    // Master chain works on the first two channels of the (wider) master buffer
    processChain (graph->masterChain, master, numSamples, masterMidi, masterFxMidi, false);

    master.applyGain (masterGain.load());
    masterMeterL.store (juce::jmax (masterMeterL.load(), master.getMagnitude (0, 0, numSamples)), std::memory_order_relaxed);
    masterMeterR.store (juce::jmax (masterMeterR.load(), master.getMagnitude (1, 0, numSamples)), std::memory_order_relaxed);

    for (int ch = 0; ch < juce::jmin (2, numOutputs); ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::copy (outputs[ch], master.getReadPointer (ch), numSamples);

    if (numOutputs >= 2 && outputs[0] != nullptr && outputs[1] != nullptr)
    {
        juce::AudioBuffer<float> outView (const_cast<float**> (outputs), 2, numSamples);
        preview.process (outView, numSamples, tempo.load());

        if (const int left = toneSamplesLeft.load (std::memory_order_relaxed); left > 0)
        {
            if (toneSamplesTotal == 0 || left > toneSamplesTotal)
            {
                toneSamplesTotal = left;
                tonePhase = 0.0;
            }
            const double inc = 2.0 * juce::MathConstants<double>::pi * 440.0 / rate;
            const int fade = juce::jmax (1, (int) (0.005 * rate));
            const int n = juce::jmin (numSamples, left);
            for (int i = 0; i < n; ++i)
            {
                const int pos = toneSamplesTotal - (left - i); // samples played so far
                const int toEnd = left - i;
                const float env = juce::jmin (1.0f, (float) pos / (float) fade, (float) toEnd / (float) fade);
                const float s = 0.1f * env * (float) std::sin (tonePhase); // 0.1 = -20 dBFS
                tonePhase += inc;
                outputs[0][i] += s;
                outputs[1][i] += s;
            }
            toneSamplesLeft.store (left - n, std::memory_order_relaxed);
            if (left - n == 0)
                toneSamplesTotal = 0;
        }
    }

    positionBeats = transport.getPositionBeats();
}
