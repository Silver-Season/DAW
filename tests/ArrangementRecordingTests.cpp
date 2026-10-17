// Stage 3, tasks 3.4 and 3.5 of docs/spec/03-arrangement.md: recording onto the timeline (takes in a loop, overdub,
// punch-in / punch-out), recording the Session View into the arrangement, and taking a track from a version.
// Spec tests 6, 7 and 9, plus the pieces they rest on: the engine's log of song jumps and launches, a note across a loop
// jump, one undo step per recording, and no heap use on the audio thread while a recording runs.
#include "TestSupport.h"

#include "../app/Source/Strings.h"
#include "../app/Source/engine/ArrangementRecorder.h"
#include "../app/Source/engine/RtCheck.h"
#include "../app/Source/engine/SessionRecorder.h"
#include "../app/Source/library/UserLibrary.h"
#include "../app/Source/model/ArrangementEdit.h"
#include "../app/Source/model/ProjectActions.h"
#include "../app/Source/model/TrackTransfer.h"
#include "../app/Source/project/ProjectFile.h"
#include "../app/Source/project/ProjectSession.h"
#include "../app/Source/ui/ArrangementView.h"
#include "../app/Source/ui/ArrangementPlayhead.h"
#include "../app/Source/ui/Theme.h"
#include "../app/Source/ui/TopBar.h"

#include <array>
#include <atomic>
#include <cmath>
#include <set>
#include <tuple>

using namespace tests;
using daw::engine::InputEvent;
using daw::engine::Note;

namespace
{
constexpr int kBlock = 256;
constexpr double kRate = 48000.0;
constexpr double kSpb = 24000.0; // 120 BPM

double gClockMs = 1000.0;
double scriptedClock() { return gClockMs; }

bool near (double a, double b, double tol = 1.0e-3)
{
    return std::abs (a - b) <= tol;
}

juce::String xmlOf (const ProjectModel& m)
{
    return m.root.toXmlString();
}

std::vector<Note> notesOf (const ArrangementClipRef& c)
{
    return c.clip().notes();
}

bool hasNote (const std::vector<Note>& notes, double start, double length, int pitch, double tol = 1.0e-3)
{
    for (const auto& n : notes)
        if (n.pitch == pitch && near (n.start, start, tol) && near (n.length, length, tol))
            return true;
    return false;
}

//==============================================================================
// Fake events, as the engine would log them and the live input would deliver them (spec test 6 works on these).
EngineEvent anchorAt (double mono, double song, bool loop = false)
{
    EngineEvent e;
    e.type = loop ? EngineEvent::Type::loopJump : EngineEvent::Type::anchor;
    e.monoBeat = mono;
    e.beat = song;
    return e;
}
EngineEvent launchAt (int64_t track, int slot, double mono, double song, double clipStart)
{
    EngineEvent e;
    e.type = EngineEvent::Type::launch;
    e.trackId = track;
    e.slot = slot;
    e.monoBeat = mono;
    e.beat = song;
    e.clipStartMono = clipStart;
    return e;
}
EngineEvent stopAt (int64_t track, int slot, double mono, double song)
{
    EngineEvent e;
    e.type = EngineEvent::Type::stop;
    e.trackId = track;
    e.slot = slot;
    e.monoBeat = mono;
    e.beat = song;
    return e;
}
InputEvent midi (int64_t track, double mono, uint8_t b0, uint8_t b1, uint8_t b2)
{
    InputEvent e;
    e.trackId = track;
    e.beat = mono;
    e.playing = true;
    e.size = 3;
    e.bytes[0] = b0;
    e.bytes[1] = b1;
    e.bytes[2] = b2;
    return e;
}
InputEvent on (int64_t track, double mono, int pitch, int vel = 100) { return midi (track, mono, 0x90, (uint8_t) pitch, (uint8_t) vel); }
InputEvent off (int64_t track, double mono, int pitch) { return midi (track, mono, 0x80, (uint8_t) pitch, 0); }

// A project, an engine that is never started and the recorder, driven by hand.
struct Bench
{
    Bench()
    {
        for (int i = 0; i < 4; ++i)
            f.model.insertScene (-1);
        a = f.model.addTrack ("midi");
        b = f.model.addTrack ("midi");
        f.model.undo.clearUndoHistory();
    }

    // Plays a recording: the given engine log and live input, from monotonic beat `start` to `end`.
    ArrangementRecorder::Summary record (double start, double end, const std::vector<EngineEvent>& events, const std::vector<InputEvent>& input)
    {
        rec.beginRun (start);
        for (const auto& e : events)
            rec.feedEngineEvent (e);
        for (const auto& e : input)
            rec.feedInput (e);
        return rec.finishRun (end);
    }

    Fixture f;
    ArrangementRecorder rec { f.engine, f.model };
    TrackRef a, b;
};

// Runs `op` and checks that it is exactly one undo step.
template <typename F>
void oneStep (ProjectModel& m, const juce::String& what, F op)
{
    const auto before = xmlOf (m);
    op();
    const auto after = xmlOf (m);
    check (after != before, what + ": changes the project");
    m.undo.undo();
    check (xmlOf (m) == before, what + ": one undo restores everything");
    m.undo.redo();
    check (xmlOf (m) == after, what + ": redo brings it back");
}

//==============================================================================
void testRecordOnTimeline()
{
    std::printf ("[arrangement test 6] recording onto the timeline: two bars from 9.1.1 make a clip at beat 32 with the notes in place\n");
    Bench p;
    const auto id = p.a.id();
    // 9.1.1 is bar 9 = beat 32. The engine says: the song is at 32 when the recording starts (monotonic beat 0).
    const std::vector<EngineEvent> log { anchorAt (0.0, 32.0) };
    const std::vector<InputEvent> played { on (id, 0.0, 60), off (id, 1.0, 60), on (id, 4.5, 64), off (id, 6.0, 64), on (id, 7.5, 67) };

    oneStep (p.f.model, "recording", [&] { p.record (0.0, 8.0, log, played); });
    const auto clips = p.a.arrangementClips();
    check (clips.size() == 1, "one clip on the recorded track");
    if (clips.size() == 1)
    {
        check (near (clips[0].start(), 32.0, 1.0e-9) && near (clips[0].length(), 8.0, 1.0e-9), "it starts at beat 32 and lasts the two bars");
        const auto notes = notesOf (clips[0]);
        check (notes.size() == 3 && hasNote (notes, 0.0, 1.0, 60) && hasNote (notes, 4.5, 1.5, 64), "the notes sit where they were played");
        check (hasNote (notes, 7.5, 0.5, 67), "a note held at the end of the recording is closed there");
        check (clips[0].activeTake() == 0 && clips[0].takesNode().getNumChildren() == 0, "a recording without a loop has no takes");
    }
    check (p.b.numArrangementClips() == 0, "a track that played nothing gets no clip");

    // it goes through resolveOverlaps: a clip underneath is cut in two
    {
        Bench q;
        q.f.model.undo.beginNewTransaction ("setup");
        auto under = arrangement::addEmptyClip (q.f.model, q.a, 28.0, 16.0);
        under.clip().setNotes ({ { 0.0, 1.0, 50, 100 }, { 5.0, 1.0, 52, 100 }, { 13.0, 1.0, 55, 100 } }, &q.f.model.undo);
        q.f.model.undo.clearUndoHistory();
        q.record (0.0, 8.0, log, { on (q.a.id(), 1.0, 60), off (q.a.id(), 2.0, 60) });
        const auto spans = q.a.arrangementClips();
        check (spans.size() == 3 && near (spans[0].start(), 28.0) && near (spans[0].end(), 32.0) && near (spans[1].start(), 32.0)
                   && near (spans[1].end(), 40.0) && near (spans[2].start(), 40.0) && near (spans[2].end(), 44.0),
               "the new clip lands over the old one, which is cut in two");
        q.f.model.undo.undo();
        check (q.a.numArrangementClips() == 1 && near (q.a.arrangementClips()[0].length(), 16.0), "one undo brings the old clip back whole");
    }

    // the record quantisation applies, on the grid of the song
    {
        Bench q;
        q.f.model.setRecordQuantise (0.5);
        q.record (0.0, 8.0, log, { on (q.a.id(), 4.43, 64), off (q.a.id(), 5.43, 64) });
        const auto c = q.a.arrangementClips();
        check (c.size() == 1 && hasNote (notesOf (c[0]), 4.5, 1.0, 64), "with record quantisation 1/8 a note at 4.43 lands on 4.5");
    }

    // a recording that began at a position inside a bar keeps its start (the clip starts where the recording did)
    {
        Bench q;
        q.record (0.0, 4.0, { anchorAt (0.0, 5.5) }, { on (q.a.id(), 0.0, 60), off (q.a.id(), 1.0, 60) });
        const auto c = q.a.arrangementClips();
        check (c.size() == 1 && near (c[0].start(), 5.5) && near (c[0].length(), 4.0), "starts where the song position was");
    }

    // live events before the transport ran (the count-in) do not belong to the recording
    {
        Bench q;
        auto early = on (q.a.id(), 1.0, 61);
        early.beat = -3.0;
        q.record (0.0, 4.0, { anchorAt (-4.0, 28.0) }, { early, on (q.a.id(), 2.0, 60), off (q.a.id(), 3.0, 60) });
        const auto c = q.a.arrangementClips();
        check (c.size() == 1 && notesOf (c[0]).size() == 1 && near (c[0].start(), 32.0), "notes played during the count-in are not recorded");
    }
}

//==============================================================================
void testTakes()
{
    std::printf ("[arrangement test] takes: every pass of the loop is a take; the newest pass with notes is on top; switching is one undo step\n");
    Bench p;
    const auto id = p.a.id();
    // the loop is 32..40; the recording starts at 32 and runs 2.5 passes
    const std::vector<EngineEvent> log { anchorAt (0.0, 32.0), anchorAt (8.0, 32.0, true), anchorAt (16.0, 32.0, true) };
    const std::vector<InputEvent> played { on (id, 1.0, 60),  off (id, 2.0, 60),
                                           on (id, 9.5, 62),  off (id, 10.5, 62),
                                           on (id, 15.5, 65), off (id, 16.5, 65), // crosses the jump at the end of the second pass
                                           on (id, 17.0, 67), off (id, 18.0, 67) };
    oneStep (p.f.model, "loop recording", [&] { p.record (0.0, 20.0, log, played); });
    const auto clips = p.a.arrangementClips();
    check (clips.size() == 1 && near (clips[0].start(), 32.0) && near (clips[0].length(), 8.0), "one clip over the loop");
    if (clips.size() != 1)
        return;
    const auto clip = clips[0];
    const auto takes = arrangement::takesOf (clip);
    check (takes.size() == 3 && takes[0].number == 1 && takes[2].number == 3, "three passes make three takes");
    check (clip.activeTake() == 3 && takes[2].active, "the newest pass is on top");
    check (hasNote (notesOf (clip), 1.0, 1.0, 67) && notesOf (clip).size() == 1, "and it shows that pass");

    // a note across the loop jump: closed at the end of its pass, and its note-off in the next pass is not a note
    // (the active take is the third; look inside the second by switching to it)
    p.f.model.undo.beginNewTransaction (Strings::arrRec::undoSwitchTake);
    check (arrangement::switchTake (p.f.model, clip, 2), "take 2 can be shown");
    {
        const auto notes = notesOf (clip);
        check (notes.size() == 2 && hasNote (notes, 1.5, 1.0, 62) && hasNote (notes, 7.5, 0.5, 65),
               "a note held across the loop jump ends with its pass (0.5 beats), nothing is left over in the next");
    }
    check (clip.activeTake() == 2 && arrangement::takesOf (clip).size() == 3, "switching keeps all three takes");
    p.f.model.undo.undo();
    check (clip.activeTake() == 3 && hasNote (notesOf (clip), 1.0, 1.0, 67), "undo of the switch brings the third take back");
    p.f.model.undo.redo();
    p.f.model.undo.beginNewTransaction (Strings::arrRec::undoSwitchTake);
    check (arrangement::switchTake (p.f.model, clip, 1), "take 1");
    check (notesOf (clip).size() == 1 && hasNote (notesOf (clip), 1.0, 1.0, 60), "take 1 holds the first pass");
    check (! arrangement::switchTake (p.f.model, clip, 1) && ! arrangement::switchTake (p.f.model, clip, 7), "the shown take and a missing one cannot be switched to");

    // ids stay unique, a copy of the project's tree has no clashes
    std::set<juce::int64> seen;
    bool unique = true;
    const std::function<void (const juce::ValueTree&)> visit = [&] (const juce::ValueTree& n) {
        if (n.hasProperty (ids::id))
            unique = unique && seen.insert ((juce::int64) n.getProperty (ids::id)).second;
        for (const auto& c : n)
            visit (c);
    };
    visit (p.f.model.root);
    check (unique, "all ids are unique with takes in the tree");

    // the engine plays what is shown
    p.f.engine.rebuildGraph();
    check (p.f.engine.getPublishedGeneration() > 0, "the graph with the clip is published");

    // the first pass starting in the middle of the loop: the clip still covers the whole loop
    {
        Bench q;
        const std::vector<EngineEvent> log2 { anchorAt (0.0, 36.0), anchorAt (4.0, 32.0, true) };
        q.record (0.0, 10.0, log2, { on (q.a.id(), 1.0, 60), off (q.a.id(), 2.0, 60), on (q.a.id(), 5.0, 62), off (q.a.id(), 6.0, 62) });
        const auto c = q.a.arrangementClips();
        check (c.size() == 1 && near (c[0].start(), 32.0) && near (c[0].length(), 8.0) && arrangement::takesOf (c[0]).size() == 2,
               "a first pass that starts mid-loop: the clip spans the loop and has two takes");
        check (c.size() == 1 && c[0].activeTake() == 2 && hasNote (notesOf (c[0]), 1.0, 1.0, 62), "the second pass is on top");
        if (c.size() == 1)
        {
            q.f.model.undo.beginNewTransaction ("take");
            arrangement::switchTake (q.f.model, c[0], 1);
            check (hasNote (notesOf (c[0]), 5.0, 1.0, 60), "the first pass keeps its place in the loop (36 = 4 into the clip + 1)");
        }
    }

    // a seek during the recording ends the clip; the next stretch is a clip of its own
    {
        Bench q;
        const std::vector<EngineEvent> log3 { anchorAt (0.0, 0.0), anchorAt (4.0, 20.0) };
        q.record (0.0, 8.0, log3, { on (q.a.id(), 1.0, 60), off (q.a.id(), 2.0, 60), on (q.a.id(), 5.0, 62), off (q.a.id(), 6.0, 62) });
        const auto c = q.a.arrangementClips();
        check (c.size() == 2 && near (c[0].start(), 0.0) && near (c[0].length(), 4.0) && near (c[1].start(), 20.0) && near (c[1].length(), 4.0),
               "moving the cursor during the recording makes two clips, not takes");
    }
}

//==============================================================================
void testPunch()
{
    std::printf ("[arrangement test] punch-in / punch-out: only the loop brace is recorded\n");
    const auto run = [] (bool punchIn, bool punchOut, ArrangementRecorder::Summary* out = nullptr) {
        auto p = std::make_unique<Bench>();
        p->f.model.setArrangementLoop (16.0, 24.0, false);
        p->f.model.setPunchIn (punchIn);
        p->f.model.setPunchOut (punchOut);
        const auto id = p->a.id();
        // the song runs from 8 to 40; notes at 10, 17 (off at 18), 23.5 (off at 24.5), a note held from 15.5 to 16.5, one at 30
        const auto s = p->record (0.0, 32.0, { anchorAt (0.0, 8.0) },
                                  { on (id, 2.0, 60), off (id, 3.0, 60), on (id, 7.5, 61), off (id, 8.5, 61), on (id, 9.0, 62), off (id, 10.0, 62),
                                    on (id, 15.5, 63), off (id, 16.5, 63), on (id, 22.0, 64), off (id, 23.0, 64) });
        if (out != nullptr)
            *out = s;
        return p;
    };

    {
        auto p = run (true, true);
        const auto c = p->a.arrangementClips();
        check (c.size() == 1 && near (c[0].start(), 16.0) && near (c[0].end(), 24.0), "punch-in and -out: the clip is exactly the brace");
        if (c.size() == 1)
        {
            const auto notes = notesOf (c[0]);
            check (notes.size() == 2 && hasNote (notes, 1.0, 1.0, 62) && hasNote (notes, 7.5, 0.5, 63),
                   "the note before the brace and the one held into it are not recorded; the note held out of it ends at the brace");
        }
    }
    {
        auto p = run (true, false);
        const auto c = p->a.arrangementClips();
        check (c.size() == 1 && near (c[0].start(), 16.0) && near (c[0].end(), 40.0), "punch-in only: from the start of the brace to the end of the recording");
        if (c.size() == 1)
        {
            const auto notes = notesOf (c[0]);
            check (notes.size() == 3 && hasNote (notes, 1.0, 1.0, 62) && hasNote (notes, 7.5, 1.0, 63) && hasNote (notes, 14.0, 1.0, 64),
                   "and everything after the start of the brace, the note held out of it included");
        }
    }
    {
        auto p = run (false, true);
        const auto c = p->a.arrangementClips();
        check (c.size() == 1 && near (c[0].start(), 8.0) && near (c[0].end(), 24.0), "punch-out only: from the start of the recording to the end of the brace");
        if (c.size() == 1)
        {
            const auto notes = notesOf (c[0]);
            check (notes.size() == 4 && hasNote (notes, 2.0, 1.0, 60) && hasNote (notes, 15.5, 0.5, 63), "with the notes before it, and the last one cut at the end of the brace");
        }
    }
    {
        auto p = run (false, false);
        const auto c = p->a.arrangementClips();
        check (c.size() == 1 && near (c[0].start(), 8.0) && near (c[0].end(), 40.0), "no punch: everything");
    }
    {
        // a recording that never reaches the brace leaves the project alone
        Bench p;
        p.f.model.setArrangementLoop (100.0, 108.0, false);
        p.f.model.setPunchIn (true);
        p.f.model.setPunchOut (true);
        p.f.model.undo.clearUndoHistory();
        const auto before = xmlOf (p.f.model);
        const auto s = p.record (0.0, 8.0, { anchorAt (0.0, 0.0) }, { on (p.a.id(), 1.0, 60), off (p.a.id(), 2.0, 60) });
        check (! s.wrote && xmlOf (p.f.model) == before && ! p.f.model.undo.canUndo(), "nothing inside the brace: nothing written, no undo step");
    }
}

//==============================================================================
void testOverdub()
{
    std::printf ("[arrangement test] overdub on the timeline: notes join the clip under the cursor instead of replacing it\n");
    Bench p;
    p.f.model.undo.beginNewTransaction ("setup");
    auto clip = arrangement::addEmptyClip (p.f.model, p.a, 0.0, 8.0);
    clip.clip().setNotes ({ { 0.0, 1.0, 40, 100 } }, &p.f.model.undo);
    auto looped = arrangement::addEmptyClip (p.f.model, p.b, 0.0, 8.0);
    looped.clip().setLengthBeats (4.0, &p.f.model.undo);
    looped.clip().setNotes ({ { 0.0, 1.0, 41, 100 } }, &p.f.model.undo);
    p.f.model.undo.clearUndoHistory();
    p.f.model.setOverdub (true);

    const auto ia = p.a.id(), ib = p.b.id();
    oneStep (p.f.model, "overdub", [&] {
        p.record (0.0, 12.0, { anchorAt (0.0, 0.0) },
                  { on (ia, 2.0, 60), off (ia, 3.0, 60), on (ia, 6.5, 62), off (ia, 7.5, 62), on (ia, 9.0, 64), off (ia, 10.0, 64), // 9..10 is past the clip
                    on (ib, 6.0, 70), off (ib, 7.0, 70) });
    });
    const auto ca = p.a.arrangementClips();
    check (ca.size() == 2 && near (ca[0].start(), 0.0) && near (ca[0].length(), 8.0), "the clip under the notes stays as long as it was");
    if (ca.size() == 2)
    {
        const auto notes = notesOf (ca[0]);
        check (notes.size() == 3 && hasNote (notes, 0.0, 1.0, 40) && hasNote (notes, 2.0, 1.0, 60) && hasNote (notes, 6.5, 1.0, 62),
               "the old note stays, the new ones are added at their place");
        check (near (ca[1].start(), 8.0) && near (ca[1].end(), 12.0) && hasNote (notesOf (ca[1]), 1.0, 1.0, 64),
               "what was played where no clip lies makes a clip of its own, only in the gap");
    }
    const auto cb = p.b.arrangementClips();
    check (cb.size() == 1 && hasNote (notesOf (cb[0]), 2.0, 1.0, 70), "in a clip that repeats its content the note goes to the content (6 = 2 into the second pass)");
}

//==============================================================================
// The clips of a Session View recording: copies of the launched clips with the right phase.
void testSessionLog()
{
    std::printf ("[arrangement test 7] session -> arrangement: scene launches become clips of the right length with copies of the clips\n");
    Bench p;
    p.f.model.undo.beginNewTransaction ("setup");
    const auto putClip = [&] (const TrackRef& t, int scene, double length, std::vector<Note> notes, const juce::String& name) {
        auto c = p.f.model.createClip (t, scene, length, name);
        c.setNotes (notes, &p.f.model.undo);
    };
    p.f.model.sceneAt (0).setName ("Intro", &p.f.model.undo);
    p.f.model.sceneAt (1).setName ("Drop", &p.f.model.undo);
    putClip (p.a, 0, 16.0, { { 0.0, 1.0, 48, 100 }, { 8.0, 1.0, 50, 100 } }, "Intro A");
    putClip (p.a, 1, 32.0, { { 0.0, 1.0, 52, 100 }, { 16.0, 1.0, 53, 100 } }, "Drop A");
    putClip (p.b, 0, 16.0, { { 0.0, 1.0, 60, 100 } }, "Intro B");
    putClip (p.b, 1, 32.0, { { 0.0, 1.0, 62, 100 } }, "Drop B");
    p.f.model.undo.clearUndoHistory();
    const auto ia = p.a.id(), ib = p.b.id();

    // Intro for 8 bars (32 beats), then Drop for 8 bars; the song is at beat 0 when the recording starts
    const std::vector<EngineEvent> log { anchorAt (0.0, 0.0),
                                         launchAt (ia, 0, 0.0, 0.0, 0.0),   launchAt (ib, 0, 0.0, 0.0, 0.0),
                                         launchAt (ia, 1, 32.0, 32.0, 32.0), launchAt (ib, 1, 32.0, 32.0, 32.0) };
    oneStep (p.f.model, "session recording", [&] { p.record (0.0, 64.0, log, {}); });

    for (const auto& [track, introName, dropName, introPitch, dropPitch] :
         { std::tuple { p.a, "Intro A", "Drop A", 48, 52 }, std::tuple { p.b, "Intro B", "Drop B", 60, 62 } })
    {
        const auto clips = track.arrangementClips();
        check (clips.size() == 2, juce::String ("two clips on ") + introName);
        if (clips.size() != 2)
            continue;
        check (near (clips[0].start(), 0.0) && near (clips[0].length(), 32.0) && clips[0].loop() && near (clips[0].offset(), 0.0),
               juce::String (introName) + ": 8 bars of the looping intro clip from the start");
        check (near (clips[1].start(), 32.0) && near (clips[1].length(), 32.0), juce::String (dropName) + ": 8 bars of the drop");
        check (clips[0].clip().name() == introName && clips[1].clip().name() == dropName, "each is a copy of its clip");
        check (hasNote (notesOf (clips[0]), 0.0, 1.0, introPitch) && hasNote (notesOf (clips[1]), 0.0, 1.0, dropPitch), "with the notes of the clip");
        check (clips[0].clip().id() != track.clipAt (0).id() && clips[1].clip().id() != track.clipAt (1).id(), "not the slot clips themselves: own copies, own ids");
    }

    // a clip stopped before the next one: the clip is as long as it played
    {
        Bench q;
        q.f.model.undo.beginNewTransaction ("setup");
        auto c = q.f.model.createClip (q.a, 0, 16.0, "X");
        c.setNotes ({ { 0.0, 1.0, 48, 100 } }, &q.f.model.undo);
        q.f.model.undo.clearUndoHistory();
        q.record (0.0, 64.0, { anchorAt (0.0, 0.0), launchAt (q.a.id(), 0, 8.0, 8.0, 8.0), stopAt (q.a.id(), 0, 40.0, 40.0) }, {});
        const auto cl = q.a.arrangementClips();
        check (cl.size() == 1 && near (cl[0].start(), 8.0) && near (cl[0].length(), 32.0), "launched at 8 and stopped at 40: a clip of 32 beats");
    }

    // a clip that already played when the recording began is entered at its phase
    {
        Bench q;
        q.f.model.undo.beginNewTransaction ("setup");
        auto c = q.f.model.createClip (q.a, 0, 16.0, "X");
        c.setNotes ({ { 0.0, 1.0, 48, 100 }, { 10.0, 1.0, 50, 100 } }, &q.f.model.undo);
        q.f.model.undo.clearUndoHistory();
        // it began at monotonic beat -6 (before the recording); the recording starts at 4 (song 4)
        q.record (4.0, 20.0, { anchorAt (4.0, 4.0), launchAt (q.a.id(), 0, 0.0, 0.0, -6.0) }, {});
        const auto cl = q.a.arrangementClips();
        check (cl.size() == 1 && near (cl[0].start(), 4.0) && near (cl[0].length(), 16.0) && near (cl[0].offset(), 10.0),
               "10 beats into the clip when the recording began: offset 10 (" + juce::String (cl.empty() ? -1.0 : cl[0].offset()) + ")");
        check (cl.size() == 1 && cl[0].loop(), "and it keeps looping");
    }

    // a one-shot is not longer than its content
    {
        Bench q;
        q.f.model.undo.beginNewTransaction ("setup");
        auto c = q.f.model.createClip (q.a, 0, 8.0, "Once");
        c.setLoopEnabled (false, &q.f.model.undo);
        c.setNotes ({ { 0.0, 1.0, 48, 100 } }, &q.f.model.undo);
        q.f.model.undo.clearUndoHistory();
        q.record (0.0, 32.0, { anchorAt (0.0, 0.0), launchAt (q.a.id(), 0, 4.0, 4.0, 4.0) }, {});
        const auto cl = q.a.arrangementClips();
        check (cl.size() == 1 && near (cl[0].length(), 8.0) && ! cl[0].loop(), "a one-shot clip is as long as it is, not as long as the recording went on");
    }

    // the loop of the arrangement during a session recording: the second pass is on top of the first
    {
        Bench q;
        q.f.model.undo.beginNewTransaction ("setup");
        auto c = q.f.model.createClip (q.a, 0, 16.0, "L");
        c.setNotes ({ { 0.0, 1.0, 48, 100 } }, &q.f.model.undo);
        q.f.model.undo.clearUndoHistory();
        q.record (0.0, 16.0, { anchorAt (0.0, 0.0), launchAt (q.a.id(), 0, 0.0, 0.0, 0.0), anchorAt (8.0, 0.0, true) }, {});
        const auto cl = q.a.arrangementClips();
        check (cl.size() == 1 && near (cl[0].start(), 0.0) && near (cl[0].length(), 8.0) && near (cl[0].offset(), 8.0),
               "two passes over the same 8 beats of song: the later pass wins, entered 8 beats into the clip");
    }

    // punch cuts a session recording to the brace
    {
        Bench q;
        q.f.model.undo.beginNewTransaction ("setup");
        auto c = q.f.model.createClip (q.a, 0, 16.0, "P");
        c.setNotes ({ { 0.0, 1.0, 48, 100 } }, &q.f.model.undo);
        q.f.model.undo.clearUndoHistory();
        q.f.model.setArrangementLoop (8.0, 24.0, false);
        q.f.model.setPunchIn (true);
        q.f.model.setPunchOut (true);
        q.record (0.0, 64.0, { anchorAt (0.0, 0.0), launchAt (q.a.id(), 0, 0.0, 0.0, 0.0) }, {});
        const auto cl = q.a.arrangementClips();
        check (cl.size() == 1 && near (cl[0].start(), 8.0) && near (cl[0].end(), 24.0) && near (cl[0].offset(), 8.0), "punch: only the brace, entered at the right phase");
    }
}

//==============================================================================
// An engine driven by hand, with a log instrument on every track.
class MidiLog final : public juce::AudioProcessor
{
public:
    MidiLog() : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)) {}

    using AudioProcessor::processBlock;
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        for (const auto meta : midi)
            if (meta.numBytes == 3 && (meta.data[0] & 0xF0) == 0x90 && meta.data[2] > 0)
            {
                const int pitch = meta.data[1] & 0x7F;
                counts[(size_t) pitch].fetch_add (1, std::memory_order_relaxed);
            }
        buffer.clear();
    }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "MidiLog"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    int countNoteOns (int pitch) const { return counts[(size_t) pitch].load(); }

    std::array<std::atomic<int>, 128> counts {};
};

struct Sched
{
    double beat;
    uint8_t b0, b1, b2;
};
Sched noteOn (double beat, int pitch, int vel = 100) { return { beat, 0x90, (uint8_t) pitch, (uint8_t) vel }; }
Sched noteOff (double beat, int pitch) { return { beat, 0x80, (uint8_t) pitch, 0 }; }

struct Rig
{
    explicit Rig (bool logDevice = true, int scenes = 2, int numTracks = 1)
        : dev (kBlock, kRate), runner (f.engine, kBlock), recorder (f.engine, f.model)
    {
        if (logDevice)
            f.engine.setDeviceFactory ([this] (const DeviceRef&) -> std::unique_ptr<juce::AudioProcessor> {
                auto p = std::make_unique<MidiLog>();
                logs.push_back (p.get());
                return p;
            });
        for (int i = 0; i < scenes; ++i)
            f.model.insertScene (-1);
        for (int i = 0; i < numTracks; ++i)
            tracks.push_back (addTrack());
        if (! tracks.empty())
            f.model.setSelectedTrack (tracks[0].id());
        f.model.setCountInBars (1);
        gClockMs = 1000.0;
        f.engine.audioDeviceAboutToStart (&dev);
        f.engine.rebuildGraphIfNeeded();
        f.engine.setClockForTesting (&scriptedClock);
        runner.run (1);
        f.model.undo.clearUndoHistory();
    }
    ~Rig() { f.engine.audioDeviceStopped(); }

    TrackRef addTrack()
    {
        auto t = f.model.addTrack ("midi");
        f.model.addDevice (t, "prisma", "Log");
        t.setArm (true, nullptr);
        f.engine.rebuildGraphIfNeeded();
        return t;
    }

    MidiLog* logOf (const TrackRef& t) const
    {
        for (auto* l : logs)
            if (f.engine.findProcessor (t.device (0).id()) == l)
                return l;
        return nullptr;
    }

    void block (const std::vector<std::pair<int, Sched>>& events = {})
    {
        for (const auto& [offset, s] : events)
        {
            const uint8_t bytes[3] { s.b0, s.b1, s.b2 };
            f.engine.pushLiveMidi (bytes, 3, true, gClockMs + offset * 1000.0 / kRate + 0.0001);
        }
        gClockMs += kBlock * 1000.0 / kRate;
        runner.run (1);
        if (++blocks % 6 == 0)
            recorder.poll();
    }

    // Runs until the transport reaches `beat`. The events of `schedule` happen at the monotonic beats they name, as the
    // player would hear them (the recorder has to take the latency off again).
    void runTo (double beat, const std::vector<Sched>& schedule = {})
    {
        const int comp = f.engine.getRecordCompensationSamples();
        while (f.engine.getPositionBeats() < beat)
        {
            const double blockStart = f.engine.getPositionBeats() * kSpb;
            std::vector<std::pair<int, Sched>> now;
            for (const auto& s : schedule)
            {
                const long at = std::lround (s.beat * kSpb + comp - blockStart);
                if (at >= 0 && at < kBlock)
                    now.push_back ({ (int) at, s });
            }
            block (now);
            if (++guard > 2000000)
                break;
        }
    }
    void settle (int n = 30)
    {
        for (int i = 0; i < n; ++i)
            block();
    }

    Fixture f;
    FakeDevice dev;
    Runner runner;
    SessionRecorder recorder;
    std::vector<TrackRef> tracks;
    std::vector<MidiLog*> logs;
    int blocks = 0, guard = 0;
};

//==============================================================================
void testEngineLog()
{
    std::printf ("[arrangement test] the engine logs song jumps and launches while a recording runs, and nothing before\n");
    Rig r (false, 2);
    r.f.model.setCountInBars (0);
    r.f.model.setArrangementLoop (4.0, 8.0, true);
    r.f.model.setArrangementPlayStart (4.0);
    r.f.model.undo.beginNewTransaction ("clip");
    auto c = r.f.model.createClip (r.tracks[0], 0, 4.0, "c");
    c.setNotes ({ { 0.0, 1.0, 60, 100 } }, &r.f.model.undo);
    r.f.engine.rebuildGraph();
    auto& engine = r.f.engine;

    // (the Rig's recorder is not polled here: it would take the log away)
    engine.play();
    r.runner.run (12);
    std::vector<EngineEvent> log;
    engine.popEngineEvents (log);
    check (log.empty() && engine.getArrangementRecordState() == AudioEngine::arrangementIdle, "no log without a recording");

    check (engine.startArrangementRecord() && engine.launchClip (r.tracks[0].id(), 0), "arm, then launch the clip");
    while (engine.getPositionBeats() < 14.0)
        r.runner.run (1);
    engine.popEngineEvents (log);
    int anchors = 0, launches = 0, stops = 0;
    std::vector<double> jumps;
    for (const auto& e : log)
    {
        anchors += e.type == EngineEvent::Type::anchor ? 1 : 0;
        launches += e.type == EngineEvent::Type::launch ? 1 : 0;
        stops += e.type == EngineEvent::Type::stop ? 1 : 0;
        if (e.type == EngineEvent::Type::loopJump)
        {
            jumps.push_back (e.monoBeat);
            check (near (e.beat, 4.0, 1.0e-9), "a loop jump lands on the loop start");
        }
    }
    check (anchors == 1 && log.front().type == EngineEvent::Type::anchor && near (log.front().beat - log.front().monoBeat, 4.0, 1.0e-6),
           "one anchor at the start: the song runs 4 ahead of the monotonic clock");
    check (jumps.size() == 3 && near (jumps[0], 4.0, 1.0e-9) && near (jumps[1], 8.0, 1.0e-9) && near (jumps[2], 12.0, 1.0e-9),
           "loop jumps at the monotonic beats 4, 8 and 12, exactly (found " + juce::String ((int) jumps.size()) + ")");
    check (launches == 1 && stops == 0, "and one launch");
    for (const auto& e : log)
        if (e.type == EngineEvent::Type::launch)
            check (e.trackId == r.tracks[0].id() && e.slot == 0 && near (e.monoBeat, 4.0, 1.0e-9) && near (e.clipStartMono, 4.0, 1.0e-9)
                       && near (e.beat, 4.0, 1.0e-9),
                   "the launch happened at the next bar, on the loop start, in the song as well");
    check (engine.getArrangementRecordState() == AudioEngine::arrangementRunning, "the recording runs");

    check (engine.stopArrangementRecord(), "stop");
    r.runner.run (2);
    check (engine.getArrangementRecordState() == AudioEngine::arrangementFinished && near (engine.getArrangementRecordEndBeat(), 14.0, 0.1), "it finished at the stop");
    engine.acknowledgeArrangementRecord();
    engine.stopArrangementRecord();
    r.runner.run (50);
    log.clear();
    engine.popEngineEvents (log);
    check (log.empty() && engine.getArrangementRecordState() == AudioEngine::arrangementIdle && engine.getEngineEventDrops() == 0, "nothing is logged after it");
}

void testLiveRecordingThroughEngine()
{
    std::printf ("[arrangement test] recording through the engine: count-in, song position, notes in place, one undo step\n");
    Rig r (true, 2);
    r.f.model.setArrangementPlayStart (32.0); // 9.1.1
    const auto id = r.tracks[0].id();
    auto& arr = r.recorder.arrangement();
    const auto before = xmlOf (r.f.model);

    check (arr.start() && arr.isActive() && arr.isWaiting(), "armed");
    const std::vector<Sched> played { noteOn (0.5, 60), noteOff (1.5, 60), noteOn (3.0, 64), noteOff (3.5, 64), noteOn (6.0, 67), noteOff (7.0, 67) };
    r.runTo (0.25, played);
    check (r.f.engine.isCountingIn() == false && arr.isActive() && ! arr.isWaiting(), "after the count-in the recording runs");
    r.runTo (8.0, played);
    ArrangementRecorder::Progress progress;
    check (arr.getProgress (id, progress) && ! progress.waiting && near (progress.startBeat, 32.0, 0.05) && progress.endBeat > 38.0
               && ! progress.notes.empty(),
           "while it runs the lanes can ask for the growing clip (start 32, end " + juce::String (progress.endBeat, 2) + ")");
    arr.stop();
    r.runTo (9.0, played);
    r.settle();
    check (! arr.isActive(), "the recording is over");

    const auto clips = r.tracks[0].arrangementClips();
    check (clips.size() == 1, "one clip");
    if (clips.size() == 1)
    {
        check (near (clips[0].start(), 32.0, 1.0e-9), "it starts at the play start, 9.1.1 = beat 32");
        check (clips[0].length() > 7.9 && clips[0].length() < 8.2, "and lasts until the stop (" + juce::String (clips[0].length(), 3) + " beats)");
        const auto notes = notesOf (clips[0]);
        check (notes.size() == 3 && hasNote (notes, 0.5, 1.0, 60, 0.01) && hasNote (notes, 3.0, 0.5, 64, 0.01) && hasNote (notes, 6.0, 1.0, 67, 0.01),
               "the notes are where they were played, counted from the start of the song position (compensation taken off)");
    }
    r.f.model.undo.undo();
    check (xmlOf (r.f.model) == before && ! r.f.model.undo.canUndo(), "the whole recording is one undo step");
    r.f.model.undo.redo();
    check (r.tracks[0].numArrangementClips() == 1, "and redo brings it back");
}

void testLoopTakesThroughEngine()
{
    std::printf ("[arrangement test] loop recording through the engine: takes per pass, sample-exact jumps, a note across the jump\n");
    Rig r (true, 2);
    r.f.model.setArrangementLoop (32.0, 40.0, true);
    r.f.model.setArrangementPlayStart (32.0);
    auto& arr = r.recorder.arrangement();
    const std::vector<Sched> played { noteOn (1.0, 60), noteOff (2.0, 60),   noteOn (9.5, 62),  noteOff (10.5, 62),
                                      noteOn (15.5, 65), noteOff (16.5, 65), noteOn (17.0, 67), noteOff (18.0, 67) };
    check (arr.start(), "armed");
    r.runTo (20.0, played);
    arr.stop();
    r.runTo (21.0, played);
    r.settle();
    const auto clips = r.tracks[0].arrangementClips();
    check (clips.size() == 1 && near (clips[0].start(), 32.0) && near (clips[0].length(), 8.0), "one clip over the loop brace");
    if (clips.size() != 1)
        return;
    const auto takes = arrangement::takesOf (clips[0]);
    check (takes.size() == 3 && clips[0].activeTake() == 3, "three passes, three takes, the last on top");
    r.f.model.undo.beginNewTransaction ("switch");
    arrangement::switchTake (r.f.model, clips[0], 2);
    const auto notes = notesOf (clips[0]);
    check (hasNote (notes, 1.5, 1.0, 62, 0.01) && hasNote (notes, 7.5, 0.5, 65, 0.01) && notes.size() == 2,
           "the note across the loop jump belongs to the pass it began in, cut at the loop end");
    r.f.model.undo.undo();
}

void testPunchThroughEngine()
{
    std::printf ("[arrangement test] punch-in / punch-out through the engine: the recording ends by itself at the end of the brace\n");
    Rig r (true, 2);
    r.f.model.setArrangementLoop (32.0, 40.0, false);
    r.f.model.setArrangementPlayStart (28.0);
    r.f.model.setPunchIn (true);
    r.f.model.setPunchOut (true);
    auto& arr = r.recorder.arrangement();
    const std::vector<Sched> played { noteOn (0.5, 60), noteOff (1.0, 60), noteOn (5.0, 62), noteOff (6.0, 62), noteOn (13.0, 64), noteOff (14.0, 64) };
    check (arr.start(), "armed");
    r.runTo (14.0, played);
    r.settle();
    check (! arr.isActive(), "the recording stopped itself after the brace");
    check (r.f.engine.isPlaying(), "the transport went on");
    const auto clips = r.tracks[0].arrangementClips();
    check (clips.size() == 1 && near (clips[0].start(), 32.0) && near (clips[0].end(), 40.0), "the clip is the brace");
    if (clips.size() == 1)
        check (notesOf (clips[0]).size() == 1 && hasNote (notesOf (clips[0]), 1.0, 1.0, 62, 0.01), "only the note inside it");
}

void testSessionThroughEngine()
{
    std::printf ("[arrangement test 7] session -> arrangement through the engine: Intro 8 bars, Drop 8 bars, then it plays without the session\n");
    Rig r (true, 2, 2);
    r.f.model.setCountInBars (0);
    r.f.model.undo.beginNewTransaction ("setup");
    const auto putClip = [&] (const TrackRef& t, int scene, double length, std::vector<Note> notes) {
        auto c = r.f.model.createClip (t, scene, length, "c" + juce::String (scene));
        c.setNotes (notes, &r.f.model.undo);
    };
    putClip (r.tracks[0], 0, 16.0, { { 0.0, 1.0, 48, 100 }, { 8.0, 1.0, 50, 100 } });
    putClip (r.tracks[0], 1, 32.0, { { 0.0, 1.0, 52, 100 }, { 16.0, 1.0, 53, 100 } });
    putClip (r.tracks[1], 0, 16.0, { { 0.0, 1.0, 60, 100 } });
    putClip (r.tracks[1], 1, 32.0, { { 0.0, 1.0, 62, 100 } });
    for (auto& t : r.tracks)
        t.setArm (false, &r.f.model.undo);
    r.f.model.undo.clearUndoHistory();
    r.f.engine.rebuildGraph();
    const auto before = xmlOf (r.f.model);

    auto& arr = r.recorder.arrangement();
    check (arr.start(), "armed");
    r.f.engine.launchScene (0);
    r.runTo (31.0);
    r.f.engine.launchScene (1);
    r.runTo (64.0);
    arr.stop();
    r.runTo (66.0);
    r.settle();
    check (! arr.isActive(), "done");

    for (size_t i = 0; i < 2; ++i)
    {
        const auto clips = r.tracks[i].arrangementClips();
        check (clips.size() == 2, "two clips on track " + juce::String ((int) i + 1) + " (found " + juce::String ((int) clips.size()) + ")");
        if (clips.size() != 2)
            continue;
        check (near (clips[0].start(), 0.0) && near (clips[0].length(), 32.0) && clips[0].loop(), "the intro: from 0, 8 bars, looping");
        check (near (clips[1].start(), 32.0) && near (clips[1].length(), 32.0, 0.05), "the drop: from 32, 8 bars");
        const auto copy = notesOf (clips[0]), slot = r.tracks[i].clipAt (0).notes();
        check (copy.size() == slot.size() && copy[0].pitch == slot[0].pitch, "copies of the clips");
    }
    r.f.model.undo.undo();
    check (xmlOf (r.f.model) == before, "all of it is one undo step");
    r.f.model.undo.redo();

    // now play the arrangement alone, in a project that has the recorded clips and no running session
    Rig r2 (true, 0, 0);
    r2.f.model.adopt (r.f.model.root.createCopy());
    r2.f.engine.rebuildGraph();
    r2.settle (2);
    auto* logA = r2.logOf (r2.f.model.trackAt (0));
    auto* logB = r2.logOf (r2.f.model.trackAt (1));
    check (logA != nullptr && logB != nullptr, "the second engine has the tracks");
    if (logA == nullptr || logB == nullptr)
        return;
    r2.f.engine.play();
    r2.runTo (66.0);
    r2.settle (4);
    check (logA->countNoteOns (48) == 2 && logA->countNoteOns (50) == 2 && logA->countNoteOns (52) == 1 && logA->countNoteOns (53) == 1,
           "the track plays the recorded structure without the session (48 x2, 50 x2, 52, 53)");
    check (logB->countNoteOns (60) == 2 && logB->countNoteOns (62) == 1, "the other track too");
}

void testRecordingReplacesAndOverdubPlays()
{
    std::printf ("[arrangement test] while a take is recorded the old clip is silent where it is replaced; with OVR it keeps playing\n");
    for (const bool overdub : { false, true })
    {
        Rig r (true, 2);
        r.f.model.setCountInBars (0);
        r.f.model.undo.beginNewTransaction ("old clip");
        auto old = arrangement::addEmptyClip (r.f.model, r.tracks[0], 0.0, 16.0);
        std::vector<Note> notes;
        for (int i = 0; i < 16; ++i)
            notes.push_back ({ (double) i + 0.01, 0.5, 50, 100 });
        old.clip().setNotes (notes, &r.f.model.undo);
        r.f.model.undo.clearUndoHistory();
        r.f.model.setOverdub (overdub);
        r.f.engine.rebuildGraph();
        r.settle (2);
        auto* log = r.logOf (r.tracks[0]);
        auto& arr = r.recorder.arrangement();
        check (arr.start(), "armed");
        r.runTo (7.0);
        const int sounded = log->countNoteOns (50);
        arr.stop();
        r.settle (10);
        if (overdub)
            check (sounded >= 6, "overdub: the old clip plays during the recording (" + juce::String (sounded) + " notes)");
        else
            check (sounded == 0, "replace: the old clip is silent during the recording (" + juce::String (sounded) + " notes)");
    }
}

void testRealtimeSafety()
{
    std::printf ("[arrangement test] the audio thread allocates nothing while an arrangement recording runs (loop, launches, live input)\n");
    Rig r (false, 2, 2);
    r.f.model.setCountInBars (0);
    r.f.model.setMetronome (true);
    r.f.model.setArrangementLoop (0.0, 8.0, true);
    r.f.model.undo.beginNewTransaction ("setup");
    for (auto& t : r.tracks)
        for (int s = 0; s < 2; ++s)
        {
            auto c = r.f.model.createClip (t, s, 8.0, "c");
            c.setNotes ({ { 0.0, 1.0, 48 + s, 100 }, { 4.0, 1.0, 55 + s, 100 } }, &r.f.model.undo);
        }
    r.f.engine.rebuildGraph();
    auto& arr = r.recorder.arrangement();
    arr.start();
    r.block();

    std::vector<Sched> played;
    for (int i = 0; i < 1200; ++i) // two minutes at 120 BPM
    {
        const double beat = 1.0 + i * 0.2;
        played.push_back (noteOn (beat, 36 + (i * 7) % 48));
        played.push_back (noteOff (beat + 0.1, 36 + (i * 7) % 48));
    }
    daw::rt::resetViolations();
    const int comp = r.f.engine.getRecordCompensationSamples();
    size_t first = 0;
    int guard = 0, nextLaunch = 4;
    while (r.f.engine.getPositionBeats() < 240.0 && ++guard < 400000)
    {
        const double blockStart = r.f.engine.getPositionBeats() * kSpb;
        std::vector<std::pair<int, Sched>> now;
        while (first < played.size() && played[first].beat * kSpb + comp < blockStart)
            ++first;
        for (size_t k = first; k < played.size() && played[k].beat * kSpb + comp < blockStart + kBlock; ++k)
            now.push_back ({ (int) std::lround (played[k].beat * kSpb + comp - blockStart), played[k] });
        if (r.f.engine.getPositionBeats() >= nextLaunch)
        {
            r.f.engine.launchScene ((nextLaunch / 8) % 2);
            nextLaunch += 8;
        }
        r.block (now);
    }
    const auto violations = daw::rt::violationCount();
    check (r.f.engine.getPositionBeats() >= 240.0, "two minutes ran");
    std::printf ("    %lld heap operations on the audio thread, %u queue drops, %u log drops\n", (long long) violations,
                 (unsigned) r.f.engine.getInputEventDrops(), (unsigned) r.f.engine.getEngineEventDrops());
    check (violations == 0, "no allocation or deallocation on the audio thread during an arrangement recording");
    check (r.f.engine.getInputEventDrops() == 0 && r.f.engine.getEngineEventDrops() == 0, "no event of either queue was dropped");
    arr.stop();
    r.settle();
    check (r.tracks[0].numArrangementClips() > 0, "and the recording produced clips");
}

//==============================================================================
juce::File tempFolder (const juce::String& tag)
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("daw-test-" + tag + "-" + juce::String (juce::Random::getSystemRandom().nextInt (1000000)));
    dir.createDirectory();
    return dir;
}

std::set<juce::int64> allIds (const juce::ValueTree& root)
{
    std::set<juce::int64> out;
    const std::function<void (const juce::ValueTree&)> visit = [&] (const juce::ValueTree& n) {
        if (n.hasProperty (ids::id))
            out.insert ((juce::int64) n.getProperty (ids::id));
        for (const auto& c : n)
            visit (c);
    };
    visit (root);
    return out;
}

void testTakeTrack()
{
    std::printf ("[arrangement test 9] taking a track from a saved version: same content, new ids, no conflicts\n");
    const auto root = tempFolder ("taketrack");
    juce::TemporaryFile settingsFile (".settings");
    Settings settings (settingsFile.getFile());
    library::UserLibrary lib (root.getChildFile ("Library"));
    ProjectModel model (ProjectModel::createEmpty());
    AudioEngine engine (settings, false);
    engine.setModel (&model);
    project::ProjectSession session (engine, model, settings, lib, root.getChildFile ("Music"));
    session.newProject();

    // The old project: four scenes, a "Reese" track with a Prisma preset, session clips in scenes 1 and 4, arrangement clips with a take
    model.undo.beginNewTransaction ("build");
    while (model.numScenes() < 4)
        model.insertScene (-1);
    auto reese = addPrismaTrack (model, 3);
    reese.setName ("Reese", &model.undo);
    reese.setVolumeDb (-4.5f, &model.undo);
    auto c1 = model.createClip (reese, 0, 8.0, "Riff");
    c1.setNotes ({ { 0.0, 1.0, 36, 100 }, { 2.0, 1.0, 39, 90 } }, &model.undo);
    auto c4 = model.createClip (reese, 3, 4.0, "Break");
    c4.setNotes ({ { 1.0, 0.5, 41, 80 } }, &model.undo);
    auto a1 = arrangement::addEmptyClip (model, reese, 16.0, 8.0, "Verse bass");
    a1.clip().setNotes ({ { 0.0, 2.0, 36, 100 } }, &model.undo);
    arrangement::addRecordedClip (model, reese, 32.0, 8.0, { { { { 1.0, 1.0, 43, 100 } }, {} }, { { { 2.0, 1.0, 45, 100 } }, {} } }, 1, "Take");
    const int reeseId = (int) reese.id();
    const auto reeseDeviceState = reese.device (0).stateBase64();
    session.saveAs (root.getChildFile ("Projects/Night Drive Project"));
    session.flush();
    const auto versions = session.versions();
    check (! versions.empty(), "the project has a saved version");
    const int saved = versions.back().number;

    // The project moves on: the track is deleted, other tracks and only two scenes remain
    model.undo.beginNewTransaction ("move on");
    model.deleteTrack (reese.id());
    while (model.numScenes() > 2)
        model.deleteScene (model.numScenes() - 1);
    auto other = model.addTrack ("midi");
    other.setName ("Lead", &model.undo);
    auto oc = model.createClip (other, 0, 4.0, "Lead clip");
    oc.setNotes ({ { 0.0, 1.0, 72, 100 } }, &model.undo);
    const int tracksBefore = model.numTracks();
    const int scenesBefore = model.numScenes();
    const auto idsBefore = allIds (model.root);

    juce::String error;
    const auto older = session.loadVersionModel (saved, error);
    check (older != nullptr && error.isEmpty(), "the version loads into memory");
    check (engine.getModel() == &model, "and the engine still plays the open project, not the version");
    const auto list = older != nullptr ? transfer::listTracks (*older) : std::vector<transfer::TrackInfo>();
    const auto entry = std::find_if (list.begin(), list.end(), [&] (const auto& t) { return t.id == reeseId; });
    check (list.size() == 3 && entry != list.end() && entry->name == "Reese" && entry->numDevices == 1 && entry->numSessionClips == 2
               && entry->numArrangementClips == 2,
           "its tracks are listed with their contents (" + juce::String ((int) list.size()) + " tracks)");

    model.undo.beginNewTransaction ("before");
    model.undo.beginNewTransaction ("marker"); // keeps the take in a transaction of its own
    const auto xmlBefore = xmlOf (model);
    check (session.takeTrackFromVersion (saved, (int64_t) reeseId).wasOk(), "the track is taken");
    check (model.numTracks() == tracksBefore + 1, "one track more");
    const auto taken = model.trackAt (model.numTracks() - 1);
    check (taken.name() == "Reese (v" + juce::String (saved) + ")", "named \"Reese (v" + juce::String (saved) + ")\": " + taken.name());
    check (taken.id() != reeseId && ! taken.arm(), "with a new id, not armed");
    check (std::abs (taken.volumeDb() - (-4.5f)) < 1.0e-4 && taken.numDevices() == 1 && taken.device (0).stateBase64() == reeseDeviceState && taken.device (0).kind() == "prisma",
           "the device comes with its sound");
    check (model.numScenes() == 4 && taken.numSlots() == 4, "the clip in scene 4 brought scenes 3 and 4 with it (the project had 2)");
    check (model.trackAt (0).numSlots() == 4 && model.trackAt (model.numTracks() - 2).numSlots() == 4, "and every other track has the slots");
    check (taken.clipAt (0).isValid() && taken.clipAt (0).name() == "Riff" && taken.clipAt (0).notes().size() == 2 && taken.clipAt (3).name() == "Break"
               && ! taken.clipAt (1).isValid() && ! taken.clipAt (2).isValid(),
           "the session clips are in their scenes");
    const auto aclips = taken.arrangementClips();
    check (aclips.size() == 2 && near (aclips[0].start(), 16.0) && aclips[0].clip().name() == "Verse bass" && near (aclips[1].start(), 32.0)
               && aclips[1].activeTake() == 2 && arrangement::takesOf (aclips[1]).size() == 2,
           "the arrangement clips are there, takes included");
    const auto idsAfter = allIds (model.root);
    const auto idsOfTaken = allIds (taken.state);
    bool clash = false;
    for (const auto id : idsOfTaken)
        clash = clash || idsBefore.count (id) > 0;
    check (! clash && idsOfTaken.size() > 8, "every id of the new track is new: no conflict with the project (" + juce::String ((int) idsOfTaken.size()) + " ids)");
    // new ids that came from the older project's counter must not collide with later ones
    model.undo.beginNewTransaction ("later");
    auto more = model.addTrack ("midi");
    check (idsAfter.count (more.id()) == 0, "ids handed out afterwards do not clash either");
    model.undo.undo();

    // the engine can build a graph with it (the device is created from the saved state)
    engine.rebuildGraph();
    check (engine.findRuntime (taken.id()) != nullptr, "the engine knows the new track");

    model.undo.undo();
    check (xmlOf (model) == xmlBefore && model.numTracks() == tracksBefore && model.numScenes() == scenesBefore, "one undo removes the track and the scenes it added");
    check (! session.takeTrackFromVersion (saved, 99999).wasOk() && ! session.takeTrackFromVersion (999, (int64_t) reeseId).wasOk(),
           "a missing track or version is an error and changes nothing");
    check (xmlOf (model) == xmlBefore, "(the project is untouched)");

    // the older version is only read: it still holds the old content
    {
        const auto again = project::loadVersion (root.getChildFile ("Projects/Night Drive Project"), saved);
        check (again.ok() && again.tree.getChildWithName (ids::TRACKS).getNumChildren() == 3, "the version on disk is unchanged");
    }
    session.closeCleanly();
    root.deleteRecursively();
}

//==============================================================================
void testRecordButton()
{
    std::printf ("[arrangement test] the record button: timeline in Arrangement View and while playing, slots in a stopped Session View\n");
    using A = RecordButtonAction;
    check (recordButtonAction (false, false, true, false) == A::recordTimeline, "Arrangement View, stopped: record onto the timeline");
    check (recordButtonAction (false, false, true, true) == A::recordTimeline, "Arrangement View, playing: record onto the timeline");
    check (recordButtonAction (false, false, false, true) == A::recordTimeline, "Session View, playing: record the session into the timeline");
    check (recordButtonAction (false, false, false, false) == A::recordSlot, "Session View, stopped: stage 2, record into the slot");
    check (recordButtonAction (true, false, false, true) == A::stopTimeline && recordButtonAction (true, false, true, false) == A::stopTimeline,
           "while the timeline records, the button ends it in either view");
    check (recordButtonAction (false, true, false, true) == A::stopSlots && recordButtonAction (false, true, true, true) == A::stopSlots,
           "while slots record, the button ends them (stage 2 behaviour kept)");
}

void testTransportBar()
{
    std::printf ("[arrangement test] transport: punch buttons next to the loop button, everything fits at the minimum window width\n");
    ThemeManager::get().reload();
    Fixture f;
    f.model.addTrack ("midi");
    for (const int width : { 1210, 1340, 1600 })
    {
        ui::TopBar bar (f.engine, f.model);
        bar.setBounds (0, 0, width, ui::TopBar::kHeight);
        bar.resized();
        juce::Component* loop = nullptr;
        juce::Component* punchIn = nullptr;
        juce::Component* punchOut = nullptr;
        juce::Component* follow = nullptr;
        std::vector<juce::Rectangle<int>> boxes;
        bool inside = true;
        for (auto* c : bar.getChildren())
        {
            if (! c->isVisible())
                continue;
            if (c->getHelpText() == Strings::help::loop)
                loop = c;
            if (c->getHelpText() == Strings::help::punchIn)
                punchIn = c;
            if (c->getHelpText() == Strings::help::punchOut)
                punchOut = c;
            if (c->getHelpText() == Strings::help::followPlayhead)
                follow = c;
            inside = inside && bar.getLocalBounds().contains (c->getBounds());
            boxes.push_back (c->getBounds());
        }
        bool overlap = false;
        for (size_t i = 0; i < boxes.size(); ++i)
            for (size_t j = i + 1; j < boxes.size(); ++j)
                overlap = overlap || boxes[i].reduced (1).intersects (boxes[j].reduced (1));
        const juce::String w (width);
        if (! inside || overlap)
            for (size_t i = 0; i < boxes.size(); ++i)
                std::printf ("    box %zu %s\n", i, boxes[i].toString().toRawUTF8());
        check (inside && ! overlap, w + ": every control is inside the bar and none covers another");
        check (loop && punchIn && punchOut && follow && loop->getRight() <= punchIn->getX() && punchIn->getRight() <= punchOut->getX()
                   && punchOut->getRight() <= follow->getX(),
               w + ": punch-in and punch-out sit between the loop and follow buttons");
        if (width == 1210 && punchIn != nullptr && punchOut != nullptr)
        {
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto click = [&] (juce::Component* c) {
                const auto t = juce::Time::getCurrentTime();
                const juce::MouseEvent e (source, c->getLocalBounds().getCentre().toFloat(), juce::ModifierKeys::leftButtonModifier, 0.0f, 0.0f, 0.0f, 0.0f,
                                          0.0f, c, c, t, c->getLocalBounds().getCentre().toFloat(), t, 1, false);
                c->mouseDown (e);
                c->mouseUp (e);
            };
            f.model.undo.clearUndoHistory();
            check (! f.model.punchIn() && ! f.model.punchOut(), "punch is off in a new project");
            click (punchIn);
            check (f.model.punchIn() && ! f.model.punchOut(), "a click on IN switches punch-in on");
            click (punchOut);
            check (f.model.punchIn() && f.model.punchOut(), "and OUT punch-out");
            click (punchIn);
            click (punchOut);
            check (! f.model.punchIn() && ! f.model.punchOut(), "a second click switches them off");
            check (! f.model.undo.canUndo(), "(they are transport switches, not edits of the music)");
            check (punchIn->getHelpText().contains ("Punch-in") && punchOut->getHelpText().contains ("Punch-out"), "both explain themselves");
        }
    }
}

void testTakesInTheView()
{
    std::printf ("[arrangement test] the view: Takes menu switches takes in one undo step; the clip shows its take; a growing clip is drawn while recording\n");
    ThemeManager::get().reload();
    Rig r (true, 2);
    ProjectActions actions (r.f.model, &r.f.engine);
    ui::SelectionState selection;
    ui::ArrangementView view (r.f.engine, r.f.model, actions, selection);
    view.setRecorder (&r.recorder);
    view.setBounds (0, 0, 1400, 600);
    view.setZoom (20.0, 0.0);
    view.tick();

    r.f.model.undo.beginNewTransaction ("make");
    const auto clip = arrangement::addRecordedClip (r.f.model, r.tracks[0], 8.0, 8.0,
                                                    { { { { 1.0, 1.0, 60, 100 } }, {} }, { { { 2.0, 1.0, 62, 100 } }, {} }, { { { 3.0, 1.0, 64, 100 } }, {} } }, 2);
    r.f.model.undo.clearUndoHistory();
    view.tick();
    juce::String message;
    view.onMessage = [&] (const juce::String& m) { message = m; };
    check (clip.activeTake() == 3 && arrangement::takesOf (clip).size() == 3, "a clip with three takes, the third shown");
    const auto before = xmlOf (r.f.model);
    check (view.switchTake (clip.id(), 1), "take 1 can be chosen in the view");
    check (clip.activeTake() == 1 && hasNote (notesOf (clip), 1.0, 1.0, 60) && message == Strings::arrRec::takeSelected (1), "its content is shown and the info bar says so");
    r.f.model.undo.undo();
    check (xmlOf (r.f.model) == before, "one undo step");
    check (! view.switchTake (clip.id(), 3) && ! view.switchTake (999999, 1), "the shown take and a missing clip do nothing");

    juce::Image image (juce::Image::ARGB, 1400, 600, true);
    {
        juce::Graphics g (image);
        view.paintEntireComponent (g, true);
    }
    check (image.getWidth() == 1400, "a clip with takes paints");

    // a recording that runs: the playhead layer shows the growing clip
    r.f.model.setArrangementPlayStart (0.0);
    r.f.model.setCountInBars (0);
    r.f.model.undo.clearUndoHistory();
    check (r.recorder.arrangement().start(), "recording armed");
    const std::vector<Sched> played { noteOn (1.0, 60), noteOff (3.0, 60) };
    r.runTo (6.0, played);
    view.tick();
    ArrangementRecorder::Progress progress;
    check (r.recorder.arrangement().getProgress (r.tracks[0].id(), progress) && ! progress.waiting && progress.notes.size() == 1, "the recorder reports the note played so far");
    const auto& rows = view.rows();
    if (! rows.empty())
    {
        const int top = ui::ArrangementView::kRulerHeight + rows[0].y + rows[0].height / 2;
        const int x = juce::roundToInt (view.beatToX (3.0));
        auto snapshot = view.playhead().createComponentSnapshot (view.playhead().getLocalBounds());
        const auto inside = snapshot.getPixelAt (x, top), outside = snapshot.getPixelAt (juce::roundToInt (view.beatToX (30.0)), top);
        check (inside.getAlpha() > 20 && outside.getAlpha() == 0, "the growing clip is drawn over the lane up to the position, nothing after it");
    }
    r.recorder.arrangement().stop();
    r.settle();
}
} // namespace

void tests::runArrangementRecordingTests()
{
    testRecordOnTimeline();
    testTakes();
    testPunch();
    testOverdub();
    testSessionLog();
    testEngineLog();
    testLiveRecordingThroughEngine();
    testLoopTakesThroughEngine();
    testPunchThroughEngine();
    testSessionThroughEngine();
    testRecordingReplacesAndOverdubPlays();
    testRealtimeSafety();
    testTakeTrack();
    testRecordButton();
    testTransportBar();
    testTakesInTheView();
}
