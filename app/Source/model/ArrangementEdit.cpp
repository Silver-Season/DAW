#include "ArrangementEdit.h"

#include "../Strings.h"

#include "daw/engine/Arrangement.h"

#include <algorithm>
#include <cmath>

namespace arrangement
{
namespace
{
constexpr double kEps = 1.0e-9;

// The offset of a clip whose start moves by `delta` beats while its content stays where it is in time, in the
// canonical range of the content.
double shiftedOffset (const ArrangementClipRef& c, double delta)
{
    return daw::engine::normaliseOffset (c.clip().toMidiClip(), c.offset() + delta, c.loop());
}

// A fresh, empty CLIP node for the content of an arrangement clip; not in the model yet.
juce::ValueTree makeContent (ProjectModel& model, const TrackRef& track, double lengthBeats, const juce::String& name)
{
    juce::ValueTree c (ids::CLIP);
    c.setProperty (ids::id, (juce::int64) model.newId(), nullptr);
    c.setProperty (ids::name, name.isEmpty() ? Strings::defaultClipName : name, nullptr);
    c.setProperty (ids::colour, track.colour(), nullptr);
    c.setProperty (ids::lengthBeats, lengthBeats, nullptr);
    c.setProperty (ids::loopStart, 0.0, nullptr);
    c.setProperty (ids::loopEnd, lengthBeats, nullptr);
    c.setProperty (ids::startOffset, 0.0, nullptr);
    c.setProperty (ids::loop, true, nullptr);
    c.setProperty (ids::ownScale, false, nullptr);
    c.setProperty (ids::scaleRoot, model.scaleRoot(), nullptr);
    c.setProperty (ids::scaleMode, model.scaleMode(), nullptr);
    return c;
}

// Adds an ACLIP holding `content` (a CLIP tree with fresh ids that is not in the model yet) to the track.
ArrangementClipRef addAClip (ProjectModel& model, const TrackRef& track, const juce::ValueTree& content, double start, double length,
                             double offset, bool loop)
{
    juce::ValueTree a (ids::ACLIP);
    a.setProperty (ids::id, (juce::int64) model.newId(), nullptr);
    a.setProperty (ids::start, start, nullptr);
    a.setProperty (ids::length, length, nullptr);
    a.setProperty (ids::offset, offset, nullptr);
    a.setProperty (ids::loop, loop, nullptr);
    a.appendChild (content, nullptr);
    auto node = track.arrangementNode();
    jassert (node.isValid());
    node.appendChild (a, &model.undo);
    return ArrangementClipRef (a);
}

void removeClip (ProjectModel& model, const ArrangementClipRef& c)
{
    auto parent = c.state.getParent();
    if (parent.isValid())
        parent.removeChild (c.state, &model.undo);
}

// A piece of a clip, as the overlap code needs to cut it.
ArrangementClipRef makePiece (ProjectModel& model, const TrackRef& track, const ArrangementClipRef& source, double start, double end)
{
    return addAClip (model, track, model.copyWithNewIds (source.clip().state), start, end - start, shiftedOffset (source, start - source.start()),
                     source.loop());
}

// Removes [from, to) from every clip of the track that is not in `keep`.
void carve (ProjectModel& model, const TrackRef& track, double from, double to, const std::set<int64_t>& keep)
{
    for (const auto& c : track.arrangementClips())
    {
        if (keep.count (c.id()) > 0)
            continue;
        const double s = c.start(), e = c.end();
        if (e <= from + kEps || s >= to - kEps)
            continue; // does not touch the range

        const bool keepsLeft = s < from - kEps && from - s >= kSliverBeats;
        const bool keepsRight = e > to + kEps && e - to >= kSliverBeats;
        if (keepsLeft && keepsRight)
        {
            // The range lies inside the clip: the part after it becomes a clip of its own.
            makePiece (model, track, c, to, e);
            c.setLength (from - s, &model.undo);
        }
        else if (keepsLeft)
            c.setLength (from - s, &model.undo);
        else if (keepsRight)
        {
            const double offset = shiftedOffset (c, to - s);
            c.setStart (to, &model.undo);
            c.setLength (e - to, &model.undo);
            c.setOffset (offset, &model.undo);
        }
        else
            removeClip (model, c);
    }
}

// Every arrangement clip of the project.
Clips allClips (const ProjectModel& model)
{
    Clips out;
    for (const auto& t : model.tracks())
        for (const auto& c : t.arrangementClips())
            out.push_back (c);
    return out;
}
} // namespace

//==============================================================================
ArrangementClipRef findClip (const ProjectModel& model, int64_t aclipId)
{
    for (const auto& t : model.tracksNode())
        for (const auto& c : t.getChildWithName (ids::ARRANGEMENT))
            if (c.hasType (ids::ACLIP) && (int64_t) (juce::int64) c.getProperty (ids::id, -1) == aclipId)
                return ArrangementClipRef (c);
    return {};
}

TrackRef trackOf (const ProjectModel&, const ArrangementClipRef& clip)
{
    return TrackRef (clip.state.getParent().getParent());
}

ArrangementClipRef clipAt (const TrackRef& track, double beat)
{
    for (const auto& c : track.arrangementClips())
        if (c.start() <= beat + kEps && beat < c.end() - kEps)
            return c;
    return {};
}

Clips clipsIn (const TrackRef& track, double from, double to)
{
    Clips out;
    for (const auto& c : track.arrangementClips())
        if (c.end() > from + kEps && c.start() < to - kEps)
            out.push_back (c);
    return out;
}

double songLengthBeats (const ProjectModel& model)
{
    double end = 0.0;
    for (const auto& t : model.tracks())
        for (const auto& c : t.arrangementClips())
            end = std::max (end, c.end());
    for (const auto& m : model.markers())
        end = std::max (end, m.beat());
    return std::max (end, (double) kMinSongBars * model.beatsPerBar());
}

//==============================================================================
void resolveOverlaps (ProjectModel& model, const TrackRef& track, int64_t winnerId)
{
    const auto winner = findClip (model, winnerId);
    if (! winner.isValid() || trackOf (model, winner) != track)
        return;
    carve (model, track, winner.start(), winner.end(), { winnerId });
}

void clearRange (ProjectModel& model, const TrackRef& track, double from, double to)
{
    if (to > from)
        carve (model, track, from, to, {});
}

//==============================================================================
ArrangementClipRef addEmptyClip (ProjectModel& model, const TrackRef& track, double start, double lengthBeats, const juce::String& name)
{
    if (! track.arrangementNode().isValid())
        return {};
    start = std::max (0.0, start);
    lengthBeats = std::max (kMinClipBeats, lengthBeats);

    auto a = addAClip (model, track, makeContent (model, track, lengthBeats, name), start, lengthBeats, 0.0, true);
    resolveOverlaps (model, track, a.id());
    return a;
}

ArrangementClipRef addRecordedClip (ProjectModel& model, const TrackRef& track, double start, double lengthBeats,
                                    const std::vector<RecordedPass>& passes, int activePass, const juce::String& name)
{
    if (! track.arrangementNode().isValid() || passes.empty())
        return {};
    start = std::max (0.0, start);
    lengthBeats = std::max (kMinClipBeats, lengthBeats);
    activePass = std::clamp (activePass, 0, (int) passes.size() - 1);

    // Contents are built detached (no undo needed), then the whole ACLIP is appended in one undoable step.
    const auto content = [&] (const RecordedPass& pass) {
        auto c = makeContent (model, track, lengthBeats, name);
        ClipRef ref (c);
        ref.setNotes (pass.notes, nullptr);
        ref.setControllers (pass.controllers, nullptr);
        return c;
    };

    juce::ValueTree a (ids::ACLIP);
    a.setProperty (ids::id, (juce::int64) model.newId(), nullptr);
    a.setProperty (ids::start, start, nullptr);
    a.setProperty (ids::length, lengthBeats, nullptr);
    a.setProperty (ids::offset, 0.0, nullptr);
    a.setProperty (ids::loop, true, nullptr);
    a.appendChild (content (passes[(size_t) activePass]), nullptr);
    if (passes.size() > 1)
    {
        a.setProperty (ids::take, activePass + 1, nullptr);
        juce::ValueTree takes (ids::TAKES);
        for (int i = 0; i < (int) passes.size(); ++i)
        {
            if (i == activePass)
                continue;
            juce::ValueTree t (ids::TAKE);
            t.setProperty (ids::id, (juce::int64) model.newId(), nullptr);
            t.setProperty (ids::number, i + 1, nullptr);
            t.appendChild (content (passes[(size_t) i]), nullptr);
            takes.appendChild (t, nullptr);
        }
        a.appendChild (takes, nullptr);
    }
    auto node = track.arrangementNode();
    node.appendChild (a, &model.undo);
    resolveOverlaps (model, track, (int64_t) (juce::int64) a.getProperty (ids::id));
    return ArrangementClipRef (a);
}

std::vector<TakeInfo> takesOf (const ArrangementClipRef& clip)
{
    std::vector<TakeInfo> out;
    const auto takes = clip.takesNode();
    if (! takes.isValid() || clip.activeTake() <= 0)
        return out;
    out.push_back ({ clip.activeTake(), true });
    for (const auto& t : takes)
        out.push_back ({ (int) t.getProperty (ids::number, 0), false });
    std::sort (out.begin(), out.end(), [] (const auto& x, const auto& y) { return x.number < y.number; });
    return out;
}

bool switchTake (ProjectModel& model, const ArrangementClipRef& clip, int number)
{
    const auto takes = clip.takesNode();
    if (! takes.isValid() || number == clip.activeTake())
        return false;
    for (auto t : takes)
    {
        if ((int) t.getProperty (ids::number, 0) != number)
            continue;
        auto taken = t.getChildWithName (ids::CLIP);
        auto shown = clip.state.getChildWithName (ids::CLIP);
        if (! taken.isValid() || ! shown.isValid())
            return false;
        // The two contents trade places, so every take keeps its identity and nothing is copied.
        t.removeChild (taken, &model.undo);
        clip.state.removeChild (shown, &model.undo);
        clip.state.appendChild (taken, &model.undo);
        t.appendChild (shown, &model.undo);
        t.setProperty (ids::number, clip.activeTake(), &model.undo);
        clip.state.setProperty (ids::take, number, &model.undo);
        return true;
    }
    return false;
}

ArrangementClipRef addSlotClip (ProjectModel& model, const TrackRef& track, const ClipRef& source, double start)
{
    if (! track.arrangementNode().isValid() || ! source.isValid())
        return {};
    const double length = std::max (kMinClipBeats, source.lengthBeats());
    auto a = addAClip (model, track, model.copyWithNewIds (source.state), std::max (0.0, start), length, 0.0, source.loopEnabled());
    resolveOverlaps (model, track, a.id());
    return a;
}

ArrangementClipRef addSlotClipPart (ProjectModel& model, const TrackRef& track, const ClipRef& source, double start, double length, double offset)
{
    if (! track.arrangementNode().isValid() || ! source.isValid())
        return {};
    auto a = addAClip (model, track, model.copyWithNewIds (source.state), std::max (0.0, start), std::max (kMinClipBeats, length),
                       std::max (0.0, offset), source.loopEnabled());
    resolveOverlaps (model, track, a.id());
    return a;
}

ArrangementClipRef copyClip (ProjectModel& model, const ArrangementClipRef& source, const TrackRef& dest, double start)
{
    return pasteClip (model, dest, source.state, start);
}

ArrangementClipRef pasteClip (ProjectModel& model, const TrackRef& dest, const juce::ValueTree& snapshot, double start)
{
    if (! dest.arrangementNode().isValid() || ! snapshot.hasType (ids::ACLIP))
        return {};
    const ArrangementClipRef source (snapshot);
    if (! source.clip().isValid())
        return {};
    auto a = addAClip (model, dest, model.copyWithNewIds (source.clip().state), std::max (0.0, start), source.length(), source.offset(), source.loop());
    resolveOverlaps (model, dest, a.id());
    return a;
}

ArrangementClipRef duplicateAfter (ProjectModel& model, const ArrangementClipRef& clip)
{
    const auto track = trackOf (model, clip);
    return track.isValid() ? copyClip (model, clip, track, clip.end()) : ArrangementClipRef();
}

Clips duplicateRange (ProjectModel& model, const std::vector<TrackRef>& tracks, double from, double to)
{
    Clips out;
    if (to <= from)
        return out;
    const double shift = to - from;
    for (const auto& track : tracks)
    {
        // Read what lies in the range first: the copies land behind it and would be found otherwise.
        struct Piece
        {
            ArrangementClipRef source;
            double start, end;
        };
        std::vector<Piece> pieces;
        for (const auto& c : clipsIn (track, from, to))
            pieces.push_back ({ c, std::max (c.start(), from), std::min (c.end(), to) });

        std::vector<ArrangementClipRef> made;
        for (const auto& p : pieces)
            if (p.end - p.start >= kSliverBeats)
                made.push_back (addAClip (model, track, model.copyWithNewIds (p.source.clip().state), p.start + shift, p.end - p.start,
                                          shiftedOffset (p.source, p.start - p.source.start()), p.source.loop()));
        std::set<int64_t> keep;
        for (const auto& m : made)
            keep.insert (m.id());
        carve (model, track, from + shift, to + shift, keep);
        for (const auto& m : made)
            out.push_back (m);
    }
    return out;
}

//==============================================================================
void moveClips (ProjectModel& model, const std::vector<Move>& moves)
{
    if (moves.empty())
        return;
    double minStart = 1.0e18;
    for (const auto& m : moves)
        minStart = std::min (minStart, m.start);
    const double shift = minStart < 0.0 ? -minStart : 0.0;

    std::set<int64_t> moved;
    std::vector<TrackRef> targets;
    for (const auto& m : moves)
    {
        if (! m.clip.isValid() || ! m.track.arrangementNode().isValid())
            continue;
        moved.insert (m.clip.id());
        if (std::find (targets.begin(), targets.end(), m.track) == targets.end())
            targets.push_back (m.track);

        auto target = m.track.arrangementNode();
        if (m.clip.state.getParent() != target)
        {
            m.clip.state.getParent().removeChild (m.clip.state, &model.undo);
            target.appendChild (m.clip.state, &model.undo);
        }
        m.clip.setStart (m.start + shift, &model.undo);
    }
    // Only now, with the whole group in place, the group cuts what lies under it.
    for (const auto& m : moves)
        if (m.clip.isValid() && m.track.arrangementNode().isValid())
            carve (model, m.track, m.clip.start(), m.clip.end(), moved);
}

void resizeRight (ProjectModel& model, const ArrangementClipRef& clip, double newEnd)
{
    const auto track = trackOf (model, clip);
    if (! track.isValid())
        return;
    clip.setLength (std::max (kMinClipBeats, newEnd - clip.start()), &model.undo);
    resolveOverlaps (model, track, clip.id());
}

void resizeLeft (ProjectModel& model, const ArrangementClipRef& clip, double newStart)
{
    const auto track = trackOf (model, clip);
    if (! track.isValid())
        return;
    const double end = clip.end();
    newStart = std::clamp (newStart, 0.0, end - kMinClipBeats);
    // Without looping there is nothing in front of the content to pull into view.
    if (! clip.loop())
        newStart = std::max (newStart, clip.start() - clip.offset());

    const double offset = shiftedOffset (clip, newStart - clip.start());
    clip.setStart (newStart, &model.undo);
    clip.setLength (end - newStart, &model.undo);
    clip.setOffset (offset, &model.undo);
    resolveOverlaps (model, track, clip.id());
}

//==============================================================================
ArrangementClipRef splitAt (ProjectModel& model, const ArrangementClipRef& clip, double beat)
{
    const auto track = trackOf (model, clip);
    if (! track.isValid() || beat - clip.start() < kSliverBeats || clip.end() - beat < kSliverBeats)
        return {};
    const double end = clip.end();
    auto right = makePiece (model, track, clip, beat, end);
    clip.setLength (beat - clip.start(), &model.undo);
    return right;
}

int splitTracks (ProjectModel& model, const std::vector<TrackRef>& tracks, const std::vector<double>& beats)
{
    int cuts = 0;
    for (const auto& track : tracks)
        for (const double beat : beats)
            if (const auto c = clipAt (track, beat); c.isValid() && splitAt (model, c, beat).isValid())
                ++cuts;
    return cuts;
}

ArrangementClipRef joinClips (ProjectModel& model, const Clips& input)
{
    Clips clips;
    for (const auto& c : input)
        if (c.isValid() && c.clip().isValid())
            clips.push_back (c);
    if (clips.size() < 2)
        return {};
    const auto track = trackOf (model, clips.front());
    for (const auto& c : clips)
        if (trackOf (model, c) != track)
            return {};
    std::sort (clips.begin(), clips.end(), [] (const auto& a, const auto& b) { return a.start() < b.start(); });

    const double start = clips.front().start();
    double end = start;
    for (const auto& c : clips)
        end = std::max (end, c.end());

    // The content takes its name, colour and scale from the first clip.
    auto content = model.copyWithNewIds (clips.front().clip().state);
    ClipRef merged (content);
    std::vector<daw::engine::Note> notes;
    std::vector<daw::engine::ControllerEvent> controllers;
    for (const auto& c : clips)
    {
        const auto flat = daw::engine::flattenArrangementClip (c.clip().toMidiClip(), c.offset(), c.loop(), c.length());
        for (auto n : flat.notes)
        {
            n.start += c.start() - start;
            notes.push_back (n);
        }
        for (auto e : flat.controllers)
        {
            e.time += c.start() - start;
            controllers.push_back (e);
        }
    }
    // The new content is not in the model yet, so these edits need no undo; adding it below is the one undoable step.
    merged.setLengthBeats (end - start, nullptr);
    merged.setLoopStart (0.0, nullptr);
    merged.setStartOffset (0.0, nullptr);
    merged.setLoopEnabled (true, nullptr);
    merged.setNotes (notes, nullptr);
    merged.setControllers (controllers, nullptr);

    for (const auto& c : clips)
        removeClip (model, c);
    auto joined = addAClip (model, track, content, start, end - start, 0.0, true);
    resolveOverlaps (model, track, joined.id()); // a clip between the joined ones that was not selected gives way
    return joined;
}

void deleteClips (ProjectModel& model, const Clips& clips)
{
    for (const auto& c : clips)
        if (c.isValid())
            removeClip (model, c);
}

//==============================================================================
void insertTime (ProjectModel& model, double atBeat, double lengthBeats)
{
    if (lengthBeats <= 0.0)
        return;
    atBeat = std::max (0.0, atBeat);
    for (const auto& track : model.tracks())
        for (const auto& c : track.arrangementClips())
        {
            if (c.end() <= atBeat + kEps)
                continue;
            if (c.start() >= atBeat - kEps)
                c.setStart (c.start() + lengthBeats, &model.undo);
            else if (const auto right = splitAt (model, c, atBeat); right.isValid())
                right.setStart (atBeat + lengthBeats, &model.undo);
            // else: the insert point is within a sliver of the clip's start or end and the clip stays as it is
        }
    for (const auto& m : model.markers())
        if (m.beat() >= atBeat - kEps)
            m.setBeat (m.beat() + lengthBeats, &model.undo);
}

void removeTime (ProjectModel& model, double fromBeat, double toBeat)
{
    fromBeat = std::max (0.0, fromBeat);
    if (toBeat <= fromBeat)
        return;
    const double length = toBeat - fromBeat;
    for (const auto& track : model.tracks())
        carve (model, track, fromBeat, toBeat, {});
    for (const auto& c : allClips (model))
        if (c.start() >= toBeat - kEps)
            c.setStart (c.start() - length, &model.undo);
    for (const auto& m : model.markers())
    {
        if (m.beat() >= toBeat - kEps)
            m.setBeat (m.beat() - length, &model.undo);
        else if (m.beat() >= fromBeat - kEps)
            m.state.getParent().removeChild (m.state, &model.undo);
    }
}

//==============================================================================
int sendToSession (ProjectModel& model, const ArrangementClipRef& clip, int fromScene)
{
    const auto track = trackOf (model, clip);
    if (! track.isValid() || ! clip.clip().isValid())
        return -1;

    int scene = std::max (0, fromScene);
    while (scene < model.numScenes() && track.clipAt (scene).isValid())
        ++scene;
    if (scene >= model.numScenes())
    {
        model.insertScene (-1);
        scene = model.numScenes() - 1;
    }

    // What plays in the arrangement, written out: the slot clip sounds like the arrangement clip, repeats included.
    const auto flat = daw::engine::flattenArrangementClip (clip.clip().toMidiClip(), clip.offset(), clip.loop(), clip.length());
    auto copy = model.setClip (track, scene, clip.clip());
    copy.setLengthBeats (clip.length(), &model.undo);
    copy.setLoopStart (0.0, &model.undo);
    copy.setStartOffset (0.0, &model.undo);
    copy.setLoopEnabled (true, &model.undo);
    copy.setNotes (flat.notes, &model.undo);
    copy.setControllers (flat.controllers, &model.undo);
    return scene;
}

//==============================================================================
MarkerRef addMarker (ProjectModel& model, double beat, const juce::String& name)
{
    auto node = model.markersNode();
    if (! node.isValid())
        return {};
    juce::ValueTree m (ids::MARKER);
    m.setProperty (ids::id, (juce::int64) model.newId(), nullptr);
    m.setProperty (ids::beat, std::max (0.0, beat), nullptr);
    m.setProperty (ids::name, name.isEmpty() ? Strings::defaultMarkerName (node.getNumChildren() + 1) : name, nullptr);
    m.setProperty (ids::colour, -1, nullptr);
    node.appendChild (m, &model.undo);
    return MarkerRef (m);
}

void renameMarker (ProjectModel& model, const MarkerRef& m, const juce::String& name)
{
    if (m.isValid())
        m.setName (name, &model.undo);
}

void moveMarker (ProjectModel& model, const MarkerRef& m, double beat)
{
    if (m.isValid())
        m.setBeat (std::max (0.0, beat), &model.undo);
}

void setMarkerColour (ProjectModel& model, const MarkerRef& m, int paletteIndex)
{
    if (m.isValid())
        m.setColour (paletteIndex, &model.undo);
}

void deleteMarker (ProjectModel& model, const MarkerRef& m)
{
    if (m.isValid() && m.state.getParent().isValid())
        m.state.getParent().removeChild (m.state, &model.undo);
}

MarkerRef previousMarker (const ProjectModel& model, double beat)
{
    MarkerRef found;
    for (const auto& m : model.markers()) // sorted by beat
        if (m.beat() < beat - kEps)
            found = m;
    return found;
}

MarkerRef nextMarker (const ProjectModel& model, double beat)
{
    for (const auto& m : model.markers())
        if (m.beat() > beat + kEps)
            return m;
    return {};
}
} // namespace arrangement
