#pragma once

#include "ProjectModel.h"

class AudioEngine;

// The edits the window makes to a project, each as exactly one undo transaction with a name from Strings.h.
// The views call these instead of touching the model, so that "one action = one transaction" holds in one
// place and is testable without a window.
//
// `engine` is optional: when given, edits that remove or copy devices first store the live state of the
// affected processors (see AudioEngine::deleteTrack), so that undo brings the parameters back.
class ProjectActions
{
public:
    explicit ProjectActions (ProjectModel&, AudioEngine* engine = nullptr);

    // Tracks
    TrackRef addMidiTrack (int insertIndex = -1); // a MIDI track with a Prisma "Init"
    void renameTrack (int64_t trackId, const juce::String& name);
    void setTrackColour (int64_t trackId, int paletteIndex);
    TrackRef duplicateTrack (int64_t trackId);
    void deleteTrack (int64_t trackId);
    // Mixer buttons. `exclusive`: turning one on turns the others off (a plain click); otherwise they accumulate (Cmd-click).
    void setTrackMute (int64_t trackId, bool on);
    void setTrackSolo (int64_t trackId, bool on, bool exclusive);
    void setTrackArm (int64_t trackId, bool on, bool exclusive);
    // "midi" or "audio" (recording audio arrives with stage 5; the editor offers only MIDI until then).
    void setTrackRecordSource (int64_t trackId, const juce::String& source);

    // Clips (a clip is addressed by its slot)
    ClipRef createEmptyClip (int64_t trackId, int scene); // one bar long
    void renameClip (int64_t trackId, int scene, const juce::String& name);
    void setClipColour (int64_t trackId, int scene, int paletteIndex);
    // The copy goes into the next empty slot below; if there is none, a scene is added at the end. False if the slot is empty.
    bool duplicateClip (int64_t trackId, int scene);
    bool deleteClip (int64_t trackId, int scene);
    // Moves a clip to another slot (replacing what is there) or, with `copy`, copies it.
    bool moveClip (int64_t fromTrack, int fromScene, int64_t toTrack, int toScene, bool copy);
    bool copyClip (int64_t trackId, int scene); // to the clipboard, not an edit
    bool cutClip (int64_t trackId, int scene);
    bool pasteClip (int64_t trackId, int scene);
    // The same two for any clip found by id, also the content of an arrangement clip (the clip editor uses these).
    void renameClipById (int64_t clipId, const juce::String& name);
    void setClipColourById (int64_t clipId, int paletteIndex);
    bool hasClipboard() const { return clipboard.isValid(); }

    // Scenes
    SceneRef insertScene (int index);
    // A new scene after `afterScene` holding copies of the clips that play now: (track id, playing scene) pairs.
    SceneRef captureScene (int afterScene, const std::vector<std::pair<int64_t, int>>& playing);
    void renameScene (int scene, const juce::String& name);
    void setSceneColour (int scene, int paletteIndex);
    void duplicateScene (int scene);
    bool deleteScene (int scene); // refuses to delete the last scene

    // Project settings of stage 2 (the window "Project properties", the scale button of the transport)
    void setProjectScale (int root, const juce::String& mode);
    void setProjectTuning (double hz);
    void setProjectSignature (int num, int den);
    void setProjectAuthor (const juce::String&);
    void setProjectNotes (const juce::String&);
    void setQuantiseSettings (const ProjectModel::QuantiseSettings&);

    // Clip settings of the clip editor
    void setClipLoopEnabled (int64_t clipId, bool on);
    void setClipScale (int64_t clipId, bool own, int root, const juce::String& mode);
    void setTrackTranspose (int64_t trackId, int semitones);

    // Devices of the selected track's chain
    void removeDevice (int64_t deviceId);
    void moveDevice (int64_t deviceId, int newIndex);
    void setDeviceEnabled (int64_t deviceId, bool enabled);
    void setDeviceFolded (int64_t deviceId, bool folded);

private:
    void begin (const juce::String& name) { model.undo.beginNewTransaction (name); }

    ProjectModel& model;
    AudioEngine* engine;
    juce::ValueTree clipboard; // a CLIP node without a parent
};
