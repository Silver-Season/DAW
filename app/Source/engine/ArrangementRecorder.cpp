#include "ArrangementRecorder.h"
#include "AudioRecorder.h"

#include "../Strings.h"
#include "../model/ArrangementEdit.h"

#include "daw/engine/Arrangement.h"
#include "daw/engine/Quantize.h"

#include <algorithm>
#include <cmath>

namespace
{
using daw::engine::ControllerEvent;
using daw::engine::InputEvent;
using daw::engine::Note;

// A note-on this close before the start of a recording still belongs to it (the player was a hair early).
constexpr double kEarlyBeats = 1.0 / 16.0;
constexpr double kSliver = 1.0 / 64.0;
constexpr double kInf = 1.0e18;
constexpr double kTinyPass = 0.25; // a trailing pass shorter than this that holds nothing is not worth a take
constexpr juce::uint32 kPendingTimeoutMs = 2000;
constexpr size_t kMaxEventsPerTrack = 400000;

bool noteBefore (const Note& a, const Note& b)
{
    return juce::exactlyEqual (a.start, b.start) ? a.pitch < b.pitch : a.start < b.start;
}

//==============================================================================
// The mapping from the monotonic transport clock to the song, built from the anchors of the log.
struct Anchor
{
    double mono = 0.0, song = 0.0;
    bool loop = false;
};

std::vector<Anchor> anchorsOf (const std::vector<EngineEvent>& events)
{
    std::vector<Anchor> out;
    for (const auto& e : events)
        if (e.type == EngineEvent::Type::anchor || e.type == EngineEvent::Type::loopJump)
            out.push_back ({ e.monoBeat, e.beat, e.type == EngineEvent::Type::loopJump });
    std::stable_sort (out.begin(), out.end(), [] (const Anchor& a, const Anchor& b) { return a.mono < b.mono; });
    return out;
}

// Without any anchor the song is taken to run with the monotonic clock.
double songAt (const std::vector<Anchor>& anchors, double mono)
{
    if (anchors.empty())
        return mono;
    const auto it = std::upper_bound (anchors.begin(), anchors.end(), mono + 1.0e-9, [] (double v, const Anchor& a) { return v < a.mono; });
    const Anchor& a = it == anchors.begin() ? anchors.front() : *(it - 1);
    return a.song + (mono - a.mono);
}

// A stretch of the recording in which the song runs without a jump: one pass of a loop, or all of a recording without one.
struct Segment
{
    double m0 = 0.0, m1 = 0.0; // monotonic beats
    double s0 = 0.0;           // song beat at m0
    int group = 0;             // a seek during the recording starts a new group; a loop jump only a new pass
};

std::vector<Segment> splitSegments (const std::vector<Anchor>& anchors, double startMono, double endMono)
{
    std::vector<Segment> out;
    if (endMono - startMono < 1.0e-6)
        return out;
    Segment cur;
    cur.m0 = startMono;
    cur.s0 = songAt (anchors, startMono);
    for (const auto& a : anchors)
    {
        if (a.mono <= startMono + 1.0e-9 || a.mono >= endMono - 1.0e-9)
            continue;
        cur.m1 = a.mono;
        if (cur.m1 - cur.m0 >= 1.0e-6)
            out.push_back (cur);
        Segment next;
        next.m0 = a.mono;
        next.s0 = a.song;
        next.group = a.loop ? cur.group : cur.group + 1;
        cur = next;
    }
    cur.m1 = endMono;
    if (cur.m1 - cur.m0 >= 1.0e-6)
        out.push_back (cur);
    return out;
}

//==============================================================================
// What was played in one pass, in song beats and cut to the punch range.
struct PassData
{
    int group = 0;
    double r0 = 0.0, r1 = 0.0; // the part of the pass that is recorded
    std::vector<Note> notes;
    std::vector<ControllerEvent> controllers;

    bool valid() const { return r1 - r0 >= kSliver; }
    bool hasContent() const { return ! notes.empty() || ! controllers.empty(); }
};

std::vector<PassData> analyse (const std::vector<InputEvent>& input, const std::vector<Anchor>& anchors, double startMono, double endMono,
                               double low, double high)
{
    const auto segments = splitSegments (anchors, startMono, endMono);
    std::vector<PassData> passes (segments.size());
    std::vector<daw::engine::NoteAssembler> assemblers (segments.size());
    for (size_t i = 0; i < segments.size(); ++i)
    {
        passes[i].group = segments[i].group;
        passes[i].r0 = std::max (segments[i].s0, low);
        passes[i].r1 = std::min (segments[i].s0 + (segments[i].m1 - segments[i].m0), high);
    }

    for (const auto& e : input)
    {
        if (! e.playing || e.beat < startMono - kEarlyBeats || e.beat >= endMono || segments.empty())
            continue;
        const auto it = std::upper_bound (segments.begin(), segments.end(), e.beat + 1.0e-9, [] (double v, const Segment& s) { return v < s.m0; });
        const size_t index = it == segments.begin() ? 0 : (size_t) (it - segments.begin()) - 1;
        const auto& seg = segments[index];
        if (! passes[index].valid() || e.beat >= seg.m1)
            continue;
        const double song = seg.s0 + (e.beat - seg.m0);
        if (song < passes[index].r0 - kEarlyBeats || song >= passes[index].r1)
            continue;
        assemblers[index].feed (song, e.bytes, e.size);
    }

    for (size_t i = 0; i < passes.size(); ++i)
    {
        auto& p = passes[i];
        if (! p.valid())
            continue;
        assemblers[i].closeAll (p.r1); // a note held across the end of the pass (a loop jump) ends with it
        for (auto n : assemblers[i].takeNotes())
        {
            n.start = std::max (n.start, p.r0);
            if (n.start >= p.r1 - 1.0e-9)
                continue;
            n.length = std::max (1.0e-3, std::min (n.length, p.r1 - n.start));
            p.notes.push_back (n);
        }
        for (auto c : assemblers[i].takeControllers())
        {
            c.time = std::clamp (c.time, p.r0, p.r1);
            p.controllers.push_back (c);
        }
        std::stable_sort (p.notes.begin(), p.notes.end(), noteBefore);
    }
    return passes;
}

// Applies the record quantisation on the song grid (the song starts on a bar line) and keeps the notes inside [r0, r1).
void quantiseNotes (std::vector<Note>& notes, double grid, double r0, double r1)
{
    if (grid > 0.0)
        notes = daw::engine::quantizeNotes (notes, grid, 1.0, 0.0, false);
    for (auto& n : notes)
    {
        n.start = std::clamp (n.start, r0, r1 - 1.0e-3);
        n.length = std::max (1.0e-3, std::min (n.length, r1 - n.start));
    }
    std::stable_sort (notes.begin(), notes.end(), noteBefore);
}

juce::uint32 nowMs() { return juce::Time::getMillisecondCounter(); }

// The stretches of [from, to) that no clip of the track covers.
std::vector<std::pair<double, double>> uncovered (const TrackRef& track, double from, double to)
{
    std::vector<std::pair<double, double>> gaps;
    double cursor = from;
    for (const auto& c : track.arrangementClips()) // sorted by start
    {
        if (c.end() <= cursor)
            continue;
        if (c.start() >= to)
            break;
        if (c.start() - cursor >= arrangement::kMinClipBeats)
            gaps.emplace_back (cursor, c.start());
        cursor = std::max (cursor, c.end());
    }
    if (to - cursor >= arrangement::kMinClipBeats)
        gaps.emplace_back (cursor, to);
    return gaps;
}
} // namespace

//==============================================================================
RecordButtonAction recordButtonAction (bool timelineRecording, bool slotRecording, bool arrangementShown, bool transportPlaying)
{
    if (timelineRecording)
        return RecordButtonAction::stopTimeline;
    if (slotRecording)
        return RecordButtonAction::stopSlots;
    return arrangementShown || transportPlaying ? RecordButtonAction::recordTimeline : RecordButtonAction::recordSlot;
}

ArrangementRecorder::ArrangementRecorder (AudioEngine& e, ProjectModel& m) : engine (e), model (m) {}

bool ArrangementRecorder::start()
{
    if (active)
        return false;
    bool anyMidi = false;
    std::vector<int64_t> soundTracks;
    for (const auto& t : model.tracks())
    {
        if (sound != nullptr && t.arm() && sound->recordsSound (t))
        {
            if (sound->begin (t.id(), RecordStream::Mode::arrangement))
                soundTracks.push_back (t.id());
        }
        else
            anyMidi = anyMidi || t.type() == "midi";
    }
    if ((! anyMidi && soundTracks.empty()) || ! engine.startArrangementRecord())
    {
        for (const auto id : soundTracks)
            sound->cancel (id);
        return false;
    }
    beginRun (0.0);
    run.soundTracks = std::move (soundTracks);
    active = true;
    awaitingSound = false;
    running = sawState = stopSent = false;
    createdMs = nowMs();
    return true;
}

bool ArrangementRecorder::stop()
{
    if (! active)
        return false;
    stopSent = true;
    return engine.stopArrangementRecord();
}

void ArrangementRecorder::reset()
{
    if (active)
        engine.stopArrangementRecord();
    cancel();
}

void ArrangementRecorder::cancel()
{
    if (sound != nullptr)
        for (const auto id : run.soundTracks)
            sound->cancel (id); // only takes back what has not begun; a file that began completes and is let go of
    active = running = sawState = stopSent = collecting = awaitingSound = false;
    run = {};
}

bool ArrangementRecorder::isSoundTrack (int64_t trackId) const
{
    return std::find (run.soundTracks.begin(), run.soundTracks.end(), trackId) != run.soundTracks.end();
}

bool ArrangementRecorder::soundReady() const
{
    if (sound == nullptr)
        return true;
    for (const auto id : run.soundTracks)
        if (sound->isRecording (id) && ! sound->isFinished (id))
            return false;
    return true;
}

void ArrangementRecorder::finishAndReport()
{
    const auto summary = finishRun (run.endMono);
    cancel();
    lastSummary = summary;
    if (summary.wrote && onFinished)
        onFinished (summary);
}

void ArrangementRecorder::beginRun (double startMono)
{
    const auto soundTracks = std::move (run.soundTracks);
    run = {};
    run.soundTracks = soundTracks;
    run.startMono = startMono;
    collecting = true;
}

void ArrangementRecorder::feedEngineEvent (const EngineEvent& e)
{
    if (run.events.size() < 1000000)
        run.events.push_back (e);
}

void ArrangementRecorder::feedInput (const InputEvent& e)
{
    if (! collecting || ! e.playing || e.trackId == 0)
        return;
    auto& list = run.input[e.trackId];
    if (list.size() < kMaxEventsPerTrack)
        list.push_back (e);
}

ArrangementRecorder::Snapshot ArrangementRecorder::snapshot() const
{
    Snapshot s;
    s.state = engine.getArrangementRecordState();
    s.start = engine.getArrangementRecordStartBeat();
    s.end = engine.getArrangementRecordEndBeat();
    s.songPosition = engine.getSongPositionBeats();
    s.monoPosition = engine.getPositionBeats();
    return s;
}

void ArrangementRecorder::process (const Snapshot& s)
{
    static thread_local std::vector<EngineEvent> scratch;
    scratch.clear();
    engine.popEngineEvents (scratch);

    if (! active)
    {
        // A recording nobody waits for any more (the project was replaced) is let go of.
        if (s.state == AudioEngine::arrangementFinished)
            engine.acknowledgeArrangementRecord();
        return;
    }
    for (const auto& e : scratch)
        feedEngineEvent (e);
    songNow = s.songPosition;
    monoNow = s.monoPosition;

    // The engine is done; the files of the sound tracks get their last frames (the latency tail) and are closed. Should
    // the audio stop for good meanwhile, what was written so far is kept after a few seconds.
    if (awaitingSound)
    {
        if (soundReady() || nowMs() - awaitingSinceMs > 5000)
            finishAndReport();
        return;
    }

    switch (s.state)
    {
        case AudioEngine::arrangementIdle:
            // Either the audio thread has not taken the command yet (no device is running), or the recording was cancelled
            // before it began.
            if (sawState || nowMs() - createdMs >= kPendingTimeoutMs)
                cancel();
            return;
        case AudioEngine::arrangementWaiting:
            sawState = true;
            run.startMono = s.start;
            return;
        case AudioEngine::arrangementRunning:
            sawState = true;
            running = true;
            run.startMono = s.start;
            run.endMono = s.end;
            // Punch-out without a loop ends the recording when the song reaches the end of the brace (the transport goes on).
            if (! stopSent && model.punchOut() && ! model.arrangementLoopOn() && s.songPosition >= model.arrangementLoopEnd())
                stop();
            return;
        case AudioEngine::arrangementFinished:
        {
            run.startMono = s.start;
            run.endMono = s.end;
            engine.acknowledgeArrangementRecord();
            if (soundReady())
                finishAndReport();
            else
            {
                awaitingSound = true;
                awaitingSinceMs = nowMs();
            }
            return;
        }
        default: return;
    }
}

//==============================================================================
bool ArrangementRecorder::getProgress (int64_t trackId, Progress& out) const
{
    if (! active || ! sawState)
        return false;
    const auto track = model.findTrack (trackId);
    if (! track.isValid() || track.type() != "midi")
        return false;
    out = {};
    out.waiting = ! running;
    const auto anchors = anchorsOf (run.events);
    if (! running)
    {
        out.startBeat = out.endBeat = songAt (anchors, run.startMono);
        return true;
    }

    static const std::vector<InputEvent> noInput;
    const auto found = run.input.find (trackId);
    const double low = model.punchIn() ? model.arrangementLoopStart() : -kInf;
    const double high = model.punchOut() ? model.arrangementLoopEnd() : kInf;
    const auto passes = analyse (found != run.input.end() ? found->second : noInput, anchors, run.startMono,
                                 std::max (run.startMono + 1.0e-3, std::min (monoNow, run.endMono)), low, high);
    if (passes.empty())
    {
        out.startBeat = out.endBeat = songAt (anchors, run.startMono);
        return true;
    }
    const int group = passes.back().group;
    double from = kInf, to = -kInf;
    int count = 0;
    const PassData* newest = nullptr;
    for (const auto& p : passes)
        if (p.group == group && p.valid())
        {
            from = std::min (from, p.r0);
            to = std::max (to, p.r1);
            ++count;
            newest = &p;
        }
    if (newest == nullptr)
    {
        out.startBeat = out.endBeat = std::max (low, songNow);
        return true;
    }
    out.startBeat = from;
    out.endBeat = to;
    out.numPasses = out.pass = count;
    for (auto n : newest->notes)
    {
        n.start -= from;
        out.notes.push_back (n);
    }
    return true;
}

//==============================================================================
ArrangementRecorder::Summary ArrangementRecorder::finishRun (double endMono)
{
    run.endMono = endMono;
    Summary s;
    if (endMono - run.startMono >= 1.0e-6)
        s = commit();
    if (s.wrote)
        engine.rebuildGraph();
    run.events.clear();
    run.input.clear();
    collecting = false;
    return s;
}

ArrangementRecorder::Summary ArrangementRecorder::commit()
{
    Summary summary;
    const auto anchors = anchorsOf (run.events);
    const double low = model.punchIn() ? model.arrangementLoopStart() : -kInf;
    const double high = model.punchOut() ? model.arrangementLoopEnd() : kInf;
    const bool overdub = model.overdub();
    const double grid = model.recordQuantise();
    bool opened = false;
    const auto open = [&] {
        if (! opened)
            model.undo.beginNewTransaction (Strings::arrRec::undoRecord);
        opened = true;
    };

    //==========================================================================
    // Session View launches -> copies of the launched clips
    for (const auto& track : model.tracks())
    {
        if (track.type() != "midi")
            continue;
        struct Section
        {
            int slot;
            double clipStart, m0, m1;
        };
        std::vector<Section> sections;
        bool open_ = false;
        Section cur {};
        const auto close = [&] (double at) {
            if (open_ && std::min (at, run.endMono) - std::max (cur.m0, run.startMono) >= kSliver)
                sections.push_back ({ cur.slot, cur.clipStart, std::max (cur.m0, run.startMono), std::min (at, run.endMono) });
            open_ = false;
        };
        for (const auto& e : run.events)
        {
            if (e.trackId != track.id())
                continue;
            if (e.type == EngineEvent::Type::launch)
            {
                close (e.monoBeat);
                cur = { e.slot, e.clipStartMono, e.monoBeat, 0.0 };
                open_ = true;
            }
            else if (e.type == EngineEvent::Type::stop && open_ && cur.slot == e.slot)
                close (e.monoBeat);
        }
        close (run.endMono);

        for (const auto& sec : sections)
        {
            const auto clip = track.clipAt (sec.slot);
            if (! clip.isValid())
                continue;
            const auto content = clip.toMidiClip();
            for (const auto& seg : splitSegments (anchors, sec.m0, sec.m1))
            {
                const double len = seg.m1 - seg.m0;
                const double r0 = std::max (seg.s0, low), r1 = std::min (seg.s0 + len, high);
                if (r1 - r0 < kSliver)
                    continue;
                const double elapsed = (seg.m0 + (r0 - seg.s0)) - sec.clipStart;
                const double offset = content.positionAfter (elapsed);
                double length = r1 - r0;
                if (! content.loop)
                {
                    if (elapsed >= content.firstPassBeats() - 1.0e-9)
                        continue; // the one-shot had played out
                    length = std::min (length, content.endBeat() - offset);
                }
                open();
                arrangement::addSlotClipPart (model, track, clip, r0, length, offset);
                ++summary.numClips;
                ++summary.numLaunches;
            }
        }
    }

    //==========================================================================
    // Live notes
    for (const auto& [trackId, events] : run.input)
    {
        const auto track = model.findTrack (trackId);
        if (! track.isValid() || track.type() != "midi" || isSoundTrack (trackId))
            continue;
        const auto passes = analyse (events, anchors, run.startMono, run.endMono, low, high);

        int groupCount = 0;
        for (const auto& p : passes)
            groupCount = std::max (groupCount, p.group + 1);
        for (int g = 0; g < groupCount; ++g)
        {
            std::vector<const PassData*> mine;
            for (const auto& p : passes)
                if (p.group == g && p.valid())
                    mine.push_back (&p);
            // A last pass that is short and holds nothing is the tail of the stop, not a take.
            while (mine.size() > 1 && ! mine.back()->hasContent() && mine.back()->r1 - mine.back()->r0 < kTinyPass)
                mine.pop_back();
            bool any = false;
            double from = kInf, to = -kInf;
            for (const auto* p : mine)
            {
                any = any || p->hasContent();
                from = std::min (from, p->r0);
                to = std::max (to, p->r1);
            }
            if (! any || to - from < arrangement::kMinClipBeats)
                continue;

            if (overdub)
            {
                // Notes go into the clips they fall into; what falls into a gap becomes a clip of its own.
                const auto gaps = uncovered (track, from, to);
                std::vector<Note> loose;
                std::vector<ControllerEvent> looseControllers;
                std::map<int64_t, std::vector<ControllerEvent>> controllersByClip;
                bool touched = false;
                for (const auto* p : mine)
                {
                    auto notes = p->notes;
                    quantiseNotes (notes, grid, p->r0, p->r1);
                    for (const auto& n : notes)
                    {
                        const auto a = arrangement::clipAt (track, n.start);
                        if (! a.isValid())
                        {
                            loose.push_back (n);
                            continue;
                        }
                        const auto content = a.clip().toMidiClip();
                        double pos = a.offset() + (n.start - a.start());
                        if (a.loop())
                            pos = daw::engine::normaliseOffset (content, pos, true);
                        else if (pos >= content.endBeat())
                            continue;
                        Note copy = n;
                        copy.start = pos;
                        copy.length = std::max (1.0e-3, std::min ({ n.length, a.end() - n.start, content.endBeat() - pos }));
                        open();
                        a.clip().addNote (copy, &model.undo);
                        touched = true;
                    }
                    for (auto c : p->controllers)
                    {
                        const auto a = arrangement::clipAt (track, c.time);
                        if (! a.isValid())
                            continue;
                        const auto content = a.clip().toMidiClip();
                        double pos = a.offset() + (c.time - a.start());
                        if (a.loop())
                            pos = daw::engine::normaliseOffset (content, pos, true);
                        else if (pos >= content.endBeat())
                            continue;
                        c.time = pos;
                        controllersByClip[a.clip().id()].push_back (c);
                    }
                }
                for (const auto& [clipId, list] : controllersByClip)
                    if (const auto clip = model.findClip (clipId); clip.isValid())
                    {
                        auto merged = clip.controllers();
                        merged.insert (merged.end(), list.begin(), list.end());
                        std::stable_sort (merged.begin(), merged.end(), [] (const auto& a, const auto& b) { return a.time < b.time; });
                        open();
                        clip.setControllers (merged, &model.undo);
                        touched = true;
                    }
                if (touched)
                    ++summary.numClips;
                for (const auto& [g0, g1] : gaps)
                {
                    arrangement::RecordedPass pass;
                    for (auto n : loose)
                        if (n.start >= g0 - 1.0e-9 && n.start < g1)
                        {
                            n.start -= g0;
                            n.length = std::max (1.0e-3, std::min (n.length, g1 - g0 - n.start));
                            pass.notes.push_back (n);
                        }
                    if (pass.notes.empty())
                        continue;
                    open();
                    arrangement::addRecordedClip (model, track, g0, g1 - g0, { pass }, 0, Strings::arrRec::clipName);
                    ++summary.numClips;
                }
                continue;
            }

            std::vector<arrangement::RecordedPass> contents;
            int topPass = -1;
            for (const auto* p : mine)
            {
                arrangement::RecordedPass rp;
                auto notes = p->notes;
                quantiseNotes (notes, grid, p->r0, p->r1);
                for (auto n : notes)
                {
                    n.start -= from;
                    rp.notes.push_back (n);
                }
                for (auto c : p->controllers)
                {
                    c.time -= from;
                    rp.controllers.push_back (c);
                }
                if (p->hasContent())
                    topPass = (int) contents.size(); // the newest pass that holds something is on top
                contents.push_back (std::move (rp));
            }
            open();
            arrangement::addRecordedClip (model, track, from, to - from, contents, topPass, Strings::arrRec::clipName);
            ++summary.numClips;
            if (contents.size() > 1)
                summary.numTakes += (int) contents.size();
        }
    }

    //==========================================================================
    // Sound: one file per track; every pass of the loop is a take that reads the file at its own place. Frame 0 of the
    // file is monotonic beat startMono; the content is moved earlier by the latency the recorder measured. The tempo is
    // taken to be the same over the whole recording.
    for (const auto trackId : run.soundTracks)
    {
        AudioRecorder::Finished f;
        if (sound == nullptr || ! sound->takeFinished (trackId, f) || ! f.ok)
            continue;
        const auto track = model.findTrack (trackId);
        if (! track.isValid())
            continue;
        if (f.droppedFrames > 0)
            summary.warning = Strings::recording::audioDropped ((double) juce::jmax ((int64_t) 0, f.firstDropFrame) / f.sampleRate,
                                                                (double) f.droppedFrames * 1000.0 / f.sampleRate);
        const double framesPerBeat = f.sampleRate * 60.0 / std::max (1.0, model.tempo());
        const auto segments = splitSegments (anchors, run.startMono, run.endMono);
        int groupCount = 0;
        for (const auto& seg : segments)
            groupCount = std::max (groupCount, seg.group + 1);

        int64_t fileId = -1;
        for (int g = 0; g < groupCount; ++g)
        {
            struct Pass
            {
                Segment seg;
                double r0, r1;
            };
            std::vector<Pass> passes;
            for (const auto& seg : segments)
            {
                if (seg.group != g)
                    continue;
                const double r0 = std::max (seg.s0, low), r1 = std::min (seg.s0 + (seg.m1 - seg.m0), high);
                if (r1 - r0 >= kSliver)
                    passes.push_back ({ seg, r0, r1 });
            }
            // The tail of the stop after the last full pass is not a take.
            while (passes.size() > 1 && passes.back().r1 - passes.back().r0 < kTinyPass)
                passes.pop_back();
            if (passes.empty())
                continue;
            double from = kInf, to = -kInf;
            for (const auto& p : passes)
            {
                from = std::min (from, p.r0);
                to = std::max (to, p.r1);
            }
            if (to - from < arrangement::kMinClipBeats)
                continue;

            open();
            if (fileId < 0)
                fileId = sound->addToPool (f);
            std::vector<juce::ValueTree> contents;
            int top = (int) passes.size() - 1;
            for (int i = 0; i < (int) passes.size(); ++i)
            {
                const auto& p = passes[(size_t) i];
                const double monoAtFrom = p.seg.m0 + (from - p.seg.s0);
                auto node = model.makeAudioClipNode (track, fileId, to - from, f.name);
                ClipRef (node).setSampleStart (f.latency + (monoAtFrom - run.startMono) * framesPerBeat, nullptr);
                contents.push_back (node);
            }
            // The newest pass that covers at least half of the clip is on top (a pass cut short by the stop is a take).
            for (int i = (int) passes.size() - 1; i >= 0; --i)
                if (passes[(size_t) i].r1 - passes[(size_t) i].r0 >= 0.5 * (to - from))
                {
                    top = i;
                    break;
                }
            arrangement::addRecordedContents (model, track, from, to - from, std::move (contents), top);
            ++summary.numClips;
            if (passes.size() > 1)
                summary.numTakes += (int) passes.size();
        }
    }

    summary.wrote = opened;
    return summary;
}
