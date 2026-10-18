#include "ModelRefs.h"

#include <algorithm>

namespace
{
// Strict ordering of notes in a clip: by start, then pitch.
bool noteBefore (const daw::engine::Note& a, const daw::engine::Note& b)
{
    if (a.start < b.start)
        return true;
    if (b.start < a.start)
        return false;
    return a.pitch < b.pitch;
}

juce::ValueTree makeNoteNode (const daw::engine::Note& n)
{
    juce::ValueTree v (ids::NOTE);
    v.setProperty (ids::start, n.start, nullptr);
    v.setProperty (ids::length, n.length, nullptr);
    v.setProperty (ids::pitch, n.pitch, nullptr);
    v.setProperty (ids::velocity, n.velocity, nullptr);
    return v;
}
} // namespace

daw::engine::Note ClipRef::readNote (const juce::ValueTree& n)
{
    daw::engine::Note out;
    out.start = (double) n.getProperty (ids::start, 0.0);
    out.length = (double) n.getProperty (ids::length, 1.0);
    out.pitch = (int) n.getProperty (ids::pitch, 60);
    out.velocity = (int) n.getProperty (ids::velocity, 100);
    return out;
}

daw::engine::MidiClip ClipRef::toMidiClip() const
{
    daw::engine::MidiClip c;
    c.id = id();
    c.name = name().toStdString();
    c.lengthBeats = lengthBeats();
    c.loopStart = loopStart();
    c.startOffset = startOffset();
    c.loop = loopEnabled();
    c.notes = notes();
    c.controllers = controllers();
    return c;
}

daw::engine::Note ClipRef::note (int index) const
{
    return readNote (state.getChild (index));
}

std::vector<daw::engine::Note> ClipRef::notes() const
{
    std::vector<daw::engine::Note> out;
    out.reserve ((size_t) numNotes());
    for (int i = 0; i < numNotes(); ++i)
        out.push_back (note (i));
    return out;
}

juce::ValueTree ClipRef::addNote (const daw::engine::Note& n, juce::UndoManager* um) const
{
    // After the last note that is not after `n`, so equal notes keep insertion order.
    int index = numNotes();
    while (index > 0 && noteBefore (n, note (index - 1)))
        --index;
    auto node = makeNoteNode (n);
    state.addChild (node, index, um);
    return node;
}

void ClipRef::updateNote (const juce::ValueTree& noteNode, const daw::engine::Note& n, juce::UndoManager* um) const
{
    if (! hasNote (noteNode))
        return;
    auto node = noteNode; // a handle to the same node
    const auto old = readNote (node);
    if (! juce::exactlyEqual (old.start, n.start))
        node.setProperty (ids::start, n.start, um);
    if (! juce::exactlyEqual (old.length, n.length))
        node.setProperty (ids::length, n.length, um);
    if (old.pitch != n.pitch)
        node.setProperty (ids::pitch, n.pitch, um);
    if (old.velocity != n.velocity)
        node.setProperty (ids::velocity, n.velocity, um);

    // The other notes are sorted, so the node belongs after every one of them that is not after it.
    const int current = state.indexOf (node);
    int target = 0;
    for (int i = 0; i < numNotes(); ++i)
        if (i != current && ! noteBefore (n, note (i)))
            ++target;
    if (target != current)
        state.moveChild (current, target, um);
}

void ClipRef::removeNote (const juce::ValueTree& node, juce::UndoManager* um) const
{
    if (hasNote (node))
        state.removeChild (node, um);
}

void ClipRef::removeNote (int index, juce::UndoManager* um) const
{
    state.removeChild (index, um);
}

void ClipRef::updateNote (int index, const daw::engine::Note& n, juce::UndoManager* um) const
{
    removeNote (index, um);
    addNote (n, um);
}

void ClipRef::setNotes (const std::vector<daw::engine::Note>& list, juce::UndoManager* um) const
{
    for (int i = numNotes() - 1; i >= 0; --i)
        state.removeChild (i, um); // the controllers stay
    for (const auto& n : list)
        addNote (n, um);
}

juce::Array<DeviceRef> TrackRef::devices() const
{
    juce::Array<DeviceRef> out;
    for (const auto& d : devicesNode())
        out.add (DeviceRef (d));
    return out;
}

ClipRef TrackRef::clipAt (int scene) const
{
    const auto slot = slotsNode().getChild (scene);
    return ClipRef (slot.isValid() ? slot.getChildWithName (ids::CLIP) : juce::ValueTree());
}

std::vector<ArrangementClipRef> TrackRef::arrangementClips() const
{
    std::vector<ArrangementClipRef> out;
    for (const auto& c : arrangementNode())
        if (c.hasType (ids::ACLIP))
            out.emplace_back (c);
    std::stable_sort (out.begin(), out.end(), [] (const auto& a, const auto& b) { return a.start() < b.start(); });
    return out;
}

//==============================================================================
std::vector<daw::engine::ControllerEvent> ClipRef::controllers() const
{
    std::vector<daw::engine::ControllerEvent> out;
    const auto node = state.getChildWithName (ids::CONTROLLERS);
    out.reserve ((size_t) node.getNumChildren());
    for (const auto& c : node)
    {
        daw::engine::ControllerEvent e;
        e.time = (double) c.getProperty (ids::time, 0.0);
        const int value = (int) c.getProperty (ids::value, 0);
        if (c.hasType (ids::CC))
        {
            e.status = 0xB0;
            e.data1 = (uint8_t) juce::jlimit (0, 127, (int) c.getProperty (ids::number, 1));
            e.data2 = (uint8_t) juce::jlimit (0, 127, value);
        }
        else if (c.hasType (ids::BEND))
        {
            const int v = juce::jlimit (0, 16383, value);
            e.status = 0xE0;
            e.data1 = (uint8_t) (v & 0x7F);
            e.data2 = (uint8_t) (v >> 7);
        }
        else if (c.hasType (ids::PRESSURE))
        {
            e.status = 0xD0;
            e.data1 = (uint8_t) juce::jlimit (0, 127, value);
        }
        else
            continue;
        out.push_back (e);
    }
    std::stable_sort (out.begin(), out.end(), [] (const auto& a, const auto& b) { return a.time < b.time; });
    return out;
}

std::vector<daw::engine::WarpMarker> ClipRef::warpMarkers() const
{
    std::vector<daw::engine::WarpMarker> out;
    for (const auto& m : state.getChildWithName (ids::WARP))
        out.push_back ({ (double) m.getProperty (ids::sample, 0.0), (double) m.getProperty (ids::beat, 0.0) });
    std::sort (out.begin(), out.end(), [] (const auto& a, const auto& b) { return a.beat < b.beat; });
    return out;
}

void ClipRef::setWarpMarkers (const std::vector<daw::engine::WarpMarker>& list, juce::UndoManager* um) const
{
    auto node = state.getChildWithName (ids::WARP);
    if (list.empty())
    {
        if (node.isValid())
            state.removeChild (node, um);
        return;
    }
    if (! node.isValid())
    {
        node = juce::ValueTree (ids::WARP);
        state.addChild (node, numNotes(), um); // after the notes, before CONTROLLERS
    }
    // Rewrite in place: a marker being dragged keeps its node, so the undo step holds only what changed.
    while (node.getNumChildren() > (int) list.size())
        node.removeChild (node.getNumChildren() - 1, um);
    while (node.getNumChildren() < (int) list.size())
        node.appendChild (juce::ValueTree (ids::MARKER), um);
    for (size_t i = 0; i < list.size(); ++i)
    {
        auto m = node.getChild ((int) i);
        if (! juce::exactlyEqual ((double) m.getProperty (ids::sample, -1.0), list[i].frame))
            m.setProperty (ids::sample, list[i].frame, um);
        if (! juce::exactlyEqual ((double) m.getProperty (ids::beat, -1.0e9), list[i].beat))
            m.setProperty (ids::beat, list[i].beat, um);
    }
}

daw::engine::WarpMap ClipRef::warpMap (double fileRate, double projectTempo) const
{
    const double fpb = juce::jmax (1.0, fileRate) * 60.0 / juce::jmax (1.0, contentTempo (projectTempo));
    if (! followTempo())
        return { sampleStart(), fpb };
    return { warpMarkers(), sampleStart(), fpb };
}

void ClipRef::setControllers (const std::vector<daw::engine::ControllerEvent>& list, juce::UndoManager* um) const
{
    const auto old = state.getChildWithName (ids::CONTROLLERS);
    if (old.isValid())
        state.removeChild (old, um);
    if (list.empty())
        return;

    juce::ValueTree node (ids::CONTROLLERS);
    for (const auto& e : list)
    {
        const int type = e.status & 0xF0;
        juce::ValueTree c (type == 0xB0 ? ids::CC : (type == 0xE0 ? ids::BEND : ids::PRESSURE));
        if (type == 0xB0)
        {
            c.setProperty (ids::number, (int) e.data1, nullptr);
            c.setProperty (ids::value, (int) e.data2, nullptr);
        }
        else if (type == 0xE0)
            c.setProperty (ids::value, (e.data2 << 7) | e.data1, nullptr);
        else
            c.setProperty (ids::value, (int) e.data1, nullptr);
        c.setProperty (ids::time, e.time, nullptr);
        node.appendChild (c, nullptr);
    }
    state.appendChild (node, um); // always the last child, after the notes
}
