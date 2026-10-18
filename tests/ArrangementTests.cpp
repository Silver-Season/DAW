// Stage 3, tasks 3.1 and 3.2 of docs/spec/03-arrangement.md: the arrangement model (overlaps, editing operations,
// insert and remove time, markers, schema 2) and its playback through the engine (loop, seek, chase, session against
// arrangement). Tests 1, 2, 3, 4, 5 and 8 of the spec; 2 to 4 also have core-level versions in core/tests/core_tests.cpp.
#include "TestSupport.h"

#include "../app/Source/Strings.h"
#include "../app/Source/engine/RtCheck.h"
#include "../app/Source/model/ArrangementEdit.h"
#include "../app/Source/project/ProjectFile.h"

#include <array>
#include <atomic>
#include <cmath>
#include <set>

using namespace tests;

namespace
{
constexpr double kRate = 48000.0;
constexpr double kSpb = 24000.0; // 120 BPM

bool eq (double a, double b)
{
    return std::abs (a - b) < 1.0e-9;
}

juce::String xml (const ProjectModel& m)
{
    return m.root.toXmlString();
}

// Runs `op` as one undo transaction and checks that it changed the project and that undo and redo restore it exactly.
template <typename F>
void undoable (ProjectModel& m, const juce::String& what, F op)
{
    const auto before = xml (m);
    m.undo.beginNewTransaction (what);
    op();
    const auto after = xml (m);
    check (after != before, what + ": changes the project");
    m.undo.undo();
    check (xml (m) == before, what + ": one undo restores everything");
    m.undo.redo();
    check (xml (m) == after, what + ": redo brings it back");
}

// An arrangement clip of `length` beats whose content is `contentLength` beats long and holds `notes`.
ArrangementClipRef place (ProjectModel& m, const TrackRef& t, double start, double length, std::vector<daw::engine::Note> notes = {},
                          double contentLength = -1.0)
{
    auto a = arrangement::addEmptyClip (m, t, start, length);
    if (contentLength > 0.0)
        a.clip().setLengthBeats (contentLength, &m.undo);
    a.clip().setNotes (notes, &m.undo);
    return a;
}

// {start, end} pairs of the clips of a track, sorted
std::vector<std::pair<double, double>> spans (const TrackRef& t)
{
    std::vector<std::pair<double, double>> out;
    for (const auto& c : t.arrangementClips())
        out.emplace_back (c.start(), c.end());
    return out;
}

bool noOverlaps (const ProjectModel& m)
{
    for (const auto& t : m.tracks())
    {
        double end = -1.0;
        for (const auto& c : t.arrangementClips())
        {
            if (c.start() < end - 1.0e-9)
                return false;
            end = c.end();
        }
    }
    return true;
}

// All ids in the project are unique
bool idsUnique (const juce::ValueTree& root)
{
    std::set<juce::int64> seen;
    bool ok = true;
    const std::function<void (const juce::ValueTree&)> visit = [&] (const juce::ValueTree& n) {
        if (n.hasProperty (ids::id))
            ok = ok && seen.insert ((juce::int64) n.getProperty (ids::id)).second;
        for (const auto& c : n)
            visit (c);
    };
    visit (root);
    return ok;
}

struct TwoTracks
{
    TwoTracks()
    {
        for (int i = 0; i < 4; ++i)
            m.insertScene (-1);
        a = m.addTrack ("midi");
        b = m.addTrack ("midi");
        m.undo.clearUndoHistory();
    }
    ProjectModel m = ProjectModel::createEmpty();
    TrackRef a, b;
};

//==============================================================================
void testOverlaps()
{
    std::printf ("[arrangement test 1] resolveOverlaps: cut in two, trimmed, removed; undo restores\n");

    {
        TwoTracks p; // a clip on top of the middle of another cuts it in two
        auto under = place (p.m, p.a, 0.0, 16.0, { { 0.0, 1.0, 60, 100 }, { 9.0, 1.0, 62, 100 } });
        const auto id = under.id();
        const auto before = xml (p.m);
        p.m.undo.beginNewTransaction ("x");
        auto top = arrangement::addEmptyClip (p.m, p.a, 4.0, 4.0);
        check (p.a.numArrangementClips() == 3, "three clips: left part, the new one, right part");
        check (spans (p.a) == std::vector<std::pair<double, double>> { { 0.0, 4.0 }, { 4.0, 8.0 }, { 8.0, 16.0 } }, "the spans are [0,4) [4,8) [8,16)");
        const auto clips = p.a.arrangementClips();
        check (clips[0].id() == id && eq (clips[0].offset(), 0.0), "the left part is the original clip");
        check (clips[2].id() != id && eq (clips[2].offset(), 8.0), "the right part is a new clip with its content entered at beat 8");
        check (clips[2].clip().id() != clips[0].clip().id(), "and with its own copy of the content");
        check (clips[2].clip().numNotes() == 2, "the copy holds the notes");
        check (noOverlaps (p.m) && idsUnique (p.m.root), "no overlaps, all ids unique");
        p.m.undo.undo();
        check (xml (p.m) == before, "undo gives back the one original clip");
        juce::ignoreUnused (top);
    }
    {
        TwoTracks p; // over the right edge, over the left edge, covering completely
        place (p.m, p.a, 8.0, 8.0);
        p.m.undo.beginNewTransaction ("x");
        arrangement::addEmptyClip (p.m, p.a, 14.0, 4.0);
        check (spans (p.a) == std::vector<std::pair<double, double>> { { 8.0, 14.0 }, { 14.0, 18.0 } }, "a clip over the right edge trims the old one");
        arrangement::addEmptyClip (p.m, p.a, 4.0, 6.0);
        const auto s = spans (p.a);
        check (s == std::vector<std::pair<double, double>> { { 4.0, 10.0 }, { 10.0, 14.0 }, { 14.0, 18.0 } }, "a clip over the left edge trims the old one from the left");
        check (std::abs (p.a.arrangementClips()[1].offset() - 2.0) < 1e-9, "the trimmed clip keeps its content in place (offset 2)");
        p.m.undo.beginNewTransaction ("y");
        arrangement::addEmptyClip (p.m, p.a, 0.0, 32.0);
        check (p.a.numArrangementClips() == 1 && noOverlaps (p.m), "a clip that covers everything leaves only itself");
        p.m.undo.undo();
        check (p.a.numArrangementClips() == 3, "undo of that: the three clips are back");
    }
    {
        TwoTracks p; // a clip of another track is not touched
        place (p.m, p.a, 0.0, 8.0);
        place (p.m, p.b, 4.0, 8.0);
        check (p.a.numArrangementClips() == 1 && p.b.numArrangementClips() == 1, "tracks do not cut each other");
    }
    {
        TwoTracks p; // exactly adjacent clips do not overlap
        place (p.m, p.a, 0.0, 4.0);
        place (p.m, p.a, 4.0, 4.0);
        check (p.a.numArrangementClips() == 2, "clips that touch stay as they are");
    }
}

//==============================================================================
void testEditing()
{
    std::printf ("[arrangement test] editing operations are undoable and keep the clips apart\n");
    const auto notes = std::vector<daw::engine::Note> { { 0.0, 1.0, 60, 100 }, { 2.0, 1.0, 64, 90 } };

    {
        TwoTracks p;
        undoable (p.m, "add an empty clip", [&] { arrangement::addEmptyClip (p.m, p.a, 8.0, 4.0, "Intro"); });
        check (p.a.arrangementClips()[0].clip().name() == "Intro" && p.a.arrangementClips()[0].loop(), "the clip has its name and loops");
    }
    {
        TwoTracks p; // copy of a slot clip
        auto slot = p.m.createClip (p.a, 1, 8.0, "Riff");
        slot.setNotes (notes, &p.m.undo);
        slot.setLoopEnabled (false, &p.m.undo);
        p.m.undo.clearUndoHistory();
        ArrangementClipRef made;
        undoable (p.m, "add a slot clip", [&] { made = arrangement::addSlotClip (p.m, p.b, slot, 16.0); });
        const auto c = p.b.arrangementClips()[0];
        check (eq (c.start(), 16.0) && eq (c.length(), 8.0) && ! c.loop() && c.clip().numNotes() == 2, "length of the slot clip, its loop flag and a copy of its notes");
        check (c.clip().id() != slot.id() && p.m.findClip (c.clip().id()).isValid(), "the copy has its own id and findClip finds it");
        slot.addNote ({ 5.0, 1.0, 70, 100 }, nullptr);
        check (c.clip().numNotes() == 2, "editing the slot clip later does not change the copy");
    }
    {
        TwoTracks p; // move: in time, to another track, a group
        auto c1 = place (p.m, p.a, 0.0, 4.0, notes), c2 = place (p.m, p.a, 8.0, 4.0, notes);
        place (p.m, p.b, 0.0, 16.0);
        undoable (p.m, "move to another track", [&] { arrangement::moveClips (p.m, { { c1, p.b, 4.0 } }); });
        check (p.a.numArrangementClips() == 1 && p.b.numArrangementClips() == 3, "the clip left track A and cut the clip of track B in two");
        check (spans (p.b) == std::vector<std::pair<double, double>> { { 0.0, 4.0 }, { 4.0, 8.0 }, { 8.0, 16.0 } }, "track B: [0,4) [4,8) [8,16)");

        // a group moves together: neither clip of the group cuts the other, however the order goes
        TwoTracks q;
        auto g1 = place (q.m, q.a, 0.0, 4.0), g2 = place (q.m, q.a, 4.0, 4.0);
        undoable (q.m, "move a group by 2 beats", [&] { arrangement::moveClips (q.m, { { g1, q.a, 2.0 }, { g2, q.a, 6.0 } }); });
        check (spans (q.a) == std::vector<std::pair<double, double>> { { 2.0, 6.0 }, { 6.0, 10.0 } }, "both clips moved, none was cut by the other");
        arrangement::moveClips (q.m, { { g1, q.a, -3.0 }, { g2, q.a, 1.0 } });
        check (spans (q.a) == std::vector<std::pair<double, double>> { { 0.0, 4.0 }, { 4.0, 8.0 } }, "a group cannot leave the song at the left: it shifts as a whole");
        juce::ignoreUnused (c2);
    }
    {
        TwoTracks p; // copy and paste
        auto src = place (p.m, p.a, 0.0, 4.0, notes);
        ArrangementClipRef copy;
        undoable (p.m, "copy to another track", [&] { copy = arrangement::copyClip (p.m, src, p.b, 12.0); });
        check (p.a.numArrangementClips() == 1 && p.b.numArrangementClips() == 1, "the original stays, the copy is on the other track");
        const auto c = p.b.arrangementClips()[0];
        check (eq (c.start(), 12.0) && eq (c.length(), 4.0) && c.clip().numNotes() == 2 && c.clip().id() != src.clip().id() && c.id() != src.id(),
               "the copy has the same shape, its own content and new ids");
        check (idsUnique (p.m.root), "all ids are unique");
        const auto clipboard = src.state.createCopy(); // what the view keeps for Cmd+C
        undoable (p.m, "paste from the clipboard", [&] { arrangement::pasteClip (p.m, p.a, clipboard, 6.0); });
        undoable (p.m, "duplicate after the clip", [&] { arrangement::duplicateAfter (p.m, src); });
        check (spans (p.a) == std::vector<std::pair<double, double>> { { 0.0, 4.0 }, { 4.0, 8.0 }, { 8.0, 10.0 } },
               "the duplicate sits right after the clip [4,8) and cut the pasted clip [6,10) to [8,10)");
        check (noOverlaps (p.m), "no overlaps after copy, paste and duplicate");
    }
    {
        TwoTracks p; // duplicate a time range: copies of everything in [4, 8) go to [8, 12)
        place (p.m, p.a, 0.0, 6.0, notes);
        place (p.m, p.a, 6.0, 4.0, notes);
        place (p.m, p.b, 4.0, 2.0, notes);
        undoable (p.m, "duplicate a time range", [&] { arrangement::duplicateRange (p.m, { p.a, p.b }, 4.0, 8.0); });
        check (spans (p.a) == std::vector<std::pair<double, double>> { { 0.0, 6.0 }, { 6.0, 8.0 }, { 8.0, 10.0 }, { 10.0, 12.0 } },
               "track A: [4,6) and [6,8) were copied to [8,10) and [10,12); the clip [6,10) under them is cut to [6,8)");
        check (noOverlaps (p.m) && idsUnique (p.m.root), "no overlaps, unique ids");
        const auto bs = spans (p.b);
        check (bs.size() == 2 && eq (bs[1].first, 8.0) && eq (bs[1].second, 10.0), "track B: the clip [4,6) was copied to [8,10)");
    }
    {
        TwoTracks p; // stretching
        auto c = place (p.m, p.a, 4.0, 4.0, notes);
        place (p.m, p.a, 12.0, 4.0);
        undoable (p.m, "stretch right", [&] { arrangement::resizeRight (p.m, c, 14.0); });
        check (spans (p.a) == std::vector<std::pair<double, double>> { { 4.0, 14.0 }, { 14.0, 16.0 } }, "stretched to 14: the next clip is trimmed from the left");
        check (p.a.arrangementClips()[0].loop() && eq (p.a.arrangementClips()[1].offset(), 2.0), "the content repeats, the trimmed clip's offset follows");
        undoable (p.m, "shrink right", [&] { arrangement::resizeRight (p.m, c, 5.0); });
        check (std::abs (c.length() - 1.0) < 1e-9, "shrunk to one beat");
        arrangement::resizeRight (p.m, c, 4.0);
        check (eq (c.length(), arrangement::kMinClipBeats), "a clip cannot get shorter than the minimum");
    }
    {
        TwoTracks p; // stretching the left edge changes the offset; the content stays in time
        auto c = place (p.m, p.a, 8.0, 8.0, notes, 4.0);
        undoable (p.m, "trim left", [&] { arrangement::resizeLeft (p.m, c, 10.0); });
        check (eq (c.start(), 10.0) && eq (c.end(), 16.0) && std::abs (c.offset() - 2.0) < 1e-9, "start 10, end unchanged, offset 2");
        arrangement::resizeLeft (p.m, c, 7.0); // looping: pulls the previous repeat into view
        check (eq (c.start(), 7.0) && eq (c.end(), 16.0) && std::abs (c.offset() - 3.0) < 1e-9, "growing to the left wraps the offset through the repeats (7 -> offset 3)");
        auto d = place (p.m, p.b, 8.0, 4.0, notes, 8.0);
        d.setLoop (false, &p.m.undo);
        arrangement::resizeLeft (p.m, d, 4.0);
        check (eq (d.start(), 8.0), "a clip that does not loop cannot grow to the left past the start of its content");
        arrangement::resizeLeft (p.m, d, 10.0);
        check (eq (d.start(), 10.0) && eq (d.offset(), 2.0), "but it can shrink");
    }
    {
        TwoTracks p; // delete
        auto c = place (p.m, p.a, 0.0, 4.0);
        undoable (p.m, "delete a clip", [&] { arrangement::deleteClips (p.m, { c }); });
        place (p.m, p.a, 0.0, 16.0);
        undoable (p.m, "delete a time range", [&] { arrangement::clearRange (p.m, p.a, 4.0, 8.0); });
        check (spans (p.a) == std::vector<std::pair<double, double>> { { 0.0, 4.0 }, { 8.0, 16.0 } }, "the content of [4,8) is gone, the clip is in two parts");
    }
}

//==============================================================================
void testSplitAndJoin()
{
    std::printf ("[arrangement test] split and join\n");
    TwoTracks p;
    const std::vector<daw::engine::Note> notes { { 0.0, 1.0, 60, 100 }, { 2.0, 1.0, 64, 90 } };
    auto c = place (p.m, p.a, 4.0, 8.0, notes, 4.0); // two repeats of a one-bar clip

    ArrangementClipRef right;
    undoable (p.m, "split", [&] { right = arrangement::splitAt (p.m, c, 7.0); });
    const auto parts = p.a.arrangementClips();
    check (parts.size() == 2 && eq (parts[0].end(), 7.0) && eq (parts[1].start(), 7.0) && eq (parts[1].end(), 12.0), "two clips, [4,7) and [7,12)");
    check (std::abs (parts[1].offset() - 3.0) < 1e-9, "the right part enters the content at beat 3");
    check (arrangement::splitAt (p.m, parts[0], 4.0).isValid() == false && arrangement::splitAt (p.m, parts[0], 20.0).isValid() == false,
           "no cut at the edges or outside");

    // the two parts play exactly what the whole clip played
    const auto whole = daw::engine::flattenArrangementClip (c.clip().toMidiClip(), 0.0, true, 8.0);
    const auto leftPlay = daw::engine::flattenArrangementClip (parts[0].clip().toMidiClip(), parts[0].offset(), parts[0].loop(), parts[0].length());
    const auto rightPlay = daw::engine::flattenArrangementClip (parts[1].clip().toMidiClip(), parts[1].offset(), parts[1].loop(), parts[1].length());
    bool same = true;
    for (const auto& n : whole.notes)
    {
        bool found = false;
        for (const auto& l : leftPlay.notes)
            found = found || (std::abs (l.start - n.start) < 1e-9 && l.pitch == n.pitch);
        for (const auto& r : rightPlay.notes)
            found = found || (std::abs (r.start + 3.0 - n.start) < 1e-9 && r.pitch == n.pitch);
        same = same && found;
    }
    check (same, "every note that sounded before the cut sounds after it, at the same time");

    // join: the two parts and a third one after a gap
    auto third = place (p.m, p.a, 16.0, 4.0, { { 1.0, 1.0, 67, 80 } });
    ArrangementClipRef joined;
    undoable (p.m, "join", [&] { joined = arrangement::joinClips (p.m, p.a.arrangementClips()); });
    check (p.a.numArrangementClips() == 1, "one clip remains");
    const auto j = p.a.arrangementClips()[0];
    check (eq (j.start(), 4.0) && eq (j.end(), 20.0) && eq (j.offset(), 0.0), "from the start of the first to the end of the last");
    const auto jn = j.clip().notes();
    check (eq (j.clip().lengthBeats(), 16.0), "the content is 16 beats long, the gap included");
    int atGap = 0;
    for (const auto& n : jn)
        atGap += n.start >= 8.0 && n.start < 12.0 ? 1 : 0;
    check (atGap == 0, "no notes in the gap [12,16) of the song (content beats 8..12)");
    check (jn.size() == whole.notes.size() + 1, "all notes were moved: " + juce::String ((int) jn.size()));
    bool lateNote = false;
    for (const auto& n : jn)
        lateNote = lateNote || (n.pitch == 67 && std::abs (n.start - 13.0) < 1e-9);
    check (lateNote, "the note of the third clip (beat 1 of a clip at 16) is at content beat 13");
    check (arrangement::joinClips (p.m, { j }).isValid() == false, "one clip alone is not joined");
    juce::ignoreUnused (third);
}

//==============================================================================
void testTime()
{
    std::printf ("[arrangement test 8] insert and remove time shift clips and markers of all tracks\n");
    TwoTracks p;
    place (p.m, p.a, 4.0, 8.0, { { 0.0, 1.0, 60, 100 } }, 4.0);   // [4,12)
    place (p.m, p.a, 16.0, 4.0);                                    // [16,20)
    place (p.m, p.b, 0.0, 4.0);                                     // [0,4)
    place (p.m, p.b, 10.0, 2.0);                                    // [10,12)
    arrangement::addMarker (p.m, 2.0, "A");
    arrangement::addMarker (p.m, 8.0, "B");
    arrangement::addMarker (p.m, 18.0, "C");
    p.m.undo.clearUndoHistory();

    undoable (p.m, "insert time", [&] { arrangement::insertTime (p.m, 8.0, 4.0); });
    check (spans (p.a) == std::vector<std::pair<double, double>> { { 4.0, 8.0 }, { 12.0, 16.0 }, { 20.0, 24.0 } },
           "track A: the clip cut by the insert point became two [4,8) [12,16); the next clip moved to [20,24)");
    check (std::abs (p.a.arrangementClips()[1].offset() - 0.0) < 1e-9, "the right part still enters the content at the beat where the cut was (4 of 4 = 0)");
    check (spans (p.b) == std::vector<std::pair<double, double>> { { 0.0, 4.0 }, { 14.0, 16.0 } }, "track B: the clip before stays, the one after moved by 4");
    const auto markers = p.m.markers();
    check (markers.size() == 3 && eq (markers[0].beat(), 2.0) && eq (markers[1].beat(), 12.0) && eq (markers[2].beat(), 22.0), "markers at or after the insert point moved by 4");
    check (noOverlaps (p.m) && idsUnique (p.m.root), "no overlaps, unique ids");

    undoable (p.m, "remove time", [&] { arrangement::removeTime (p.m, 6.0, 14.0); });
    check (spans (p.a) == std::vector<std::pair<double, double>> { { 4.0, 6.0 }, { 6.0, 8.0 }, { 12.0, 16.0 } },
           "track A: [4,8) is cut at 6, [12,16) lost [12,14) and moved to [6,8), [20,24) moved to [12,16)");
    check (spans (p.b) == std::vector<std::pair<double, double>> { { 0.0, 4.0 }, { 6.0, 8.0 } }, "track B: the clip [14,16) moved to [6,8)");
    const auto bm = p.m.markers();
    check (bm.size() == 2 && eq (bm[0].beat(), 2.0) && eq (bm[1].beat(), 14.0), "the marker inside the removed range is gone, the later one moved by 8");
    check (noOverlaps (p.m), "no overlaps");
}

//==============================================================================
void testSessionAndMarkers()
{
    std::printf ("[arrangement test] send to session, markers, song length, loop and view properties\n");
    {
        TwoTracks p;
        auto c = place (p.m, p.a, 8.0, 8.0, { { 0.0, 1.0, 60, 100 } }, 4.0);
        p.m.createClip (p.a, 0, 4.0);
        p.m.createClip (p.a, 1, 4.0);
        p.m.undo.clearUndoHistory();
        int scene = -1;
        undoable (p.m, "send to session", [&] { scene = arrangement::sendToSession (p.m, c, 0); });
        check (scene == 2 && p.a.clipAt (2).isValid(), "the first empty slot from scene 0 is scene 2");
        check (eq (p.a.clipAt (2).lengthBeats(), 8.0) && p.a.clipAt (2).numNotes() == 2, "the clip is as long as the arrangement clip and the repeats are written out");
        check (p.a.clipAt (2).id() != c.clip().id() && idsUnique (p.m.root), "with its own ids");
        p.m.createClip (p.a, 3, 4.0);
        const int scenesBefore = p.m.numScenes();
        undoable (p.m, "send to session, no free slot", [&] { scene = arrangement::sendToSession (p.m, c, 0); });
        check (scene == scenesBefore && p.m.numScenes() == scenesBefore + 1 && p.a.clipAt (scene).isValid(), "with no free slot a scene is added");
    }
    {
        TwoTracks p;
        check (eq (arrangement::songLengthBeats (p.m), 64.0), "an empty song is 16 bars (64 beats)");
        p.m.setSignature (3, 4);
        check (eq (arrangement::songLengthBeats (p.m), 48.0), "3/4: the minimum is 16 bars of 3 beats");
        p.m.setSignature (4, 4);
        place (p.m, p.a, 100.0, 8.0);
        check (eq (arrangement::songLengthBeats (p.m), 108.0), "the end of the last clip");
        arrangement::addMarker (p.m, 200.0);
        check (eq (arrangement::songLengthBeats (p.m), 200.0), "or of the last marker");
    }
    {
        TwoTracks p;
        MarkerRef m1, m2;
        undoable (p.m, "add a marker", [&] { m1 = arrangement::addMarker (p.m, 16.0, Strings::utf8 ("Куплет")); });
        m2 = arrangement::addMarker (p.m, 32.0);
        check (m1.name() == Strings::utf8 ("Куплет") && m2.name() == Strings::defaultMarkerName (2), "name given, default name numbered");
        undoable (p.m, "rename a marker", [&] { arrangement::renameMarker (p.m, m2, Strings::utf8 ("Дроп")); });
        undoable (p.m, "move a marker", [&] { arrangement::moveMarker (p.m, m2, 40.0); });
        undoable (p.m, "recolour a marker", [&] { arrangement::setMarkerColour (p.m, m2, 5); });
        check (m2.name() == Strings::utf8 ("Дроп") && eq (m2.beat(), 40.0) && m2.colour() == 5, "values are set");
        check (eq (arrangement::previousMarker (p.m, 40.0).beat(), 16.0) && eq (arrangement::nextMarker (p.m, 16.0).beat(), 40.0), "previous and next marker");
        check (! arrangement::previousMarker (p.m, 16.0).isValid() && ! arrangement::nextMarker (p.m, 40.0).isValid(), "none before the first or after the last");
        undoable (p.m, "delete a marker", [&] { arrangement::deleteMarker (p.m, m1); });
        check (p.m.markers().size() == 1, "one marker left");
    }
    {
        TwoTracks p;
        check (! p.m.arrangementLoopOn() && eq (p.m.arrangementLoopStart(), 0.0) && eq (p.m.arrangementLoopEnd(), 16.0) && p.m.followPlayhead(),
               "defaults: loop off, 0 to 16, follow on");
        undoable (p.m, "set the loop", [&] { p.m.setArrangementLoop (8.0, 24.0, true); });
        check (eq (p.m.arrangementLoopStart(), 8.0) && eq (p.m.arrangementLoopEnd(), 24.0) && p.m.arrangementLoopOn(), "the loop is stored");
        p.m.setArrangementLoop (8.0, 8.0, true);
        check (p.m.arrangementLoopEnd() >= 8.0 + ProjectModel::kMinArrangementLoopBeats, "the loop end is kept after the start");
        undoable (p.m, "toggle the loop", [&] { p.m.setArrangementLoopOn (false); });

        p.m.undo.clearUndoHistory();
        p.m.setArrangementPlayStart (12.0);
        p.m.setFollowPlayhead (false);
        check (eq (p.m.arrangementPlayStart(), 12.0) && ! p.m.followPlayhead() && ! p.m.undo.canUndo(), "play start and follow are view state: stored, not undoable");
    }
}

//==============================================================================
void testSchema()
{
    std::printf ("[arrangement test] schema 2: a version 1 file still loads\n");
    check (ProjectModel::kSchemaVersion >= 2, "the schema version is at least 2");

    auto model = ProjectModel::createDefault();
    auto root = model.root.createCopy();
    for (auto track : root.getChildWithName (ids::TRACKS))
        track.removeChild (track.getChildWithName (ids::ARRANGEMENT), nullptr);
    root.removeChild (root.getChildWithName (ids::MARKERS), nullptr);
    root.setProperty (ids::schemaVersion, 1, nullptr);
    const auto result = project::parseProject (project::serialise (root));
    check (result.ok(), "a version 1 file without ARRANGEMENT and MARKERS loads: " + result.error);
    check ((int) result.tree.getProperty (ids::schemaVersion) == ProjectModel::kSchemaVersion, "and is brought to the current version");
    check (result.tree.getChildWithName (ids::TRACKS).getChild (0).getChildWithName (ids::ARRANGEMENT).isValid(), "every track has an ARRANGEMENT");

    ProjectModel loaded (result.tree);
    check (loaded.markersNode().isValid() && loaded.numTracks() == 2, "the markers node exists, the tracks are intact");
    auto a = arrangement::addEmptyClip (loaded, loaded.trackAt (0), 0.0, 4.0);
    check (a.isValid() && loaded.trackAt (0).numArrangementClips() == 1, "the loaded project can be edited");

    // a project with arrangement content survives save and load
    const auto text = project::serialise (loaded.root);
    const auto again = project::parseProject (text);
    ProjectModel back (again.tree);
    check (again.ok() && back.trackAt (0).arrangementClips().size() == 1 && back.trackAt (0).arrangementClips()[0].clip().isValid(), "an arrangement clip is saved and loaded");
    check (project::migrations().size() >= 1, "the migration 1 to 2 exists");

    // a track copied with its arrangement gets new ids
    const auto copy = loaded.duplicateTrack (loaded.trackAt (0).id());
    check (copy.numArrangementClips() == 1 && idsUnique (loaded.root), "a duplicated track brings a copy of its arrangement, with new ids");
}

//==============================================================================
// Engine: playback of the arrangement
// An instrument that logs what it is sent, with the engine sample of each event. Fixed storage: no allocation on the audio thread.
class EventLog final : public juce::AudioProcessor
{
public:
    struct Entry
    {
        int64_t sample;
        uint8_t status, d1, d2;
    };

    explicit EventLog (int block) : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)), blockSize (block) {}

    using AudioProcessor::processBlock;
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        for (const auto meta : midi)
        {
            const int n = count.load (std::memory_order_relaxed);
            if (n < (int) log.size())
            {
                log[(size_t) n] = { blocks * blockSize + meta.samplePosition, meta.data[0], meta.numBytes > 1 ? meta.data[1] : (uint8_t) 0,
                                    meta.numBytes > 2 ? meta.data[2] : (uint8_t) 0 };
                count.store (n + 1, std::memory_order_relaxed);
            }
        }
        ++blocks;
        buffer.clear();
    }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "EventLog"; }
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

    // sample positions of the note-ons (or note-offs) of a pitch
    std::vector<int64_t> ons (int pitch) const { return of (0x90, pitch); }
    std::vector<int64_t> offs (int pitch) const { return of (0x80, pitch); }
    // note-ons minus note-offs of a pitch: 0 when nothing hangs
    int balance (int pitch) const { return (int) ons (pitch).size() - (int) offs (pitch).size(); }

    std::atomic<int> count { 0 };
    int64_t blocks = 0; // audio thread
    const int blockSize;
    std::array<Entry, 65536> log {};

private:
    std::vector<int64_t> of (int type, int pitch) const
    {
        std::vector<int64_t> out;
        for (int i = 0; i < count.load(); ++i)
        {
            const auto& e = log[(size_t) i];
            const int kind = e.status & 0xF0;
            if (kind != 0x80 && kind != 0x90)
                continue;
            const bool on = kind == 0x90 && e.d2 > 0;
            if (on == (type == 0x90) && e.d1 == pitch)
                out.push_back (e.sample);
        }
        return out;
    }
};

struct Rig
{
    explicit Rig (int block = 256, int tracks = 2) : dev (block, kRate), runner (f.engine, block), blockSize (block)
    {
        f.engine.setDeviceFactory ([this] (const DeviceRef&) -> std::unique_ptr<juce::AudioProcessor> {
            auto p = std::make_unique<EventLog> (blockSize);
            logs.push_back (p.get());
            return p;
        });
        for (int i = 0; i < 4; ++i)
            f.model.insertScene (-1);
        for (int i = 0; i < tracks; ++i)
        {
            auto t = f.model.addTrack ("midi");
            f.model.addDevice (t, "prisma", "Log");
            trackList.push_back (t);
        }
        f.engine.audioDeviceAboutToStart (&dev);
        f.engine.rebuildGraphIfNeeded();
        f.model.undo.clearUndoHistory();
    }
    ~Rig() { f.engine.audioDeviceStopped(); }

    EventLog* log (int track) const
    {
        const auto* p = f.engine.findProcessor (trackList[(size_t) track].device (0).id());
        for (auto* l : logs)
            if (l == p)
                return l;
        return nullptr;
    }
    // Runs blocks until `beats` beats (of the monotonic clock, 24000 samples each) have been added to the engine sample.
    void runSamples (int64_t samples)
    {
        const int64_t target = f.engine.getEngineSample() + samples;
        while (f.engine.getEngineSample() < target)
            runner.run (1);
    }
    void runBeats (double beats) { runSamples ((int64_t) std::llround (beats * kSpb)); }
    static bool near (int64_t a, int64_t b, int64_t tolerance = 1) { return std::abs (a - b) <= tolerance; }

    Fixture f;
    FakeDevice dev;
    Runner runner;
    const int blockSize;
    std::vector<EventLog*> logs;
    std::vector<TrackRef> trackList;
};

void testPlayback()
{
    std::printf ("[arrangement test 2] playback: offsets, repeats and the end of an arrangement clip through the engine\n");
    Rig r (331, 1); // a block size that does not divide the beats
    auto t = r.trackList[0];
    // a one-bar clip with two notes, in an arrangement clip of two bars from beat 4
    place (r.f.model, t, 4.0, 8.0, { { 0.0, 1.0, 60, 100 }, { 2.0, 3.0, 64, 100 } }, 4.0); // the 64 is cut at the end of each pass
    r.f.engine.rebuildGraphIfNeeded();

    r.runner.run (2);
    const int64_t origin = r.f.engine.getEngineSample();
    r.f.engine.play();
    r.runBeats (14.0);
    auto* log = r.log (0);
    const auto on60 = log->ons (60), on64 = log->ons (64), off64 = log->offs (64);
    check (on60.size() == 2 && Rig::near (on60[0], origin + (int64_t) (4 * kSpb)) && Rig::near (on60[1], origin + (int64_t) (8 * kSpb)),
           "note 60 sounds at song beats 4 and 8 (two repeats of the one-bar content)");
    check (on64.size() == 2 && Rig::near (on64[0], origin + (int64_t) (6 * kSpb)) && Rig::near (on64[1], origin + (int64_t) (10 * kSpb)), "note 64 at beats 6 and 10");
    check (log->balance (60) == 0 && log->balance (64) == 0, "every note-on has a note-off");
    check (off64.size() == 2 && Rig::near (off64[0], origin + (int64_t) (8 * kSpb), 2) && Rig::near (off64[1], origin + (int64_t) (12 * kSpb), 2),
           "the long note is cut at the end of each pass and the last one at the end of the arrangement clip (beat 12)");
    check (r.runner.nonFinite == 0, "no NaN");
    r.f.engine.stop();
    r.runner.run (3);
}

void testLoopThroughEngine()
{
    std::printf ("[arrangement test 4] the song loop through the engine: exact jumps, the metronome and the song position follow\n");
    Rig r (331, 1);
    auto t = r.trackList[0];
    place (r.f.model, t, 0.0, 32.0, { { 0.0, 0.25, 60, 100 } }, 1.0); // a note on every beat
    r.f.model.setArrangementLoop (0.0, 8.0, true);
    r.f.model.setMetronome (true);
    r.f.engine.rebuildGraphIfNeeded();

    daw::rt::resetViolations();
    r.runner.run (2);
    const int64_t origin = r.f.engine.getEngineSample();
    r.f.engine.play();
    double maxSong = 0.0, maxMono = 0.0;
    const int64_t total = (int64_t) (3 * 8 * kSpb) + 5000;
    while (r.f.engine.getEngineSample() < origin + total)
    {
        r.runner.run (1);
        maxSong = std::max (maxSong, r.f.engine.getSongPositionBeats());
        maxMono = std::max (maxMono, r.f.engine.getPositionBeats());
    }
    const auto ons = r.log (0)->ons (60);
    bool exact = ons.size() >= 24;
    for (size_t i = 0; i < ons.size() && i < 24; ++i)
        exact = exact && Rig::near (ons[i], origin + (int64_t) i * (int64_t) kSpb);
    check (exact, "three laps of eight beats: 24 notes, one every 24000 samples, the first of each lap exactly at the jump (sample 192000 of a lap)");
    check (maxSong <= 8.0 + 1e-9, "the song position never leaves the loop (max " + juce::String (maxSong, 4) + ")");
    check (maxMono > 24.0, "the monotonic position runs on (" + juce::String (maxMono, 2) + ")");
    check (r.runner.nonFinite == 0 && daw::rt::violationCount() == 0, "no NaN, no allocation on the audio thread");

    // switching the loop off lets the song run on
    r.f.model.setArrangementLoopOn (false);
    r.runBeats (10.0);
    check (r.f.engine.getSongPositionBeats() > 9.0, "with the loop off the song position passes the old loop end");
    r.f.engine.stop();
    r.runner.run (3);
    check (r.log (0)->balance (60) == 0, "no hanging note after the stop");
}

void testSeekAndChase()
{
    std::printf ("[arrangement test 3] seek: playing notes are released and the note under the new position is chased\n");
    Rig r (331, 1);
    auto t = r.trackList[0];
    place (r.f.model, t, 0.0, 64.0, { { 0.0, 64.0, 48, 90 }, { 20.0, 0.5, 72, 100 } }, 64.0); // a pad for the whole song
    r.f.engine.rebuildGraphIfNeeded();
    r.runner.run (2);
    r.f.engine.play();
    r.runBeats (2.0);
    auto* log = r.log (0);
    check (log->ons (48).size() == 1 && log->balance (48) == 1, "the pad sounds");

    r.f.engine.seek (30.0);
    r.runner.run (1);
    check (log->ons (48).size() == 2 && log->offs (48).size() == 1 && log->balance (48) == 1, "after the seek: released once and started once, the pad sounds again");
    const auto on = log->ons (48), off = log->offs (48);
    check (on[1] == off[0] || on[1] - off[0] <= 1, "the new note-on is in the first block after the seek, right behind the note-off");
    check (std::abs (r.f.engine.getSongPositionBeats() - 30.0) < 0.1, "the song position is at the target");

    r.f.engine.seek (20.0);
    r.runner.run (1);
    check (log->ons (72).size() == 1, "a note that starts exactly at the seek position sounds once");
    check (log->ons (48).size() == 3 && log->balance (48) == 1, "and the pad is chased again");

    // a quantised seek waits for the next bar of the monotonic clock and lands on the sample
    r.f.engine.seek (0.0);
    r.runner.run (1);
    const double mono = r.f.engine.getPositionBeats();
    r.f.engine.seekQuantised (40.0);
    r.runBeats (4.5);
    const double boundary = std::ceil (mono / 4.0) * 4.0; // the next multiple of the launch quantisation (4 beats)
    const double expectedSong = 40.0 + (r.f.engine.getPositionBeats() - boundary);
    check (std::abs (r.f.engine.getSongPositionBeats() - expectedSong) < 0.02, "a quantised seek lands on the bar line of the monotonic clock (song " + juce::String (r.f.engine.getSongPositionBeats(), 3) + ")");
    r.f.engine.stop();
    r.runner.run (3);
    check (log->balance (48) == 0, "the stop closes the pad");
}

void testTransportPositions()
{
    std::printf ("[arrangement test] play start, continue, rewind and stop\n");
    Rig r (256, 1);
    r.f.model.setArrangementPlayStart (8.0);
    r.runner.run (2);
    check (! r.f.engine.isPlaying(), "stopped");
    r.f.engine.play();
    r.runBeats (1.0);
    const double first = r.f.engine.getSongPositionBeats();
    check (first > 8.9 && first < 9.2, "play starts the song at the play start (8): " + juce::String (first, 3));
    check (r.f.engine.getPositionBeats() > 0.9 && r.f.engine.getPositionBeats() < 1.2, "the monotonic clock starts at 0");
    r.f.engine.stop();
    r.runner.run (2);
    const double stoppedAt = r.f.engine.getSongPositionBeats();
    check (std::abs (stoppedAt - first) < 0.1 && ! r.f.engine.isPlaying(), "stopped: the song position stays where it stopped");

    r.f.engine.play();
    r.runBeats (0.5);
    check (std::abs (r.f.engine.getSongPositionBeats() - 8.5) < 0.15, "play again goes back to the play start");
    r.f.engine.stop();
    r.runner.run (2);
    const double stoppedAgain = r.f.engine.getSongPositionBeats();
    r.f.engine.playFromPosition();
    r.runBeats (0.5);
    const double continued = r.f.engine.getSongPositionBeats();
    check (std::abs (continued - (stoppedAgain + 0.5)) < 0.05 && stoppedAgain > 8.4, "Shift+Space continues from the position where it stopped (" + juce::String (stoppedAgain, 3) + " -> " + juce::String (continued, 3) + ")");
    r.f.engine.stop();
    r.runner.run (2);
    r.f.engine.rewindSong();
    r.runner.run (1);
    check (eq (r.f.engine.getSongPositionBeats(), 0.0), "rewind puts the song position at 0");

    // seek while stopped moves the point to continue from
    r.f.engine.seek (12.0);
    r.runner.run (1);
    r.f.engine.playFromPosition();
    r.runBeats (0.25);
    check (std::abs (r.f.engine.getSongPositionBeats() - 12.25) < 0.1, "a seek while stopped moves the continue point");
    r.f.engine.stop();
}

void testCountIn()
{
    std::printf ("[arrangement test] the count-in is silent for the arrangement and the song starts at the play start after it\n");
    Rig r (331, 1);
    auto t = r.trackList[0];
    place (r.f.model, t, 0.0, 16.0, { { 0.0, 0.25, 60, 100 } }, 1.0); // a note on every beat from song beat 0
    r.f.model.setCountInBars (1);
    r.f.model.setArrangementPlayStart (4.0);
    r.f.engine.rebuildGraphIfNeeded();
    r.runner.run (2);
    const int64_t origin = r.f.engine.getEngineSample();
    r.f.engine.startRecord (t.id(), 0); // from a stopped transport: one bar of count-in, then the recording
    r.runBeats (8.0);
    const auto on = r.log (0)->ons (60);
    check (! on.empty() && Rig::near (on[0], origin + (int64_t) (4 * kSpb), 1), "the first arrangement note is at the end of the count-in (monotonic beat 0, song beat 4)");
    check (on.size() >= 4 && Rig::near (on[1], on[0] + (int64_t) kSpb, 1), "and the notes follow beat by beat");
    check (r.f.engine.getSongPositionBeats() > 7.5, "the song position is 4 + the time since the end of the count-in (about 8)");
    r.f.engine.stop();
    r.runner.run (3);
}

void testSessionAgainstArrangement()
{
    std::printf ("[arrangement test 5] session against arrangement: a launch silences the track, \"Back to arrangement\" brings it back\n");
    Rig r (256, 2);
    auto a = r.trackList[0], b = r.trackList[1];
    // arrangement: a note on every beat (60) on both tracks and a pad (48) on A; session: a bar of 72 in scene 0 of A
    std::vector<daw::engine::Note> padAndBeats { { 0.0, 64.0, 48, 90 } };
    for (int beat = 0; beat < 64; ++beat)
        padAndBeats.push_back ({ (double) beat, 0.25, 60, 100 });
    place (r.f.model, a, 0.0, 64.0, padAndBeats, 64.0);
    place (r.f.model, b, 0.0, 64.0, { { 0.0, 0.25, 60, 100 } }, 1.0);
    auto slot = r.f.model.createClip (a, 0, 4.0);
    slot.setNotes ({ { 0.0, 0.5, 72, 100 }, { 2.0, 0.5, 72, 100 } }, nullptr);
    r.f.engine.rebuildGraphIfNeeded();

    r.runner.run (2);
    const int64_t origin = r.f.engine.getEngineSample();
    const auto S = [&] (double beat) { return origin + (int64_t) std::llround (beat * kSpb); };
    r.f.engine.play();
    r.runBeats (5.2);
    check (! r.f.engine.anyTrackInSession(), "nothing is in Session View yet");
    r.f.engine.launchClip (a.id(), 0); // quantised: starts at beat 8
    r.runBeats (1.0);
    check (! r.f.engine.anyTrackInSession(), "a queued clip has not taken the track yet");
    r.runBeats (3.0);
    check (r.f.engine.findRuntime (a.id())->inSession.load() && ! r.f.engine.findRuntime (b.id())->inSession.load(), "after the launch track A is in session, track B is not");
    check (r.f.engine.anyTrackInSession(), "the engine reports a track in Session View");

    auto* logA = r.log (0);
    auto* logB = r.log (1);
    bool arrAfterLaunch = false;
    for (const auto s : logA->ons (60))
        arrAfterLaunch = arrAfterLaunch || s >= S (8.0) - 256;
    check (! arrAfterLaunch, "no arrangement note on track A from the start of the session clip on");
    check (! logA->ons (72).empty() && Rig::near (logA->ons (72)[0], S (8.0), 1), "the session clip starts on the bar line (beat 8)");
    check (logB->ons (60).size() >= 8, "track B keeps playing its arrangement");
    check (logA->balance (48) == 0, "the pad of the arrangement was released when the track went to the session");

    // back to arrangement at the next bar line (beat 12); the pad is chased there
    r.f.engine.backToArrangement(); // now at about beat 9.2
    r.runBeats (1.0);
    check (r.f.engine.findRuntime (a.id())->inSession.load(), "until the bar line the track stays in session");
    r.runBeats (3.0);
    check (! r.f.engine.anyTrackInSession() && ! r.f.engine.findRuntime (a.id())->inSession.load(), "after the bar line the arrangement is back");
    const auto on60 = logA->ons (60);
    int64_t firstBack = -1;
    for (const auto s : on60)
        if (s >= S (8.0) - 256)
        {
            firstBack = s;
            break;
        }
    check (firstBack >= 0 && Rig::near (firstBack, S (12.0), 1), "the first arrangement note is back exactly on the bar line (beat 12)");
    const auto on48 = logA->ons (48);
    check (on48.size() == 2 && Rig::near (on48.back(), S (12.0), 1), "the pad is chased at the bar line");
    int sessionAfter = 0;
    for (const auto s : logA->ons (72))
        sessionAfter += s >= S (12.0) ? 1 : 0;
    check (sessionAfter == 0, "the session clip stopped there");
    r.f.engine.stop();
    r.runner.run (3);
    check (logA->balance (72) == 0 && logA->balance (48) == 0 && logA->balance (60) == 0, "no hanging notes");
    check (r.runner.nonFinite == 0, "no NaN");
}

void testStopTrackAndEdits()
{
    std::printf ("[arrangement test] the stop button puts a track in session; editing the arrangement during playback\n");
    Rig r (256, 1);
    auto t = r.trackList[0];
    place (r.f.model, t, 0.0, 64.0, { { 0.0, 64.0, 48, 90 } }, 64.0);
    auto slot = r.f.model.createClip (t, 0, 4.0);
    slot.setNotes ({ { 0.0, 0.5, 72, 100 } }, nullptr);
    r.f.engine.rebuildGraphIfNeeded();
    r.runner.run (2);
    r.f.engine.play();
    r.runBeats (2.0);
    auto* log = r.log (0);
    check (log->ons (48).size() == 1, "the pad sounds");

    // changing something that is not in the arrangement does not touch the pad
    slot.addNote ({ 1.0, 0.5, 74, 100 }, &r.f.model.undo);
    r.f.engine.rebuildGraphIfNeeded();
    r.runBeats (0.5);
    check (log->ons (48).size() == 1, "editing a session clip does not restart the arrangement");

    // changing the arrangement releases and chases
    auto a = t.arrangementClips()[0];
    a.clip().addNote ({ 10.0, 1.0, 55, 100 }, &r.f.model.undo);
    r.f.engine.rebuildGraphIfNeeded();
    r.runBeats (0.5);
    check (log->ons (48).size() == 2 && log->balance (48) == 1, "an edit of the arrangement: the pad is released and chased once");

    r.f.engine.stopTrack (t.id());
    r.runBeats (0.5);
    check (r.f.engine.findRuntime (t.id())->inSession.load() && log->balance (48) == 0, "the stop button of the track puts it in session: the pad is released");
    r.runBeats (4.0);
    check (log->ons (48).size() == 2, "and the arrangement stays silent");
    r.f.engine.backToArrangement();
    r.runBeats (4.2);
    check (! r.f.engine.anyTrackInSession() && log->ons (48).size() == 3 && log->balance (48) == 1, "Back to arrangement: the pad is back");
    r.f.engine.stop();
    r.runner.run (3);

    // stopped: back to arrangement works at once
    r.f.engine.stopTrack (t.id());
    r.runner.run (2);
    check (r.f.engine.anyTrackInSession(), "stopped transport: the stop button still marks the track");
    r.f.engine.backToArrangement();
    r.runner.run (2);
    check (! r.f.engine.anyTrackInSession(), "and Back to arrangement clears it at once");
}

// Live-test request after the demo song: each track switches between the timeline and Session clips on its own.
void testPerTrackSwitch()
{
    std::printf ("[arrangement test] per-track switch: one track goes live on the next bar, comes back alone\n");
    Rig r (256, 2);
    auto a = r.trackList[0], b = r.trackList[1];
    std::vector<daw::engine::Note> beats;
    for (int beat = 0; beat < 64; ++beat)
        beats.push_back ({ (double) beat, 0.25, 60, 100 });
    place (r.f.model, a, 0.0, 64.0, beats, 64.0);
    place (r.f.model, b, 0.0, 64.0, beats, 64.0);
    for (auto t : { a, b })
    {
        auto slot = r.f.model.createClip (t, 0, 4.0);
        slot.setNotes ({ { 0.0, 0.5, 72, 100 } }, nullptr);
    }
    r.f.model.setLaunchQuantise (0.0); // no launch quantisation: the switch still waits for the bar
    r.f.engine.rebuildGraphIfNeeded();

    r.runner.run (2);
    const int64_t origin = r.f.engine.getEngineSample();
    const auto S = [&] (double beat) { return origin + (int64_t) std::llround (beat * kSpb); };
    r.f.engine.play();
    r.runBeats (1.3);
    r.f.engine.launchClipOnBar (a.id(), 0);
    r.runBeats (1.0);
    check (! r.f.engine.findRuntime (a.id())->inSession.load(), "before the bar line track A still plays the timeline");
    r.runBeats (2.0);
    auto* logA = r.log (0);
    check (! logA->ons (72).empty() && Rig::near (logA->ons (72)[0], S (4.0), 1), "its Session clip starts exactly on the bar (beat 4)");
    r.f.engine.launchClipOnBar (b.id(), 0);
    r.runBeats (4.0);
    check (r.f.engine.findRuntime (b.id())->inSession.load(), "track B is live too");

    // only A comes back (no quantisation: at once); B stays in Session
    r.f.engine.backToArrangement (a.id());
    r.runBeats (1.0);
    check (! r.f.engine.findRuntime (a.id())->inSession.load(), "track A is back on the timeline");
    check (r.f.engine.findRuntime (b.id())->inSession.load(), "track B still plays its Session clip");
    const auto before = logA->ons (60).size();
    r.runBeats (2.0);
    check (logA->ons (60).size() >= before + 2, "and A plays its timeline notes again");
    r.f.engine.stop();
    r.runner.run (3);
    check (logA->balance (72) == 0 && logA->balance (60) == 0 && r.log (1)->balance (72) == 0, "no hanging notes");
}

// x2 and /2 of the clip editor (live-test request after stage 3)
void testScaleClipTime()
{
    std::printf ("[arrangement test] x2 and /2 stretch a clip, its notes and its place on the timeline\n");
    {
        TwoTracks p; // a slot clip
        auto slot = p.m.createClip (p.a, 0, 4.0, "Riff");
        slot.setNotes ({ { 0.0, 1.0, 60, 100 }, { 2.0, 0.5, 64, 90 } }, &p.m.undo);
        slot.setLoopStart (1.0, &p.m.undo);
        slot.setControllers ({ { 3.0, 0xB0, 1, 64 } }, &p.m.undo);
        p.m.undo.clearUndoHistory();
        p.m.undo.beginNewTransaction ("x2");
        arrangement::scaleClipTime (p.m, slot, 2.0);
        check (eq (slot.lengthBeats(), 8.0) && eq (slot.loopStart(), 2.0) && eq (slot.note (1).start, 4.0) && eq (slot.note (1).length, 1.0)
                   && eq (slot.controllers()[0].time, 6.0),
               "x2: length, loop start, notes and controllers twice as long");
        p.m.undo.beginNewTransaction ("/2");
        arrangement::scaleClipTime (p.m, slot, 0.5);
        p.m.undo.beginNewTransaction ("/2 again");
        arrangement::scaleClipTime (p.m, slot, 0.5);
        check (eq (slot.lengthBeats(), 2.0) && eq (slot.note (1).start, 1.0) && eq (slot.note (1).length, 0.25), "/2 twice after x2: half of the original");
        p.m.undo.undo();
        p.m.undo.undo();
        check (eq (slot.lengthBeats(), 8.0) && eq (slot.note (1).start, 4.0), "each press is one undo step");
    }
    {
        TwoTracks p; // an arrangement clip stretches on the timeline and covers the clip after it
        const auto first = place (p.m, p.a, 0.0, 4.0, { { 3.0, 1.0, 60, 100 } }, 4.0);
        place (p.m, p.a, 4.0, 8.0);
        p.m.undo.clearUndoHistory();
        undoable (p.m, "x2 on the timeline", [&] { arrangement::scaleClipTime (p.m, first.clip(), 2.0); });
        const auto clips = p.a.arrangementClips();
        check (clips.size() == 2 && eq (clips[0].length(), 8.0) && eq (clips[0].clip().note (0).start, 6.0) && eq (clips[1].start(), 8.0)
                   && eq (clips[1].end(), 12.0) && noOverlaps (p.m),
               "the clip is 8 beats long now, the next one is trimmed to start after it");
    }
}
} // namespace

void tests::runArrangementTests()
{
    testOverlaps();
    testEditing();
    testSplitAndJoin();
    testTime();
    testSessionAndMarkers();
    testSchema();
    testPlayback();
    testLoopThroughEngine();
    testSeekAndChase();
    testTransportPositions();
    testCountIn();
    testSessionAgainstArrangement();
    testStopTrackAndEdits();
    testPerTrackSwitch();
    testScaleClipTime();
}
