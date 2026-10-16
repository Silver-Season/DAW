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
