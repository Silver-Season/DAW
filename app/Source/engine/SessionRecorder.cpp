#include "SessionRecorder.h"

#include "../Strings.h"

#include "daw/engine/Quantize.h"

#include <algorithm>
#include <cmath>

namespace
{
using daw::engine::ControllerEvent;
using daw::engine::InputEvent;
using daw::engine::Note;

// A note-on this close before the start beat of a recording still belongs to it: the player was a hair early.
constexpr double kEarlyBeats = 1.0 / 16.0;
// A recording whose command the audio thread never took (no device is running) is forgotten after this long.
constexpr juce::uint32 kPendingTimeoutMs = 2000;

bool noteBefore (const Note& a, const Note& b)
{
    return juce::exactlyEqual (a.start, b.start) ? a.pitch < b.pitch : a.start < b.start;
}

// Applies the record quantisation (grid in beats, 0 = off) and keeps every start inside the clip: a note that is
// pulled onto the loop point goes to the beginning of the next pass.
std::vector<Note> quantiseRecorded (std::vector<Note> notes, double grid, double clipLength)
{
    if (grid > 0.0)
        notes = daw::engine::quantizeNotes (notes, grid, 1.0, 0.0, false);
    for (auto& n : notes)
    {
        if (n.start >= clipLength - 1.0e-9)
            n.start = std::fmod (n.start, clipLength);
        n.length = std::max (1.0e-3, std::min (n.length, clipLength - n.start));
    }
    std::stable_sort (notes.begin(), notes.end(), noteBefore);
    return notes;
}

juce::uint32 nowMs() { return juce::Time::getMillisecondCounter(); }
} // namespace

//==============================================================================
SessionRecorder::SessionRecorder (AudioEngine& e, ProjectModel& m) : engine (e), model (m), arr (e, m)
{
    scratch.reserve (4096);
}

SessionRecorder::~SessionRecorder()
{
    stopTimer();
}

//==============================================================================
bool SessionRecorder::startRecording (int64_t trackId, int scene)
{
    const auto track = model.findTrack (trackId);
    auto* rt = engine.findRuntime (trackId);
    if (! track.isValid() || rt == nullptr || track.type() != "midi" || track.recordSource() != "midi")
        return false;
    if (scene < 0 || scene >= track.numSlots() || recs.count (trackId) > 0 || rt->recordState.load() != TrackRuntime::recordIdle)
        return false;
    if (! engine.startRecord (trackId, scene))
        return false;
    Rec rec;
    rec.scene = scene;
    rec.createdMs = nowMs();
    recs[trackId] = std::move (rec);
    return true;
}

bool SessionRecorder::stopRecording (int64_t trackId)
{
    return recs.count (trackId) > 0 && engine.stopRecord (trackId);
}

bool SessionRecorder::toggleRecording (int64_t trackId, int scene)
{
    return recs.count (trackId) > 0 ? stopRecording (trackId) : startRecording (trackId, scene);
}

bool SessionRecorder::sessionRecord (int fromScene)
{
    if (! recs.empty())
    {
        for (const auto& [trackId, rec] : recs)
            engine.stopRecord (trackId);
        return true;
    }

    juce::Array<TrackRef> targets;
    for (const auto& t : model.tracks())
        if (t.arm())
            targets.add (t);
    if (targets.isEmpty())
        if (const auto selected = model.findTrack (model.selectedTrack()); selected.isValid())
            targets.add (selected);

    bool started = false;
    for (const auto& t : targets)
    {
        int scene = -1;
        if (model.overdub())
            if (auto* rt = engine.findRuntime (t.id()); rt != nullptr && rt->playingSlot.load() >= 0 && t.clipAt (rt->playingSlot.load()).isValid())
                scene = rt->playingSlot.load();
        for (int s = juce::jmax (0, fromScene); scene < 0 && s < t.numSlots(); ++s)
            if (! t.clipAt (s).isValid())
                scene = s;
        if (scene >= 0)
            started = startRecording (t.id(), scene) || started;
    }
    return started;
}

bool SessionRecorder::isRecording (int64_t trackId, int scene) const
{
    const auto it = recs.find (trackId);
    return it != recs.end() && it->second.scene == scene;
}

bool SessionRecorder::getProgress (int64_t trackId, Progress& out) const
{
    const auto it = recs.find (trackId);
    if (it == recs.end() || ! it->second.sawState)
        return false;
    const auto& rec = it->second;
    out = {};
    out.waiting = rec.waiting;
    out.overdub = rec.overdub;
    out.startBeat = rec.startBeat;
    out.lengthBeats = std::max (0.0, positionAtPoll - rec.startBeat);
    if (! rec.overdub)
    {
        out.notes = rec.display.notes();
        for (const auto& o : rec.display.openNotes())
        {
            Note n;
            n.start = o.start;
            n.length = std::max (1.0e-3, out.lengthBeats - o.start);
            n.pitch = o.pitch;
            n.velocity = o.velocity;
            out.notes.push_back (n);
        }
    }
    return true;
}

//==============================================================================
void SessionRecorder::poll()
{
    // The states are read BEFORE the events are drained: a recording seen as finished has all its events in the
    // queue already, so the drain below cannot miss the tail of it. The same goes for the position.
    struct Snapshot
    {
        TrackRuntime* runtime = nullptr;
        int state = 0;
        bool overdub = false;
        double start = 0.0, end = TrackRuntime::kNoEnd;
    };
    std::map<int64_t, Snapshot> snapshots;
    for (const auto& [trackId, rec] : recs)
        if (auto* rt = engine.findRuntime (trackId))
        {
            Snapshot s;
            s.runtime = rt;
            s.state = rt->recordState.load();
            s.overdub = rt->recordOverdub.load();
            s.start = rt->recordStartBeat.load();
            s.end = rt->recordEndBeat.load();
            snapshots[trackId] = s;
        }
    positionAtPoll = engine.getPositionBeats();
    const auto arrangementState = arr.snapshot();

    drainEvents();
    arr.process (arrangementState);

    for (auto it = recs.begin(); it != recs.end();)
    {
        const auto snap = snapshots.find (it->first);
        bool keep = snap != snapshots.end() && model.findTrack (it->first).isValid();
        if (keep)
        {
            auto& rec = it->second;
            if (snap->second.state == TrackRuntime::recordIdle)
                keep = ! rec.sawState && nowMs() - rec.createdMs < kPendingTimeoutMs; // else it was cancelled
            else
            {
                rec.sawState = true;
                rec.overdub = snap->second.overdub;
                rec.startBeat = snap->second.start;
                rec.endBeat = snap->second.end;
                rec.waiting = snap->second.state == TrackRuntime::recordWaiting;
                keep = rec.overdub ? stepOverdub (it->first, rec, *snap->second.runtime, snap->second.state)
                                   : stepNew (it->first, rec, *snap->second.runtime, snap->second.state);
            }
        }
        it = keep ? std::next (it) : recs.erase (it);
    }
}

void SessionRecorder::drainEvents()
{
    scratch.clear();
    engine.popInputEvents (scratch);
    if (scratch.empty())
        return;
    for (const auto& e : scratch)
    {
        arr.feedInput (e);
        capture_.push (e);
        if (e.playing)
            if (const auto it = recs.find (e.trackId); it != recs.end() && (it->second.events.size() < 200000))
                it->second.events.push_back (e);
    }
    capture_.evictOlderThan (engine.getEngineSample() - (int64_t) (daw::engine::CaptureBuffer::kWindowSeconds * engine.getSampleRate()));
    ++captureVersion;
}

//==============================================================================
bool SessionRecorder::stepNew (int64_t trackId, Rec& rec, TrackRuntime& rt, int state)
{
    // Events since the last poll go into the notes that are drawn in the slot.
    for (; rec.fed < rec.events.size(); ++rec.fed)
    {
        const auto& e = rec.events[rec.fed];
        if (e.beat < rec.startBeat - kEarlyBeats || e.beat >= rec.endBeat)
            continue;
        rec.display.feed (std::max (0.0, e.beat - rec.startBeat), e.bytes, e.size);
    }
    if (state != TrackRuntime::recordFinished)
        return true;

    commitNew (trackId, rec);
    rt.recordState = TrackRuntime::recordIdle;
    // The clip exists in the model now; give the engine a graph that has it and start it on the beat the recording ended.
    engine.rebuildGraph();
    // The new loop starts where the recording ended, unless the player launched another clip on the track for that moment
    // (that launch is what ended the recording) or the transport has stopped.
    const bool otherClipFollows = rt.queuedSlot.load() >= 0 || (rt.playingSlot.load() >= 0 && rt.playingStartBeat.load() > rec.endBeat - 1.0e-6);
    if (engine.isPlaying() && ! otherClipFollows)
        engine.launchAligned (trackId, rec.scene, rec.endBeat);
    return false;
}

void SessionRecorder::commitNew (int64_t trackId, Rec& rec)
{
    const auto track = model.findTrack (trackId);
    if (rec.scene >= track.numSlots())
        return;
    const double length = std::max (1.0e-3, rec.endBeat - rec.startBeat);

    daw::engine::NoteAssembler assembler;
    for (const auto& e : rec.events)
        if (e.beat >= rec.startBeat - kEarlyBeats && e.beat < rec.endBeat)
            assembler.feed (std::max (0.0, e.beat - rec.startBeat), e.bytes, e.size);
    assembler.closeAll (length); // notes still held at the end close at the end of the clip
    const auto notes = quantiseRecorded (assembler.takeNotes(), model.recordQuantise(), length);
    const auto controllers = assembler.takeControllers();

    model.undo.beginNewTransaction (Strings::recording::undoRecordClip);
    auto clip = model.createClip (track, rec.scene, length);
    clip.setNotes (notes, &model.undo);
    clip.setControllers (controllers, &model.undo);
}

//==============================================================================
daw::engine::MidiClip SessionRecorder::shapeOf (const ClipRef& clip) const
{
    daw::engine::MidiClip shape;
    shape.lengthBeats = clip.lengthBeats();
    shape.loopStart = clip.loopStart();
    shape.startOffset = clip.startOffset();
    shape.loop = clip.loopEnabled();
    return shape;
}

long SessionRecorder::passOf (const daw::engine::MidiClip& shape, double elapsed)
{
    const double first = shape.firstPassBeats();
    if (elapsed < first)
        return 0;
    if (! shape.loop)
        return 1;
    return 1 + (long) std::floor ((elapsed - first) / shape.braceBeats());
}

bool SessionRecorder::stepOverdub (int64_t trackId, Rec& rec, TrackRuntime& rt, int state)
{
    const auto track = model.findTrack (trackId);
    const auto clip = track.clipAt (rec.scene);
    if (! clip.isValid())
    {
        engine.stopRecord (trackId);
        return false;
    }
    const auto shape = shapeOf (clip);
    const double playStart = rt.playingStartBeat.load();
    if (! rec.overdubStarted)
    {
        rec.overdubStarted = true;
        rec.lastPass = passOf (shape, rec.startBeat - playStart);
    }

    // Notes are kept in beats of the transport; they are put on the clip's own time line when a layer is written.
    for (const auto& e : rec.events)
        if (e.beat >= rec.startBeat && e.beat < rec.endBeat)
            rec.assembler.feed (e.beat, e.bytes, e.size);
    rec.events.clear();
    rec.fed = 0;
    if (state == TrackRuntime::recordFinished)
        rec.assembler.closeAll (rec.endBeat);
    for (const auto& n : rec.assembler.takeNotes())
        rec.pendingNotes.push_back (n);
    for (const auto& c : rec.assembler.takeControllers())
        rec.pendingControllers.push_back (c);

    if (state == TrackRuntime::recordFinished)
    {
        flushOverdub (rec, clip, shape, playStart, true, 0);
        rt.recordState = TrackRuntime::recordIdle;
        return false;
    }

    // Each time the loop starts another pass, what was played in the earlier ones is added as one layer.
    const long pass = passOf (shape, positionAtPoll - playStart);
    if (pass > rec.lastPass)
    {
        flushOverdub (rec, clip, shape, playStart, false, pass);
        rec.lastPass = pass;
    }
    return true;
}

void SessionRecorder::flushOverdub (Rec& rec, const ClipRef& clip, const daw::engine::MidiClip& shape, double playStart, bool all, long beforePass)
{
    const auto due = [&] (double beat) { return all || passOf (shape, beat - playStart) < beforePass; };
    const double length = shape.endBeat();

    std::vector<Note> layer, keepNotes;
    for (auto n : rec.pendingNotes)
    {
        if (! due (n.start))
        {
            keepNotes.push_back (n);
            continue;
        }
        n.start = shape.positionAfter (n.start - playStart);
        layer.push_back (n);
    }
    std::vector<ControllerEvent> moved, keepControllers;
    for (auto c : rec.pendingControllers)
    {
        if (! due (c.time))
        {
            keepControllers.push_back (c);
            continue;
        }
        c.time = shape.positionAfter (c.time - playStart);
        moved.push_back (c);
    }
    rec.pendingNotes = std::move (keepNotes);
    rec.pendingControllers = std::move (keepControllers);
    if (layer.empty() && moved.empty())
        return;

    layer = quantiseRecorded (std::move (layer), model.recordQuantise(), length);
    model.undo.beginNewTransaction (Strings::recording::undoOverdub);
    for (const auto& n : layer)
        clip.addNote (n, &model.undo);
    if (! moved.empty())
    {
        auto merged = clip.controllers();
        merged.insert (merged.end(), moved.begin(), moved.end());
        std::stable_sort (merged.begin(), merged.end(), [] (const auto& a, const auto& b) { return a.time < b.time; });
        clip.setControllers (merged, &model.undo);
    }
    engine.rebuildGraph();
}

//==============================================================================
SessionRecorder::CaptureSummary SessionRecorder::capture (int64_t trackId, int sceneHint)
{
    CaptureSummary summary;
    poll(); // bring in the latest events

    const auto track = model.findTrack (trackId);
    if (! track.isValid() || track.type() != "midi")
    {
        summary.info = Strings::recording::captureNoTrack;
        return summary;
    }
    const auto result = daw::engine::capturePhrase (capture_.snapshot(), trackId, engine.getEngineSample(), engine.getSampleRate(),
                                                    model.beatsPerBar(), model.tempo());
    if (! result.ok)
    {
        summary.info = Strings::recording::captureNothing;
        return summary;
    }

    bool projectHasClips = false;
    for (const auto& t : model.tracks())
        for (int s = 0; s < t.numSlots(); ++s)
            projectHasClips = projectHasClips || t.clipAt (s).isValid();

    int scene = -1;
    for (int s = juce::jmax (0, sceneHint); s < track.numSlots(); ++s)
        if (! track.clipAt (s).isValid())
        {
            scene = s;
            break;
        }

    model.undo.beginNewTransaction (Strings::recording::undoCaptureMidi);
    if (scene < 0) // no empty slot from the chosen scene on: a scene is added at the end
    {
        model.insertScene (-1);
        scene = model.numScenes() - 1;
    }
    // Played into an empty project with the transport stopped: the tempo of the phrase becomes the tempo of the project.
    if (! projectHasClips && ! engine.isPlaying() && result.tempoFromPhrase)
        model.setTempo (std::round (result.bpm * 10.0) / 10.0);

    const auto fresh = model.findTrack (trackId);
    auto clip = model.createClip (fresh, scene, result.lengthBeats);
    clip.setNotes (result.notes, &model.undo);
    clip.setControllers (result.controllers, &model.undo);

    engine.rebuildGraph();
    if (engine.isPlaying())
        engine.launchClip (trackId, scene); // on the next launch boundary

    summary.ok = true;
    summary.scene = scene;
    summary.bars = result.numBars;
    summary.numNotes = result.numNotes;
    summary.info = Strings::recording::captured (result.numBars, result.numNotes, track.name(), model.sceneAt (scene).name());
    return summary;
}

void SessionRecorder::getRecentNoteOns (std::vector<NoteMark>& out) const
{
    out.clear();
    for (size_t i = 0; i < capture_.size(); ++i)
    {
        const auto& e = capture_[i];
        if (e.isNoteOn())
            out.push_back ({ e.engineSample, e.bytes[2], e.trackId });
    }
}
