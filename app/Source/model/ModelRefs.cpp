#include "ModelRefs.h"

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
    state.removeAllChildren (um);
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
