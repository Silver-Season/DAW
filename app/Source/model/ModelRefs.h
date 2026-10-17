#pragma once

#include "Ids.h"

#include "daw/engine/Clip.h"

#include <cstdint>
#include <type_traits>
#include <vector>

// Light typed wrappers over ValueTree nodes. They hold no data of their own: copying one is cheap and
// two refs to the same node compare equal. Every setter takes the UndoManager to record the change in
// (nullptr = not undoable); the caller opens the transaction.

class NodeRef
{
public:
    NodeRef() = default;
    explicit NodeRef (juce::ValueTree v) : state (std::move (v)) {}

    bool isValid() const { return state.isValid(); }
    int64_t id() const { return (int64_t) (juce::int64) state.getProperty (ids::id, -1); }
    bool operator== (const NodeRef& other) const { return state == other.state; }
    bool operator!= (const NodeRef& other) const { return state != other.state; }

    mutable juce::ValueTree state; // refs are handles: a const ref can still edit its node

protected:
    template <typename T>
    T get (const juce::Identifier& p, const T& def) const
    {
        if (! state.hasProperty (p))
            return def;
        const juce::var v = state.getProperty (p);
        if constexpr (std::is_same_v<T, float>)
            return (float) (double) v;
        else
            return (T) v;
    }
    void set (const juce::Identifier& p, const juce::var& v, juce::UndoManager* um) const { state.setProperty (p, v, um); }
};

//==============================================================================
class ClipRef final : public NodeRef
{
public:
    using NodeRef::NodeRef;

    juce::String name() const { return get<juce::String> (ids::name, {}); }
    int colour() const { return get<int> (ids::colour, 0); }
    // The end of the clip, which is also the end of its loop brace.
    double lengthBeats() const { return get<double> (ids::lengthBeats, 4.0); }
    double loopStart() const { return get<double> (ids::loopStart, 0.0); }
    double loopEnd() const { return lengthBeats(); }
    double startOffset() const { return get<double> (ids::startOffset, 0.0); }
    bool loopEnabled() const { return get<bool> (ids::loop, true); }
    // "Own scale": off = the clip shows the scale of the project and follows it; on = the clip's own scale below.
    bool ownScale() const { return get<bool> (ids::ownScale, false); }
    int scaleRoot() const { return get<int> (ids::scaleRoot, 0); }
    juce::String scaleMode() const { return get<juce::String> (ids::scaleMode, {}); }

    void setName (const juce::String& v, juce::UndoManager* um) const { set (ids::name, v, um); }
    void setColour (int v, juce::UndoManager* um) const { set (ids::colour, v, um); }
    void setLengthBeats (double v, juce::UndoManager* um) const
    {
        set (ids::lengthBeats, v, um);
        set (ids::loopEnd, v, um);
    }
    void setLoopStart (double v, juce::UndoManager* um) const { set (ids::loopStart, v, um); }
    void setLoopEnd (double v, juce::UndoManager* um) const { setLengthBeats (v, um); }
    void setStartOffset (double v, juce::UndoManager* um) const { set (ids::startOffset, v, um); }
    void setLoopEnabled (bool v, juce::UndoManager* um) const { set (ids::loop, v, um); }
    void setOwnScale (bool v, juce::UndoManager* um) const { set (ids::ownScale, v, um); }
    void setScale (int root, const juce::String& mode, juce::UndoManager* um) const
    {
        set (ids::scaleRoot, root, um);
        set (ids::scaleMode, mode, um);
    }

    // Notes are kept sorted by start, then pitch. Callers never sort. They are the NOTE children, which come first;
    // the optional CONTROLLERS node is always the last child (so `state.getChild (i)` is a note for i < numNotes()).
    int numNotes() const
    {
        const int n = state.getNumChildren();
        return n > 0 && state.getChild (n - 1).hasType (ids::CONTROLLERS) ? n - 1 : n;
    }
    daw::engine::Note note (int index) const;
    std::vector<daw::engine::Note> notes() const;
    // Returns the new NOTE node: a handle that stays valid while the note is moved, resized or re-sorted.
    juce::ValueTree addNote (const daw::engine::Note&, juce::UndoManager*) const;
    void removeNote (int index, juce::UndoManager*) const;
    void updateNote (int index, const daw::engine::Note&, juce::UndoManager*) const; // re-sorts if needed
    void setNotes (const std::vector<daw::engine::Note>&, juce::UndoManager*) const;

    // The clip as the core plays it (message thread; copies the notes).
    daw::engine::MidiClip toMidiClip() const;

    // Recorded controller movements (modulation wheel, sustain, pitch bend, pressure), sorted by time.
    std::vector<daw::engine::ControllerEvent> controllers() const;
    void setControllers (const std::vector<daw::engine::ControllerEvent>&, juce::UndoManager*) const; // empty removes the node

    // The same through NOTE nodes, which do not change identity when the note moves. A gesture that drags
    // notes updates the nodes in place, so a selection (a list of nodes) survives the sorting.
    static daw::engine::Note readNote (const juce::ValueTree& noteNode);
    bool hasNote (const juce::ValueTree& noteNode) const { return noteNode.isValid() && noteNode.getParent() == state; }
    void updateNote (const juce::ValueTree& noteNode, const daw::engine::Note&, juce::UndoManager*) const;
    void removeNote (const juce::ValueTree& noteNode, juce::UndoManager*) const;
};

//==============================================================================
// A clip on the Arrangement timeline (docs/spec/03-arrangement.md, task 3.1): ACLIP with a CLIP child that is its own
// copy of the content. It occupies the song beats [start, start + length). At `start` the content is entered at
// `offset` (beats from the start of the content); with `loop` the loop brace of the content repeats for as long as
// the clip lasts, otherwise the content plays once and the rest of the clip is silent.
class ArrangementClipRef final : public NodeRef
{
public:
    using NodeRef::NodeRef;

    double start() const { return get<double> (ids::start, 0.0); }
    double length() const { return get<double> (ids::length, 4.0); }
    double end() const { return start() + length(); }
    double offset() const { return get<double> (ids::offset, 0.0); }
    bool loop() const { return get<bool> (ids::loop, true); }
    // The content. Invalid only in a broken file.
    ClipRef clip() const { return ClipRef (state.getChildWithName (ids::CLIP)); }

    void setStart (double v, juce::UndoManager* um) const { set (ids::start, v, um); }
    void setLength (double v, juce::UndoManager* um) const { set (ids::length, v, um); }
    void setOffset (double v, juce::UndoManager* um) const { set (ids::offset, v, um); }
    void setLoop (bool v, juce::UndoManager* um) const { set (ids::loop, v, um); }
};

// A named point on the timeline (MARKER in MARKERS). colour is a palette index, -1 = the default colour.
class MarkerRef final : public NodeRef
{
public:
    using NodeRef::NodeRef;

    double beat() const { return get<double> (ids::beat, 0.0); }
    juce::String name() const { return get<juce::String> (ids::name, {}); }
    int colour() const { return get<int> (ids::colour, -1); }

    void setBeat (double v, juce::UndoManager* um) const { set (ids::beat, v, um); }
    void setName (const juce::String& v, juce::UndoManager* um) const { set (ids::name, v, um); }
    void setColour (int v, juce::UndoManager* um) const { set (ids::colour, v, um); }
};

//==============================================================================
class DeviceRef final : public NodeRef
{
public:
    using NodeRef::NodeRef;

    juce::String kind() const { return get<juce::String> (ids::kind, "prisma"); } // prisma | plugin | missing
    juce::String name() const { return get<juce::String> (ids::name, {}); }
    bool enabled() const { return get<bool> (ids::enabled, true); }
    bool folded() const { return get<bool> (ids::folded, false); }
    juce::String pluginDesc() const { return get<juce::String> (ids::pluginDesc, {}); }
    juce::String stateBase64() const { return get<juce::String> (ids::state, {}); }
    juce::String macroParams() const { return get<juce::String> (ids::macroParams, {}); }

    static juce::Array<DeviceRef> childrenOf (const juce::ValueTree& devicesNode)
    {
        juce::Array<DeviceRef> out;
        for (const auto& d : devicesNode)
            out.add (DeviceRef (d));
        return out;
    }

    void setKind (const juce::String& v, juce::UndoManager* um) const { set (ids::kind, v, um); }
    void setName (const juce::String& v, juce::UndoManager* um) const { set (ids::name, v, um); }
    void setEnabled (bool v, juce::UndoManager* um) const { set (ids::enabled, v, um); }
    void setFolded (bool v, juce::UndoManager* um) const { set (ids::folded, v, um); }
    void setPluginDesc (const juce::String& v, juce::UndoManager* um) const { set (ids::pluginDesc, v, um); }
    void setStateBase64 (const juce::String& v, juce::UndoManager* um) const { set (ids::state, v, um); }
    void setMacroParams (const juce::String& v, juce::UndoManager* um) const { set (ids::macroParams, v, um); }
};

//==============================================================================
class SceneRef final : public NodeRef
{
public:
    using NodeRef::NodeRef;

    juce::String name() const { return get<juce::String> (ids::name, {}); }
    int colour() const { return get<int> (ids::colour, -1); }
    double tempo() const { return get<double> (ids::tempo, -1.0); }

    void setName (const juce::String& v, juce::UndoManager* um) const { set (ids::name, v, um); }
    void setColour (int v, juce::UndoManager* um) const { set (ids::colour, v, um); }
    void setTempo (double v, juce::UndoManager* um) const { set (ids::tempo, v, um); }
};

//==============================================================================
class TrackRef final : public NodeRef
{
public:
    using NodeRef::NodeRef;

    juce::String type() const { return get<juce::String> (ids::type, "midi"); }
    juce::String name() const { return get<juce::String> (ids::name, {}); }
    int colour() const { return get<int> (ids::colour, 0); }
    float volumeDb() const { return get<float> (ids::volumeDb, 0.0f); }
    float pan() const { return get<float> (ids::pan, 0.0f); }
    bool mute() const { return get<bool> (ids::mute, false); }
    bool solo() const { return get<bool> (ids::solo, false); }
    bool arm() const { return get<bool> (ids::arm, false); }
    juce::String input() const { return get<juce::String> (ids::input, "all-midi"); }
    juce::String monitor() const { return get<juce::String> (ids::monitor, "auto"); }
    juce::String recordSource() const { return get<juce::String> (ids::recordSource, "midi"); }
    int width() const { return get<int> (ids::width, 120); }
    // Semitones added to every note that reaches the instrument, live ones included (-48..+48).
    int transpose() const { return get<int> (ids::transpose, 0); }

    void setName (const juce::String& v, juce::UndoManager* um) const { set (ids::name, v, um); }
    void setColour (int v, juce::UndoManager* um) const { set (ids::colour, v, um); }
    void setVolumeDb (float v, juce::UndoManager* um) const { set (ids::volumeDb, v, um); }
    void setPan (float v, juce::UndoManager* um) const { set (ids::pan, v, um); }
    void setMute (bool v, juce::UndoManager* um) const { set (ids::mute, v, um); }
    void setSolo (bool v, juce::UndoManager* um) const { set (ids::solo, v, um); }
    void setArm (bool v, juce::UndoManager* um) const { set (ids::arm, v, um); }
    void setInput (const juce::String& v, juce::UndoManager* um) const { set (ids::input, v, um); }
    void setMonitor (const juce::String& v, juce::UndoManager* um) const { set (ids::monitor, v, um); }
    void setRecordSource (const juce::String& v, juce::UndoManager* um) const { set (ids::recordSource, v, um); }
    void setWidth (int v, juce::UndoManager* um) const { set (ids::width, v, um); }
    void setTranspose (int v, juce::UndoManager* um) const { set (ids::transpose, v, um); }

    juce::ValueTree devicesNode() const { return state.getChildWithName (ids::DEVICES); }
    juce::ValueTree slotsNode() const { return state.getChildWithName (ids::SLOTS); }

    int numDevices() const { return devicesNode().getNumChildren(); }
    DeviceRef device (int index) const { return DeviceRef (devicesNode().getChild (index)); }
    juce::Array<DeviceRef> devices() const;

    // The clip in the slot of scene `scene`, or an invalid ref for an empty slot.
    int numSlots() const { return slotsNode().getNumChildren(); }
    ClipRef clipAt (int scene) const;

    // The clips of the Arrangement. Children of ARRANGEMENT in no particular order; arrangementClips() returns them
    // sorted by start. They do not overlap (see ArrangementEdit.h).
    juce::ValueTree arrangementNode() const { return state.getChildWithName (ids::ARRANGEMENT); }
    int numArrangementClips() const { return arrangementNode().getNumChildren(); }
    std::vector<ArrangementClipRef> arrangementClips() const;
};
