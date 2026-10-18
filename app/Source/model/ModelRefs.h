#pragma once

#include "Ids.h"

#include "daw/engine/Clip.h"
#include "daw/engine/WarpMap.h"

#include <cmath>
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
    // The content of an arrangement clip has no start or loop switch of its own: the ACLIP around it holds them (where
    // it starts in the content, whether it repeats), so the clip editor and the timeline always show the same thing.
    double startOffset() const
    {
        const auto a = arrangementParent();
        return a.isValid() ? (double) a.getProperty (ids::offset, 0.0) : get<double> (ids::startOffset, 0.0);
    }
    bool loopEnabled() const
    {
        const auto a = arrangementParent();
        return a.isValid() ? (bool) a.getProperty (ids::loop, true) : get<bool> (ids::loop, true);
    }
    juce::ValueTree arrangementParent() const
    {
        const auto p = state.getParent();
        return p.hasType (ids::ACLIP) ? p : juce::ValueTree();
    }
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
    void setStartOffset (double v, juce::UndoManager* um) const
    {
        if (auto a = arrangementParent(); a.isValid())
            a.setProperty (ids::offset, juce::jmax (0.0, v), um);
        else
            set (ids::startOffset, v, um);
    }
    void setLoopEnabled (bool v, juce::UndoManager* um) const
    {
        if (auto a = arrangementParent(); a.isValid())
            a.setProperty (ids::loop, v, um);
        else
            set (ids::loop, v, um);
    }
    void setOwnScale (bool v, juce::UndoManager* um) const { set (ids::ownScale, v, um); }
    void setScale (int root, const juce::String& mode, juce::UndoManager* um) const
    {
        set (ids::scaleRoot, root, um);
        set (ids::scaleMode, mode, um);
    }

    // Audio clips (docs/spec/05-audio-recording.md, task 5.1). The spec's AUDIOCLIP is a CLIP with a `file` property (the id
    // of a FILE in PROJECT/AUDIOFILES), so moving, copying, splitting and takes work the same for both kinds. The beats
    // above place the audio: content beat 0 is the file frame `sampleStart`, and content beats turn into frames at the
    // project tempo (no warp in this stage). An audio clip has no notes.
    bool isAudio() const { return state.hasProperty (ids::file); }
    int64_t fileId() const { return (int64_t) (juce::int64) state.getProperty (ids::file, -1); }
    double sampleStart() const { return get<double> (ids::sampleStart, 0.0); }
    double gainDb() const { return get<double> (ids::gainDb, 0.0); }
    int transpose() const { return juce::jlimit (-48, 48, get<int> (ids::transpose, 0)); } // semitones, by resampling
    int detune() const { return juce::jlimit (-50, 50, get<int> (ids::detune, 0)); }       // cents
    bool reverse() const { return get<bool> (ids::reverse, false); }
    // Warp (docs/spec/07-warp.md): the clip plays in the project tempo. `originalTempo` is the tempo of its audio (found by
    // the analysis, or set by hand, x2, /2); 0 = unknown, and then the clip cannot follow. The mode says how: "repitch"
    // resamples (the pitch moves with the speed), the others stretch without changing the pitch.
    bool followTempo() const { return get<bool> (ids::warp, false) && originalTempo() > 0.0; }
    bool warpOn() const { return get<bool> (ids::warp, false); }
    void setFollowTempo (bool v, juce::UndoManager* um) const { set (ids::warp, v, um); }
    double originalTempo() const { return get<double> (ids::originalTempo, 0.0); }
    void setOriginalTempo (double v, juce::UndoManager* um) const { set (ids::originalTempo, juce::jlimit (20.0, 999.0, v), um); }
    juce::String warpMode() const { return get<juce::String> (ids::warpMode, "complex"); }
    void setWarpMode (const juce::String& v, juce::UndoManager* um) const { set (ids::warpMode, v, um); }
    // The tempo its content beats are counted in: its own when it follows the project, else the project's.
    double contentTempo (double projectTempo) const { return followTempo() ? originalTempo() : projectTempo; }
    double fadeIn() const { return juce::jmax (0.0, get<double> (ids::fadeIn, 0.0)); }   // beats, arrangement only
    double fadeOut() const { return juce::jmax (0.0, get<double> (ids::fadeOut, 0.0)); } // beats, arrangement only
    double pitchRatio() const { return std::pow (2.0, transpose() / 12.0 + detune() / 1200.0); }

    void setFileId (int64_t v, juce::UndoManager* um) const { set (ids::file, (juce::int64) v, um); }
    void setSampleStart (double v, juce::UndoManager* um) const { set (ids::sampleStart, v, um); }
    void setGainDb (double v, juce::UndoManager* um) const { set (ids::gainDb, juce::jlimit (-70.0, 24.0, v), um); }
    void setTranspose (int v, juce::UndoManager* um) const { set (ids::transpose, juce::jlimit (-48, 48, v), um); }
    void setDetune (int v, juce::UndoManager* um) const { set (ids::detune, juce::jlimit (-50, 50, v), um); }
    void setReverse (bool v, juce::UndoManager* um) const { set (ids::reverse, v, um); }
    void setFadeIn (double v, juce::UndoManager* um) const { set (ids::fadeIn, juce::jmax (0.0, v), um); }
    void setFadeOut (double v, juce::UndoManager* um) const { set (ids::fadeOut, juce::jmax (0.0, v), um); }

    // Warp markers (docs/spec/07-warp.md, task 7.3): WARP/MARKER children, `sample` the file frame and `beat` the
    // content beat it plays at. Without markers the clip maps beats to frames by a straight line from sampleStart at its
    // original tempo. warpMap() is that mapping for a file at `fileRate`, the one the engine and the views use.
    std::vector<daw::engine::WarpMarker> warpMarkers() const;
    void setWarpMarkers (const std::vector<daw::engine::WarpMarker>&, juce::UndoManager*) const; // empty removes WARP
    daw::engine::WarpMap warpMap (double fileRate, double projectTempo) const;
    // Beats mode: how long a slice played slower keeps sounding into the gap, 0..1 (1 holds it).
    double beatsDecay() const { return juce::jlimit (0.0, 1.0, get<double> (ids::beatsDecay, 1.0)); }
    void setBeatsDecay (double v, juce::UndoManager* um) const { set (ids::beatsDecay, juce::jlimit (0.0, 1.0, v), um); }

    // Notes are kept sorted by start, then pitch. Callers never sort. They are the NOTE children, which come first;
    // the optional WARP and CONTROLLERS nodes follow them (so `state.getChild (i)` is a note for i < numNotes()).
    int numNotes() const
    {
        int n = state.getNumChildren();
        while (n > 0 && ! state.getChild (n - 1).hasType (ids::NOTE))
            --n;
        return n;
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
    // Takes of a recording that ran in a loop (task 3.4): the content above is take `activeTake()`; the other passes sit
    // in TAKES as TAKE nodes (number = which pass, a CLIP child = its content). 0 = a clip without takes.
    int activeTake() const { return get<int> (ids::take, 0); }
    juce::ValueTree takesNode() const { return state.getChildWithName (ids::TAKES); }

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

// A sound file of the project's pool (PROJECT/AUDIOFILES/FILE, task 5.1). `path` is relative to the project folder when
// the file lies inside it (recordings, imported copies), `absPath` is where it was last found.
class AudioFileRef final : public NodeRef
{
public:
    using NodeRef::NodeRef;

    juce::String path() const { return get<juce::String> (ids::path, {}); }
    juce::String absPath() const { return get<juce::String> (ids::absPath, {}); }
    double sampleRate() const { return get<double> (ids::sampleRate, 44100.0); }
    int channels() const { return get<int> (ids::channels, 2); }
    juce::int64 lengthSamples() const { return (juce::int64) state.getProperty (ids::lengthSamples, 0); }
    juce::String hash() const { return get<juce::String> (ids::hash, {}); }
    double tempo() const { return get<double> (ids::tempo, 0.0); } // known tempo of a loop, 0 = unknown
    double firstBeatSeconds() const { return get<double> (ids::firstBeat, 0.0); } // where the first downbeat is
    bool analysed() const { return get<bool> (ids::analysed, false); }
    // How sure the analysis was of the tempo, 0..1 (1: the tempo came from the name or the length in bars).
    double tempoConfidence() const { return get<double> (ids::confidence, 1.0); }
    juce::String name() const { return get<juce::String> (ids::name, {}); }

    void setPath (const juce::String& v, juce::UndoManager* um) const { set (ids::path, v, um); }
    void setAbsPath (const juce::String& v, juce::UndoManager* um) const { set (ids::absPath, v, um); }
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
    // The track whose sound keys a compressor or gate (task 6.5), -1 = its own input.
    int64_t sidechainTrack() const { return (int64_t) (juce::int64) state.getProperty (ids::sidechain, -1); }
    void setSidechainTrack (int64_t id, juce::UndoManager* um) const { set (ids::sidechain, (juce::int64) id, um); }

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
    bool isAudio() const { return type() == "audio"; }
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
    // A return track (docs/spec/06-effects-mixing.md, task 6.3): no clips, its chain gets what the sends give it.
    bool isReturn() const { return type() == "return"; }
    // Sends to the first two return tracks (A and B), post-fader, in dB; kMinSendDb or less = off.
    static constexpr float kMinSendDb = -60.0f;
    float sendDb (int which) const { return get<float> (which == 0 ? ids::sendA : ids::sendB, kMinSendDb); }
    int width() const { return get<int> (ids::width, 120); }
    // Semitones added to every note that reaches the instrument, live ones included (-48..+48).
    int transpose() const { return get<int> (ids::transpose, 0); }
    // Height of the lane in the Arrangement View in pixels; 0 = not set (the view picks one).
    int arrangementHeight() const { return get<int> (ids::arrHeight, 0); }

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
    void setSendDb (int which, float db, juce::UndoManager* um) const { set (which == 0 ? ids::sendA : ids::sendB, juce::jlimit (kMinSendDb, 6.0f, db), um); }
    void setWidth (int v, juce::UndoManager* um) const { set (ids::width, v, um); }
    void setTranspose (int v, juce::UndoManager* um) const { set (ids::transpose, v, um); }
    void setArrangementHeight (int v, juce::UndoManager* um) const { set (ids::arrHeight, v, um); }

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
