#pragma once

#include "ModelRefs.h"

// The whole project as a ValueTree plus its undo history. The UI reads and writes only this model;
// the engine listens to it and rebuilds its playback graph. The model knows nothing about the engine.
//
// Transactions are opened by the caller (undo.beginNewTransaction (name)); every method here writes
// with &undo, so one user action = one transaction.
class ProjectModel
{
public:
    static constexpr int kSchemaVersion = 1;
    static constexpr int kMaxUndoTransactions = 500;

    juce::ValueTree root;
    juce::UndoManager undo { 30000, kMaxUndoTransactions };

    // Two MIDI tracks with Prisma "Init" and eight scenes.
    static ProjectModel createDefault();

    // An empty project: no tracks, no scenes.
    static ProjectModel createEmpty();

    // Adopts an existing tree (e.g. a loaded file). Clears the undo history.
    explicit ProjectModel (juce::ValueTree projectTree);

    // Replaces the whole project (opening a file, returning to a version). `root` stays the same handle, so
    // everyone listening to it hears valueTreeRedirected and starts again from the new content. The undo
    // history is cleared.
    void adopt (juce::ValueTree projectTree);

    // Makes a loaded tree safe to use: adds the container nodes a hand-edited file may lack and gives every
    // track one SLOT per scene.
    static void normalise (juce::ValueTree& projectTree);

    ProjectModel (const ProjectModel&) = delete;
    ProjectModel& operator= (const ProjectModel&) = delete;

    // Ids are 64-bit, taken from the nextId property and never reused within a session, even after undo.
    int64_t newId();

    //==========================================================================
    // Project properties
    double tempo() const { return (double) root.getProperty (ids::tempo, 120.0); }
    void setTempo (double bpm) { root.setProperty (ids::tempo, bpm, &undo); }
    double launchQuantise() const { return (double) root.getProperty (ids::launchQuantise, 4.0); }
    void setLaunchQuantise (double beats) { root.setProperty (ids::launchQuantise, beats, &undo); }
    int64_t selectedTrack() const { return (int64_t) (juce::int64) root.getProperty (ids::selectedTrack, -1); }
    void setSelectedTrack (int64_t id) { root.setProperty (ids::selectedTrack, (juce::int64) id, nullptr); }
    juce::String name() const { return root.getProperty (ids::name).toString(); }
    void setName (const juce::String& n) { root.setProperty (ids::name, n, &undo); }

    // Time signature and the length of a bar in beats (a beat is a quarter note): 4/4 = 4, 6/8 = 3, 3/4 = 3.
    int sigNum() const { return juce::jlimit (1, 32, (int) root.getProperty (ids::sigNum, 4)); }
    int sigDen() const { return juce::jlimit (1, 32, (int) root.getProperty (ids::sigDen, 4)); }
    void setSignature (int num, int den)
    {
        root.setProperty (ids::sigNum, num, &undo);
        root.setProperty (ids::sigDen, den, &undo);
    }
    double beatsPerBar() const { return (double) sigNum() * 4.0 / (double) sigDen(); }

    // The scale of the project (docs/spec/02, task 2.7): root 0..11 (C = 0) and a key of daw::music::allScales(),
    // "" = off. New clips copy it; clips without their own scale follow it.
    int scaleRoot() const { return juce::jlimit (0, 11, (int) root.getProperty (ids::scaleRoot, 0)); }
    juce::String scaleMode() const { return root.getProperty (ids::scaleMode).toString(); }
    void setScale (int rootNote, const juce::String& mode)
    {
        root.setProperty (ids::scaleRoot, rootNote, &undo);
        root.setProperty (ids::scaleMode, mode, &undo);
    }

    // Frequency of A4 in Hz for the built-in synths, 415..466.
    static constexpr double kMinTuning = 415.0, kMaxTuning = 466.0, kDefaultTuning = 440.0;
    double tuningA4() const { return juce::jlimit (kMinTuning, kMaxTuning, (double) root.getProperty (ids::tuningA4, kDefaultTuning)); }
    void setTuningA4 (double hz) { root.setProperty (ids::tuningA4, juce::jlimit (kMinTuning, kMaxTuning, hz), &undo); }

    juce::String author() const { return root.getProperty (ids::author).toString(); }
    void setAuthor (const juce::String& v) { root.setProperty (ids::author, v, &undo); }
    juce::String projectNotes() const { return root.getProperty (ids::notes).toString(); }
    void setProjectNotes (const juce::String& v) { root.setProperty (ids::notes, v, &undo); }

    // The settings of "Quantise..." in the clip editor, remembered in the project. Grid in beats (0.25 = 1/16).
    struct QuantiseSettings
    {
        double grid = 0.25, strength = 1.0, swing = 0.0; // strength 0..1, swing 0..0.75
        bool ends = false;
    };
    QuantiseSettings quantiseSettings() const
    {
        QuantiseSettings q;
        q.grid = juce::jlimit (1.0 / 16.0, 4.0, (double) root.getProperty (ids::quantGrid, q.grid));
        q.strength = juce::jlimit (0.0, 1.0, (double) root.getProperty (ids::quantStrength, q.strength));
        q.swing = juce::jlimit (0.0, 0.75, (double) root.getProperty (ids::quantSwing, q.swing));
        q.ends = (bool) root.getProperty (ids::quantEnds, q.ends);
        return q;
    }
    void setQuantiseSettings (const QuantiseSettings& q)
    {
        root.setProperty (ids::quantGrid, q.grid, &undo);
        root.setProperty (ids::quantStrength, q.strength, &undo);
        root.setProperty (ids::quantSwing, q.swing, &undo);
        root.setProperty (ids::quantEnds, q.ends, &undo);
    }

    //==========================================================================
    // Tracks
    TrackRef addTrack (const juce::String& type, int insertIndex = -1);
    void deleteTrack (int64_t id);
    TrackRef duplicateTrack (int64_t id);

    TrackRef findTrack (int64_t id) const;
    int indexOfTrack (int64_t id) const;
    int numTracks() const { return tracksNode().getNumChildren(); }
    TrackRef trackAt (int index) const { return TrackRef (tracksNode().getChild (index)); }
    juce::Array<TrackRef> tracks() const;
    juce::ValueTree tracksNode() const { return root.getChildWithName (ids::TRACKS); }

    //==========================================================================
    // Scenes. A track always has exactly as many SLOT nodes as the project has SCENE nodes.
    SceneRef insertScene (int index);
    void deleteScene (int index);
    void duplicateScene (int index);

    int numScenes() const { return scenesNode().getNumChildren(); }
    SceneRef sceneAt (int index) const { return SceneRef (scenesNode().getChild (index)); }
    juce::ValueTree scenesNode() const { return root.getChildWithName (ids::SCENES); }

    //==========================================================================
    // Clips
    ClipRef createClip (const TrackRef&, int scene, double lengthBeats = 4.0, const juce::String& name = {});
    // Puts a copy of `source` (with fresh ids) into the slot, replacing whatever is there.
    ClipRef setClip (const TrackRef&, int scene, const ClipRef& source);
    void deleteClip (const TrackRef&, int scene);
    // Moves (or copies) a clip between slots; the destination is replaced.
    void moveClip (int64_t fromTrack, int fromScene, int64_t toTrack, int toScene, bool copy);
    ClipRef findClip (int64_t id) const;

    //==========================================================================
    // Devices. The first device of a track is its instrument, the rest are effects.
    DeviceRef addDevice (juce::ValueTree devicesNode, const juce::String& kind, const juce::String& name, int index = -1);
    DeviceRef addDevice (const TrackRef&, const juce::String& kind, const juce::String& name, int index = -1);
    void removeDevice (const DeviceRef&);
    void moveDevice (const DeviceRef&, int newIndex);
    DeviceRef findDevice (int64_t id) const;

    juce::ValueTree masterNode() const { return root.getChildWithName (ids::MASTER); }
    float masterVolumeDb() const { return (float) (double) masterNode().getProperty (ids::volumeDb, 0.0); }
    void setMasterVolumeDb (float db) { masterNode().setProperty (ids::volumeDb, (double) db, &undo); }
    juce::ValueTree masterDevicesNode() const { return masterNode().getChildWithName (ids::DEVICES); }

private:
    struct DefaultTag
    {
    };
    explicit ProjectModel (DefaultTag);

    static juce::ValueTree createEmptyTree();
    void reassignIds (juce::ValueTree&);
    juce::ValueTree makeSlot();

    int64_t highWater = 0; // largest id handed out in this session, survives undo of nextId
};
