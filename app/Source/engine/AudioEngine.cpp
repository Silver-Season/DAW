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
        const double beats = engine.songPositionBeats.load();
        const double bpm = engine.tempo.load();
        info.setBpm (bpm);
        info.setTimeSignature (juce::AudioPlayHead::TimeSignature { engine.sigNum.load(), engine.sigDen.load() });
        info.setPpqPosition (beats);
        const double bar = juce::jmax (1.0, engine.beatsPerBar.load());
        info.setPpqPositionOfLastBarStart (std::floor (beats / bar) * bar);
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
    correctionMs = settings.recordCorrectionMs();

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

void AudioEngine::playFromPosition()
{
    if (! pushCommand ({ EngineCommand::Type::resumeContinue, -1, -1 }))
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

bool AudioEngine::seek (double songBeat)
{
    return pushCommand ({ EngineCommand::Type::seek, -1, -1, juce::jmax (0.0, songBeat) });
}

bool AudioEngine::seekQuantised (double songBeat)
{
    return pushCommand ({ EngineCommand::Type::seekQuantised, -1, -1, juce::jmax (0.0, songBeat) });
}

bool AudioEngine::rewindSong()
{
    return pushCommand ({ EngineCommand::Type::rewind, -1, -1 });
}

bool AudioEngine::backToArrangement()
{
    return pushCommand ({ EngineCommand::Type::backToArrangement, -1, -1 });
}

bool AudioEngine::startRecord (int64_t trackId, int scene)
{
    return pushCommand ({ EngineCommand::Type::startRecord, trackId, scene, 0.0 });
}

bool AudioEngine::stopRecord (int64_t trackId)
{
    return pushCommand ({ EngineCommand::Type::stopRecord, trackId, -1, 0.0 });
}

bool AudioEngine::launchAligned (int64_t trackId, int scene, double beat)
{
    return pushCommand ({ EngineCommand::Type::launchAligned, trackId, scene, beat });
}

bool AudioEngine::startArrangementRecord()
{
    return pushCommand ({ EngineCommand::Type::startArrangementRecord, -1, -1, 0.0 });
}

bool AudioEngine::stopArrangementRecord()
{
    return pushCommand ({ EngineCommand::Type::stopArrangementRecord, -1, -1, 0.0 });
}

size_t AudioEngine::popEngineEvents (std::vector<EngineEvent>& out)
{
    const auto scope = engineEventFifo.read (engineEventFifo.getNumReady());
    out.insert (out.end(), engineEvents.begin() + scope.startIndex1, engineEvents.begin() + scope.startIndex1 + scope.blockSize1);
    out.insert (out.end(), engineEvents.begin() + scope.startIndex2, engineEvents.begin() + scope.startIndex2 + scope.blockSize2);
    return (size_t) (scope.blockSize1 + scope.blockSize2);
}

size_t AudioEngine::popInputEvents (std::vector<daw::engine::InputEvent>& out)
{
    const auto scope = inputFifo.read (inputFifo.getNumReady());
    out.insert (out.end(), inputEvents.begin() + scope.startIndex1, inputEvents.begin() + scope.startIndex1 + scope.blockSize1);
    out.insert (out.end(), inputEvents.begin() + scope.startIndex2, inputEvents.begin() + scope.startIndex2 + scope.blockSize2);
    return (size_t) (scope.blockSize1 + scope.blockSize2);
}

void AudioEngine::setRecordCorrectionMs (double ms)
{
    correctionMs = juce::jlimit (-50.0, 50.0, ms);
    settings.setRecordCorrectionMs (correctionMs.load());
    updateRecordCompensation();
}

void AudioEngine::updateRecordCompensation()
{
    recordCompensation = autoCompensation.load() + juce::roundToInt (correctionMs.load() * sampleRate.load() / 1000.0);
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
    metronome.prepare (sampleRate);
    autoCompensation = device->getOutputLatencyInSamples() + blockSize / 2;
    updateRecordCompensation();
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

        // The arrangement of the track changed: what it holds sounds from the new clips, chased from the current position.
        if (t.arrangementSignature != t.runtime->arrangementSignature)
        {
            t.runtime->arrangementSignature = t.arrangementSignature;
            t.runtime->arrangement.invalidate();
        }

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

    // During a count-in the position is negative, but nothing may start before beat 0.
    const double now = std::max (0.0, transport.getPositionBeats());
    const double q = playing.load() ? quantise.load() : 0.0;
    const double bpb = beatsPerBar.load();

    // A launch ends the recording of the track it launches on (at the bar line of the launch, counted from the
    // start of the recording); an overdub simply ends there.
    const auto recordingEndsWithLaunch = [&] (TrackRuntime& rt) {
        const int state = rt.recordState.load();
        if (state == TrackRuntime::recordWaiting)
        {
            rt.recordState = TrackRuntime::recordIdle;
            return;
        }
        if (state != TrackRuntime::recordRunning)
            return;
        const double at = daw::engine::nextQuantisedBeat (now, q);
        const double start = rt.recordStartBeat.load();
        const double end = rt.recordOverdub.load() ? at : start + bpb * std::max (1.0, std::ceil ((at - start) / bpb - 1.0e-9));
        if (end < rt.recordEndBeat.load())
            rt.recordEndBeat = end;
    };
    const auto launch = [&] (const PlaybackGraph::Track& t, int scene, const daw::engine::MidiClip* clip) {
        recordingEndsWithLaunch (*t.runtime);
        t.runtime->player.launch (scene, clip, now, q);
        playing = true; // launching a clip starts the transport, like in Live
    };

    const auto startRecording = [&] (const PlaybackGraph::Track& t, int scene) {
        auto& rt = *t.runtime;
        if (rt.recordState.load() != TrackRuntime::recordIdle)
            return;
        const bool running = playing.load() || transport.isPlaying();
        const auto* playingNow = rt.player.playingSlot() >= 0 ? slotClip (t, rt.player.playingSlot()) : nullptr;
        const bool overdub = running && overdubOn.load() && playingNow != nullptr && rt.player.playingSlot() == scene;

        double start = now;
        if (! running)
        {
            // Count-in: the transport begins before beat 0 and the recording at beat 0.
            const int bars = countInBars.load();
            transport.startAt (bars > 0 ? -(double) bars * bpb : 0.0);
            playing = true;
            start = 0.0;
        }
        else if (! overdub)
            start = daw::engine::nextQuantisedBeat (now, quantise.load());

        rt.recordScene = scene;
        rt.recordStartBeat = start;
        rt.recordOverdub = overdub;
        if (overdub)
        {
            rt.recordEndBeat = TrackRuntime::kNoEnd;
            rt.recordState = TrackRuntime::recordRunning;
            return;
        }
        // The clip that plays on the track gives way at the start beat.
        rt.player.stop (start, 0.0);
        const int bars = recordFixedBars.load();
        rt.recordEndBeat = bars > 0 ? start + (double) bars * bpb : TrackRuntime::kNoEnd;
        rt.recordState = TrackRuntime::recordWaiting;
    };

    const auto stopRecording = [&] (const PlaybackGraph::Track& t) {
        auto& rt = *t.runtime;
        const int state = rt.recordState.load();
        if (state == TrackRuntime::recordWaiting)
            rt.recordState = TrackRuntime::recordIdle; // never began: nothing to keep
        else if (state == TrackRuntime::recordRunning)
        {
            const double start = rt.recordStartBeat.load();
            const double end = rt.recordOverdub.load() ? now : start + bpb * std::max (1.0, std::ceil ((now - start) / bpb - 1.0e-9));
            if (end < rt.recordEndBeat.load())
                rt.recordEndBeat = end;
        }
    };

    // Returns false if the clip is not in the graph (yet).
    const auto launchAt = [&] (const EngineCommand& c) {
        if (auto* t = findTrack (c.trackId))
            if (auto* clip = slotClip (*t, c.sceneIndex))
            {
                t->runtime->player.launchAt (c.sceneIndex, clip, c.beat);
                return true;
            }
        return false;
    };

    // Aligned launches that waited for their clip
    for (int i = 0; i < numDeferred;)
    {
        if (launchAt (deferred[(size_t) i].command) || ++deferred[(size_t) i].age > 400)
            deferred[(size_t) i] = deferred[(size_t) --numDeferred];
        else
            ++i;
    }

    const auto scope = commandFifo.read (commandFifo.getNumReady());
    const auto handle = [&] (const EngineCommand& c) {
        switch (c.type)
        {
            case EngineCommand::Type::launchClip:
                if (auto* t = findTrack (c.trackId))
                    if (auto* clip = slotClip (*t, c.sceneIndex))
                        launch (*t, c.sceneIndex, clip);
                break;
            case EngineCommand::Type::launchScene:
                for (const auto& t : graph.tracks)
                {
                    if (auto* clip = slotClip (t, c.sceneIndex))
                        launch (t, c.sceneIndex, clip);
                    else
                        t.runtime->player.stop (now, q);
                }
                break;
            case EngineCommand::Type::resume:
            case EngineCommand::Type::resumeContinue:
                if (c.type == EngineCommand::Type::resumeContinue && ! transport.isPlaying())
                    continueSong = true;
                for (const auto& t : graph.tracks)
                    if (t.runtime->resumeSlot >= 0 && t.runtime->player.playingSlot() < 0)
                        if (auto* clip = slotClip (t, t.runtime->resumeSlot))
                            t.runtime->player.launch (t.runtime->resumeSlot, clip, now, 0.0);
                playing = true;
                break;
            case EngineCommand::Type::stopTrack:
                if (auto* t = findTrack (c.trackId))
                {
                    t->runtime->player.stop (now, q);
                    t->runtime->inSession = true; // like Live: the stopped track stays silent until "Back to arrangement"
                    t->runtime->backPending = false;
                }
                break;
            case EngineCommand::Type::stopAll:
                for (const auto& t : graph.tracks)
                {
                    t.runtime->player.stop (now, q);
                    t.runtime->inSession = true;
                    t.runtime->backPending = false;
                }
                break;
            case EngineCommand::Type::seek:
                transport.seekSong (c.beat);
                break;
            case EngineCommand::Type::seekQuantised:
                if (transport.isPlaying() && playing.load())
                    transport.scheduleSeek (daw::engine::nextQuantisedBeat (now, q), c.beat);
                else
                    transport.seekSong (c.beat);
                break;
            case EngineCommand::Type::rewind:
                transport.seekSong (0.0);
                break;
            case EngineCommand::Type::backToArrangement:
                for (const auto& t : graph.tracks)
                {
                    auto& rt = *t.runtime;
                    if (! rt.inSession.load() && rt.player.playingSlot() == daw::engine::ClipPlayer::kNoClip)
                        continue; // already plays its arrangement
                    rt.player.stop (now, q);
                    rt.backPending = true;
                    rt.backBeat = daw::engine::nextQuantisedBeat (now, q);
                }
                break;
            case EngineCommand::Type::startRecord:
                if (auto* t = findTrack (c.trackId))
                    startRecording (*t, c.sceneIndex);
                break;
            case EngineCommand::Type::stopRecord:
                if (auto* t = findTrack (c.trackId))
                    stopRecording (*t);
                break;
            case EngineCommand::Type::launchAligned:
                if (! launchAt (c) && numDeferred < (int) deferred.size())
                    deferred[(size_t) numDeferred++] = { c, 0 };
                break;
            case EngineCommand::Type::startArrangementRecord:
                startArrangementRecording (graph);
                break;
            case EngineCommand::Type::stopArrangementRecord:
                if (const int state = arrRecState.load(); state == arrangementWaiting)
                    arrRecState = arrangementIdle; // never began: nothing to keep
                else if (state == arrangementRunning)
                    arrRecEnd = std::min (arrRecEnd.load(), now);
                break;
        }
    };
    for (int i = 0; i < scope.blockSize1; ++i)
        handle (commands[(size_t) (scope.startIndex1 + i)]);
    for (int i = 0; i < scope.blockSize2; ++i)
        handle (commands[(size_t) (scope.startIndex2 + i)]);
}

// Arms the recording onto the timeline. A stopped transport starts with the count-in and the recording begins at beat 0;
// a playing one begins at the next launch-quantisation boundary. Session clips that play at this moment are logged as
// launches, so that the recording also holds what was already running.
void AudioEngine::startArrangementRecording (const PlaybackGraph& graph)
{
    if (arrRecState.load() != arrangementIdle)
        return;
    const double now = std::max (0.0, transport.getPositionBeats());
    const double bpb = beatsPerBar.load();
    const bool running = playing.load() || transport.isPlaying();
    double start = now;
    if (! running)
    {
        const int bars = countInBars.load();
        transport.startAt (bars > 0 ? -(double) bars * bpb : 0.0);
        playing = true;
        start = 0.0;
    }
    else
        start = daw::engine::nextQuantisedBeat (now, quantise.load());

    arrRecStart = start;
    arrRecEnd = TrackRuntime::kNoEnd;
    arrRecState = arrangementWaiting;
    expectedSongBeat = std::numeric_limits<double>::quiet_NaN(); // the first window of the log is an anchor

    for (const auto& t : graph.tracks)
        if (t.runtime->player.playingSlot() >= 0)
        {
            EngineEvent e;
            e.type = EngineEvent::Type::launch;
            e.trackId = t.runtime->trackId;
            e.slot = t.runtime->player.playingSlot();
            e.monoBeat = now;
            e.beat = transport.getSongPositionBeats();
            e.clipStartMono = t.runtime->player.playingStartBeat();
            pushEngineEvent (e);
        }
}

void AudioEngine::pushEngineEvent (const EngineEvent& e)
{
    const auto scope = engineEventFifo.write (1);
    if (scope.blockSize1 + scope.blockSize2 < 1)
    {
        engineEventDrops.fetch_add (1, std::memory_order_relaxed);
        return;
    }
    engineEvents[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)] = e;
}

// Every discontinuity of the song position inside the block goes to the log, so that the message thread can turn a
// monotonic beat (the clock of the live events) into a song beat exactly, also across a loop jump.
void AudioEngine::logSongWindows (const daw::engine::Transport::Block& block, double spb)
{
    for (int i = 0; i < block.numWindows; ++i)
    {
        const auto& w = block.windows[i];
        if (std::isnan (expectedSongBeat) || std::abs (w.startBeat - expectedSongBeat) > 1.0e-6)
        {
            EngineEvent e;
            e.type = w.afterLoop ? EngineEvent::Type::loopJump : EngineEvent::Type::anchor;
            e.monoBeat = block.startBeat + (double) w.sampleOffset / spb;
            e.beat = w.startBeat;
            pushEngineEvent (e);
        }
        expectedSongBeat = w.endBeat;
    }
    if (block.numWindows == 0)
        expectedSongBeat = std::numeric_limits<double>::quiet_NaN();
}

// The state machine of the arrangement recording follows the transport past its start and end beats. The start is
// noticed before the tracks are rendered (the arrangement must already give way in the block where the recording
// begins); the end at the end of the block, so that everything the block logged is in the queue before the state says
// "finished".
void AudioEngine::updateArrangementRecord (double blockEndBeat)
{
    if (arrRecState.load() == arrangementRunning && arrRecEnd.load() <= blockEndBeat)
        arrRecState = arrangementFinished;
}

bool AudioEngine::isRecordTarget (const TrackRuntime& rt, bool anyArmed) const
{
    return acceptsLiveMidi (rt, true, anyArmed) || acceptsLiveMidi (rt, false, anyArmed);
}

// The transport was stopped: recordings that are running end where the stop caught them (a new recording rounded up
// to whole bars, at least one); recordings that had not begun are dropped.
void AudioEngine::finishRecordingsAtStop (const PlaybackGraph& graph)
{
    const double now = std::max (0.0, transport.getPositionBeats());
    const double bpb = beatsPerBar.load();
    if (const int state = arrRecState.load(); state == arrangementWaiting)
        arrRecState = arrangementIdle;
    else if (state == arrangementRunning)
    {
        arrRecEnd = std::min (arrRecEnd.load(), now);
        arrRecState = arrangementFinished;
    }
    for (const auto& t : graph.tracks)
    {
        auto& rt = *t.runtime;
        const int state = rt.recordState.load();
        if (state == TrackRuntime::recordWaiting)
            rt.recordState = TrackRuntime::recordIdle;
        else if (state == TrackRuntime::recordRunning)
        {
            const double start = rt.recordStartBeat.load();
            rt.recordEndBeat = rt.recordOverdub.load() ? now : start + bpb * std::max (1.0, std::ceil ((now - start) / bpb - 1.0e-9));
            rt.recordState = TrackRuntime::recordFinished;
        }
    }
}

// Moves the recordings along as the transport passes their start and end beats.
void AudioEngine::updateRecordStates (const PlaybackGraph& graph, double blockEndBeat)
{
    for (const auto& t : graph.tracks)
    {
        auto& rt = *t.runtime;
        int state = rt.recordState.load();
        if (state == TrackRuntime::recordWaiting && rt.recordStartBeat.load() < blockEndBeat)
        {
            state = TrackRuntime::recordRunning;
            rt.recordState = state;
        }
        if (state == TrackRuntime::recordRunning && rt.recordEndBeat.load() <= blockEndBeat)
            rt.recordState = TrackRuntime::recordFinished;
    }
}

bool AudioEngine::acceptsLiveMidi (const TrackRuntime& rt, bool fromKeyboard, bool anyArmed) const
{
    // Live MIDI goes to armed tracks; with no armed track the selected one is armed automatically.
    if (! (rt.arm.load() || (! anyArmed && rt.trackId == selectedTrackId.load())))
        return false;
    switch ((TrackInputMode) rt.inputMode.load())
    {
        case TrackInputMode::allMidi: return true;
        case TrackInputMode::keyboard: return fromKeyboard;
        case TrackInputMode::midiDevice: return ! fromKeyboard;
        case TrackInputMode::none: return false;
    }
    return false;
}

// Every note, controller, bend and pressure message of this block goes to the recorder and the capture buffer,
// once per track it reaches (once with track 0 if it reaches none).
void AudioEngine::routeInputEvents (const PlaybackGraph& graph, const daw::engine::Transport::Block& block, double spb, int64_t blockEngineStart,
                                    bool anyArmed)
{
    const int compensation = recordCompensation.load();
    const bool transportPlaying = transport.isPlaying();

    const auto push = [&] (const daw::engine::InputEvent& e) {
        const auto scope = inputFifo.write (1);
        if (scope.blockSize1 + scope.blockSize2 < 1)
        {
            inputDrops.fetch_add (1, std::memory_order_relaxed);
            return;
        }
        inputEvents[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)] = e;
    };

    const auto route = [&] (const juce::MidiBuffer& buffer, bool fromKeyboard) {
        for (const auto meta : buffer)
        {
            const int size = meta.numBytes;
            if (size < 2 || size > 3)
                continue;
            const int type = meta.data[0] & 0xF0;
            if (type != 0x80 && type != 0x90 && type != 0xB0 && type != 0xD0 && type != 0xE0)
                continue;
            if (size < (type == 0xD0 ? 2 : 3))
                continue;

            daw::engine::InputEvent e;
            e.engineSample = blockEngineStart + meta.samplePosition;
            e.beat = block.startBeat + (double) (meta.samplePosition - compensation) / spb;
            e.playing = transportPlaying;
            e.size = (uint8_t) size;
            for (int i = 0; i < size; ++i)
                e.bytes[i] = meta.data[i];

            bool reached = false;
            for (const auto& t : graph.tracks)
                if (acceptsLiveMidi (*t.runtime, fromKeyboard, anyArmed))
                {
                    e.trackId = t.runtime->trackId;
                    push (e);
                    reached = true;
                }
            if (! reached)
            {
                e.trackId = 0;
                push (e);
            }
        }
    };
    route (hardwareMidi, false);
    route (keyboardMidi, true);
}

namespace
{
// The song beat at the monotonic beat `mono`, which lies in the block (or just before it: a launch that reached the
// graph late), read off the windows of the block.
double songBeatAtMono (const daw::engine::Transport::Block& block, double spb, double mono)
{
    if (block.numWindows == 0)
        return block.songStartBeat;
    const double sample = (mono - block.startBeat) * spb;
    int pick = 0;
    for (int i = 0; i < block.numWindows; ++i)
        if (block.windows[i].sampleOffset <= sample + 1.0e-6)
            pick = i;
    const auto& w = block.windows[pick];
    return w.startBeat + (sample - w.sampleOffset) / spb;
}

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

namespace
{
// Adds a message to `dest`, moving notes by `semitones`. `sounding` remembers what each pitch was played as,
// so a note-off ends the note that began even if the transposition was changed while it was held.
void addTransposed (juce::MidiBuffer& dest, const uint8_t* data, int size, int offset, int semitones, std::array<uint8_t, 128>& sounding)
{
    if (size != 3)
    {
        dest.addEvent (data, size, offset);
        return;
    }
    const int type = data[0] & 0xF0;
    const int note = data[1] & 0x7F;
    const bool noteOn = type == 0x90 && data[2] > 0;
    const bool noteOff = type == 0x80 || (type == 0x90 && data[2] == 0);
    if (! noteOn && ! noteOff && type != 0xA0)
    {
        dest.addEvent (data, size, offset);
        return;
    }

    int target = note + semitones;
    if (noteOn)
    {
        const bool inRange = target >= 0 && target <= 127;
        sounding[(size_t) note] = inRange ? (uint8_t) (target + 1) : TrackRuntime::kDropped;
        if (! inRange)
            return;
    }
    else if (const uint8_t mapped = sounding[(size_t) note]; mapped != 0)
    {
        if (mapped == TrackRuntime::kDropped)
        {
            if (noteOff)
                sounding[(size_t) note] = 0;
            return;
        }
        target = mapped - 1;
        if (noteOff)
            sounding[(size_t) note] = 0;
    }
    if (target < 0 || target > 127)
        return;
    const uint8_t out[3] { data[0], (uint8_t) target, data[2] };
    dest.addEvent (out, 3, offset);
}
} // namespace

// The Session View clip of an audio track: what played from the start of the block (until it stopped or gave way) and
// what began inside it. Both are placed by their launch beat, so the audio stays on the beat however the blocks fall.
void AudioEngine::renderSessionAudio (const PlaybackGraph::Track& t, const daw::engine::MidiClip* before, double startBefore,
                                      const daw::engine::Transport::Block& block, double spb, int numSamples)
{
    auto& rt = *t.runtime;
    const double rate = sampleRate.load();
    const double blockBeats = block.endBeat - block.startBeat;
    const auto* now = rt.player.playingClipPtr();
    const double startNow = rt.player.playingStartBeat();
    const bool same = now == before && juce::exactlyEqual (startNow, startBefore);
    daw::engine::AudioFades fades;
    fades.fadeIn = kAutoFadeSeconds * rate / spb;

    const auto render = [&] (const daw::engine::MidiClip& clip, double launchBeat, double from, double to) {
        if (clip.audio == nullptr || to <= from)
            return;
        const int a = (int) std::llround ((from - block.startBeat) * spb), b = (int) std::llround ((to - block.startBeat) * spb);
        const int first = std::clamp (a, 0, numSamples), last = std::clamp (b, 0, numSamples);
        if (last <= first)
            return;
        const double elapsed = from - launchBeat + (double) (first - a) / spb;
        daw::engine::renderAudioClip (clip, clip.playStart(), clip.loop, elapsed, spb, rate, fades, rt.buffer.getWritePointer (0) + first,
                                      rt.buffer.getWritePointer (1) + first, last - first, audioStats);
    };

    if (before != nullptr)
    {
        const double end = same ? block.endBeat : std::clamp (rt.player.lastEndBeat(), block.startBeat, block.endBeat);
        render (*before, startBefore, std::max (block.startBeat, startBefore), end);
    }
    if (now != nullptr && ! same && blockBeats > 0.0)
        render (*now, startNow, std::max (block.startBeat, startNow), block.endBeat);
}

// The arrangement clips of an audio track in one window of the song, with their fades (at least 4 ms at each edge, so
// that a cut never clicks).
void AudioEngine::renderArrangementAudio (const PlaybackGraph::Track& t, double startBeat, double endBeat, int sampleOffset, int numSamples,
                                          double spb)
{
    auto& rt = *t.runtime;
    const double rate = sampleRate.load();
    const double autoFade = kAutoFadeSeconds * rate / spb;
    const auto& clips = t.arrangement;
    auto it = std::partition_point (clips.begin(), clips.end(), [startBeat] (const auto& c) { return c.endBeat <= startBeat; });
    for (; it != clips.end() && it->startBeat < endBeat; ++it)
    {
        const auto& a = *it;
        if (a.clip == nullptr || a.clip->audio == nullptr)
            continue;
        const double from = std::max (startBeat, a.startBeat), to = std::min (endBeat, a.endBeat);
        const int first = std::clamp ((int) std::llround ((from - startBeat) * spb), 0, numSamples);
        const int last = std::clamp ((int) std::llround ((to - startBeat) * spb), 0, numSamples);
        if (last <= first)
            continue;
        daw::engine::AudioFades fades;
        fades.length = a.endBeat - a.startBeat;
        fades.fadeIn = std::min (fades.length * 0.5, std::max (autoFade, a.clip->audio->fadeIn));
        fades.fadeOut = std::min (fades.length * 0.5, std::max (autoFade, a.clip->audio->fadeOut));
        const double entry = daw::engine::normaliseOffset (*a.clip, a.offsetBeats, a.loop);
        const double elapsed = (startBeat + (double) first / spb) - a.startBeat;
        daw::engine::renderAudioClip (*a.clip, entry, a.loop, elapsed, spb, rate, fades, rt.buffer.getWritePointer (0) + sampleOffset + first,
                                      rt.buffer.getWritePointer (1) + sampleOffset + first, last - first, audioStats);
    }
}

void AudioEngine::renderTrack (const PlaybackGraph::Track& t, const daw::engine::Transport::Block& block, double spb, int numSamples,
                               bool killNotes, bool anySolo, bool anyArmed)
{
    auto& rt = *t.runtime;

    rt.events.clear();
    const int slotBefore = rt.player.playingSlot();
    const double startBefore = rt.player.playingStartBeat();
    const auto* clipBefore = rt.player.playingClipPtr();
    rt.player.process (block.startBeat, block.endBeat, spb, numSamples, rt.events);

    // An audio track starts every block from silence and adds its clips; its devices are effects.
    if (t.audio)
    {
        rt.buffer.clear (0, 0, numSamples);
        rt.buffer.clear (1, 0, numSamples);
        audioStats.missedFrames = 0;
        renderSessionAudio (t, clipBefore, startBefore, block, spb, numSamples);
    }

    // Arrangement recording logs when a session clip starts or stops (the song beat comes from the windows of the block).
    if (logEvents)
    {
        const int slotNow = rt.player.playingSlot();
        const double startNow = rt.player.playingStartBeat();
        if (slotNow != slotBefore || (slotNow >= 0 && ! juce::exactlyEqual (startNow, startBefore)))
        {
            EngineEvent e;
            e.trackId = rt.trackId;
            if (slotNow >= 0)
            {
                e.type = EngineEvent::Type::launch;
                e.slot = slotNow;
                e.monoBeat = e.clipStartMono = startNow;
            }
            else
            {
                e.type = EngineEvent::Type::stop;
                e.slot = slotBefore;
                e.monoBeat = rt.player.lastEndBeat();
            }
            e.beat = songBeatAtMono (block, spb, e.monoBeat);
            pushEngineEvent (e);
        }
    }

    // A session clip that plays takes the track away from the arrangement, until "Back to arrangement". The arrangement
    // goes quiet for the whole block in which the clip starts (at most one block early).
    if (rt.player.playingSlot() != daw::engine::ClipPlayer::kNoClip)
        rt.inSession.store (true, std::memory_order_relaxed);

    // "Back to arrangement" takes effect at its quantisation boundary: the session clip has just been stopped there by the
    // player, and the arrangement plays from that sample on, chasing the notes under the position.
    int arrangementFrom = 0;
    if (rt.backPending && rt.backBeat < block.endBeat)
    {
        rt.backPending = false;
        rt.inSession.store (false, std::memory_order_relaxed);
        rt.arrangement.invalidate();
        arrangementFrom = std::clamp ((int) std::llround ((rt.backBeat - block.startBeat) * spb), 0, std::max (0, numSamples - 1));
    }

    // The count-in is only the click: the arrangement waits for beat 0 of the monotonic clock, where the song reaches its start.
    if (block.startBeat < 0.0)
        arrangementFrom = std::max (arrangementFrom, (int) std::min ((double) numSamples, std::ceil (-block.startBeat * spb)));

    // While a recording runs, the arrangement of the tracks that record is silent where the recording replaces it
    // (the whole song, or only the loop brace with punch-in / punch-out). Overdub keeps playing what is there.
    const bool replacedByRecording = arrRecState.load (std::memory_order_relaxed) == arrangementRunning && ! overdubOn.load()
                                     && isRecordTarget (rt, anyArmed);
    const double punchLow = punchInOn.load() ? arrLoopStart.load() : -1.0e18;
    const double punchHigh = punchOutOn.load() ? arrLoopEnd.load() : 1.0e18;

    if (killNotes)
        rt.arrangement.releaseAll (0, rt.events);
    if (rt.inSession.load (std::memory_order_relaxed))
    {
        if (rt.arrangement.hasHeldNotes())
            rt.arrangement.releaseAll (0, rt.events);
        rt.arrangement.invalidate();
    }
    else
        for (int i = 0; i < block.numWindows; ++i)
        {
            auto w = block.windows[i];
            if (w.sampleOffset + w.numSamples <= arrangementFrom)
                continue;
            if (replacedByRecording && w.startBeat >= punchLow - 1.0e-9 && w.endBeat <= punchHigh + 1.0e-9)
            {
                // The recording takes this stretch over: what the track played here gives way.
                if (rt.arrangement.hasHeldNotes())
                    rt.arrangement.releaseAll (w.sampleOffset, rt.events);
                rt.arrangement.invalidate();
                continue;
            }
            if (w.sampleOffset < arrangementFrom) // the window begins before the boundary: only its later part plays
            {
                const int cut = arrangementFrom - w.sampleOffset;
                w.startBeat += (double) cut / spb;
                w.sampleOffset += cut;
                w.numSamples -= cut;
            }
            rt.arrangement.process (t.arrangement, w.startBeat, w.endBeat, spb, w.sampleOffset, w.numSamples, rt.events);
            if (t.audio)
                renderArrangementAudio (t, w.startBeat, w.endBeat, w.sampleOffset, w.numSamples, spb);
        }
    if (t.audio && audioStats.missedFrames > 0)
        diskMisses.fetch_add (1, std::memory_order_relaxed);
    daw::engine::sortEvents (rt.events);

    rt.playingSlot.store (rt.player.playingSlot(), std::memory_order_relaxed);
    rt.queuedSlot.store (rt.player.queuedSlot(), std::memory_order_relaxed);
    rt.playingStartBeat.store (rt.player.playingStartBeat(), std::memory_order_relaxed);

    rt.midi.clear();
    if (killNotes)
    {
        for (int ch = 1; ch <= 16; ++ch)
            rt.midi.addEvent (juce::MidiMessage::allNotesOff (ch), 0);
        rt.clipSounding.fill (0);
        rt.liveSounding.fill (0);
    }
    const int semitones = rt.transpose.load (std::memory_order_relaxed);
    for (const auto& e : rt.events)
    {
        const uint8_t bytes[3] { e.status, e.data1, e.data2 };
        const int type = e.status & 0xF0;
        addTransposed (rt.midi, bytes, type == 0xC0 || type == 0xD0 ? 2 : 3, e.sampleOffset, semitones, rt.clipSounding);
    }

    const auto addLive = [&] (const juce::MidiBuffer& source) {
        for (const auto meta : source)
            addTransposed (rt.midi, meta.data, meta.numBytes, meta.samplePosition, semitones, rt.liveSounding);
    };
    if (acceptsLiveMidi (rt, false, anyArmed))
        addLive (hardwareMidi);
    if (acceptsLiveMidi (rt, true, anyArmed))
        addLive (keyboardMidi);

    if (t.audio)
        processChain (t.chain, rt.buffer, numSamples, rt.midi, rt.fxMidi, false);
    else
    {
        if (t.chain.empty())
            return;
        // Start from silence so that a missing or disabled instrument cannot feed old audio into the effects.
        rt.buffer.clear (0, 0, numSamples);
        rt.buffer.clear (1, 0, numSamples);
        processChain (t.chain, rt.buffer, numSamples, rt.midi, rt.fxMidi, true);
    }

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

    // The clock of InputEvent::engineSample counts every block, whether or not there is a graph to play.
    const int64_t blockEngineStart = engineSamples;
    engineSamples += numSamples;
    engineSampleCount.store (engineSamples, std::memory_order_relaxed);

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

    transport.setSongStart (arrPlayStart.load());
    transport.setLoop (arrLoopStart.load(), arrLoopEnd.load(), arrLoopOn.load());

    // Stop first, so that a record command that arrives with it starts a fresh transport.
    const bool killNotes = stopRequested.exchange (false);
    if (killNotes)
    {
        finishRecordingsAtStop (*graph);
        for (const auto& t : graph->tracks)
        {
            t.runtime->resumeSlot = t.runtime->player.playingSlot();
            t.runtime->player.stopImmediately();
            t.runtime->arrangement.invalidate(); // its notes are released in renderTrack; the next start chases from its position
            t.runtime->backPending = false;
        }
        transport.stop();
    }

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

    if (! juce::exactlyEqual (transport.getTempo(), tempo.load()))
        transport.setTempo (tempo.load());
    if (playing.load() && ! transport.isPlaying())
    {
        transport.play (continueSong);
        continueSong = false;
    }

    const auto block = transport.advance (numSamples);
    const double spb = transport.samplesPerBeat();

    master.clear();
    bool anySolo = false, anyArmed = false, anyRecording = false;
    for (const auto& t : graph->tracks)
    {
        anySolo = anySolo || t.runtime->solo.load();
        anyArmed = anyArmed || t.runtime->arm.load();
    }

    logEvents = arrRecState.load() == arrangementWaiting || arrRecState.load() == arrangementRunning;
    if (logEvents)
        logSongWindows (block, spb);
    if (arrRecState.load() == arrangementWaiting && arrRecStart.load() < block.endBeat)
        arrRecState = arrangementRunning;

    routeInputEvents (*graph, block, spb, blockEngineStart, anyArmed);
    updateRecordStates (*graph, block.endBeat);
    for (const auto& t : graph->tracks)
    {
        const int state = t.runtime->recordState.load();
        anyRecording = anyRecording || state == TrackRuntime::recordWaiting || state == TrackRuntime::recordRunning;
    }

    for (const auto& t : graph->tracks)
        renderTrack (t, block, spb, numSamples, killNotes, anySolo, anyArmed);

    // Master chain works on the first two channels of the (wider) master buffer
    processChain (graph->masterChain, master, numSamples, masterMidi, masterFxMidi, false);

    master.applyGain (masterGain.load());
    masterMeterL.store (juce::jmax (masterMeterL.load(), master.getMagnitude (0, 0, numSamples)), std::memory_order_relaxed);
    masterMeterR.store (juce::jmax (masterMeterR.load(), master.getMagnitude (1, 0, numSamples)), std::memory_order_relaxed);

    // The metronome goes in after the master chain and the master gain (its own volume, no limiter in the way). It
    // is a live aid: the offline export must never call this. The count-in always clicks.
    {
        const bool countIn = block.startBeat < 0.0;
        const bool emit = transport.isPlaying() && (countIn || (metronomeOn.load() && (! metronomeOnlyRecording.load() || anyRecording)));
        // The clicks follow the song, not the monotonic clock: after a loop jump or a seek they stay on the bar lines.
        const auto sound = metronomeWood.load() ? Metronome::Sound::wood : Metronome::Sound::classic;
        if (block.numWindows == 0)
            metronome.render (master.getWritePointer (0), master.getWritePointer (1), numSamples, block.songStartBeat, spb, beatsPerBar.load(),
                              metronomeGain.load(), sound, emit);
        for (int i = 0; i < block.numWindows; ++i)
        {
            const auto& w = block.windows[i];
            metronome.render (master.getWritePointer (0) + w.sampleOffset, master.getWritePointer (1) + w.sampleOffset, w.numSamples, w.startBeat,
                              spb, beatsPerBar.load(), metronomeGain.load(), sound, emit);
        }
    }

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

    updateArrangementRecord (block.endBeat);

    positionBeats = transport.getPositionBeats();
    songPositionBeats = transport.getSongPositionBeats();

    bool anyInSessionNow = false;
    for (const auto& t : graph->tracks)
        anyInSessionNow = anyInSessionNow || t.runtime->inSession.load (std::memory_order_relaxed);
    anyInSession.store (anyInSessionNow, std::memory_order_relaxed);
}
