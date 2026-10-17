#include "ProjectActions.h"

#include "../Strings.h"
#include "../engine/AudioEngine.h"

ProjectActions::ProjectActions (ProjectModel& m, AudioEngine* e) : model (m), engine (e) {}

//==============================================================================
void ProjectActions::setProjectScale (int root, const juce::String& mode)
{
    if (root == model.scaleRoot() && mode == model.scaleMode())
        return;
    begin (Strings::undoProjectScale);
    model.setScale (juce::jlimit (0, 11, root), mode);
}

void ProjectActions::setProjectTuning (double hz)
{
    if (std::abs (hz - model.tuningA4()) < 1.0e-9)
        return;
    begin (Strings::undoTuning);
    model.setTuningA4 (hz);
}

void ProjectActions::setProjectSignature (int num, int den)
{
    if (num == model.sigNum() && den == model.sigDen())
        return;
    begin (Strings::undoSignature);
    model.setSignature (juce::jlimit (1, 32, num), den);
}

void ProjectActions::setProjectAuthor (const juce::String& text)
{
    if (text == model.author())
        return;
    begin (Strings::undoAuthor);
    model.setAuthor (text);
}

void ProjectActions::setProjectNotes (const juce::String& text)
{
    if (text == model.projectNotes())
        return;
    begin (Strings::undoProjectNotes);
    model.setProjectNotes (text);
}

void ProjectActions::setQuantiseSettings (const ProjectModel::QuantiseSettings& q)
{
    begin (Strings::undoQuantiseSettings);
    model.setQuantiseSettings (q);
}

void ProjectActions::setClipLoopEnabled (int64_t clipId, bool on)
{
    const auto clip = model.findClip (clipId);
    if (! clip.isValid() || clip.loopEnabled() == on)
        return;
    begin (Strings::undoLoopSwitch);
    clip.setLoopEnabled (on, &model.undo);
}

void ProjectActions::setClipScale (int64_t clipId, bool own, int root, const juce::String& mode)
{
    const auto clip = model.findClip (clipId);
    if (! clip.isValid())
        return;
    begin (Strings::undoClipScale);
    clip.setOwnScale (own, &model.undo);
    clip.setScale (juce::jlimit (0, 11, root), mode, &model.undo);
}

void ProjectActions::setTrackTranspose (int64_t trackId, int semitones)
{
    const auto track = model.findTrack (trackId);
    semitones = juce::jlimit (-48, 48, semitones);
    if (! track.isValid() || track.transpose() == semitones)
        return;
    begin (Strings::undoTrackTranspose);
    track.setTranspose (semitones, &model.undo);
}

//==============================================================================
TrackRef ProjectActions::addMidiTrack (int insertIndex)
{
    begin (Strings::undoNewTrack);
    auto track = model.addTrack ("midi", insertIndex);
    model.addDevice (track, "prisma", "Prisma");
    return track;
}

TrackRef ProjectActions::addAudioTrack (int insertIndex)
{
    begin (Strings::sessionAudio::undoNewAudioTrack);
    return model.addTrack ("audio", insertIndex);
}

void ProjectActions::renameTrack (int64_t trackId, const juce::String& name)
{
    const auto track = model.findTrack (trackId);
    if (! track.isValid() || name.trim().isEmpty() || name == track.name())
        return;
    begin (Strings::undoRenameTrack);
    track.setName (name.trim(), &model.undo);
}

void ProjectActions::setTrackColour (int64_t trackId, int paletteIndex)
{
    const auto track = model.findTrack (trackId);
    if (! track.isValid() || track.colour() == paletteIndex)
        return;
    begin (Strings::undoTrackColour);
    track.setColour (juce::jlimit (0, 15, paletteIndex), &model.undo);
}

TrackRef ProjectActions::duplicateTrack (int64_t trackId)
{
    const auto track = model.findTrack (trackId);
    if (! track.isValid())
        return {};
    if (engine != nullptr)
        engine->captureDeviceStates (track.state); // the copy starts from what the user hears, not from the last saved state
    begin (Strings::undoDuplicateTrack);
    return model.duplicateTrack (trackId);
}

void ProjectActions::deleteTrack (int64_t trackId)
{
    if (! model.findTrack (trackId).isValid())
        return;
    begin (Strings::undoDeleteTrack);
    if (engine != nullptr)
        engine->deleteTrack (trackId);
    else
        model.deleteTrack (trackId);
}

void ProjectActions::setTrackMute (int64_t trackId, bool on)
{
    const auto track = model.findTrack (trackId);
    if (! track.isValid() || track.mute() == on)
        return;
    begin (Strings::undoTrackMute);
    track.setMute (on, &model.undo);
}

void ProjectActions::setTrackSolo (int64_t trackId, bool on, bool exclusive)
{
    if (! model.findTrack (trackId).isValid())
        return;
    begin (Strings::undoTrackSolo);
    for (const auto& t : model.tracks())
    {
        if (t.id() == trackId)
            t.setSolo (on, &model.undo);
        else if (exclusive && on && t.solo())
            t.setSolo (false, &model.undo);
    }
}

void ProjectActions::setTrackRecordSource (int64_t trackId, const juce::String& source)
{
    const auto track = model.findTrack (trackId);
    if (! track.isValid() || track.recordSource() == source)
        return;
    begin (Strings::recording::undoRecordSource);
    track.setRecordSource (source, &model.undo);
}

void ProjectActions::setTrackArm (int64_t trackId, bool on, bool exclusive)
{
    if (! model.findTrack (trackId).isValid())
        return;
    begin (Strings::undoTrackArm);
    for (const auto& t : model.tracks())
    {
        if (t.id() == trackId)
            t.setArm (on, &model.undo);
        else if (exclusive && on && t.arm())
            t.setArm (false, &model.undo);
    }
}

//==============================================================================
ClipRef ProjectActions::createEmptyClip (int64_t trackId, int scene)
{
    const auto track = model.findTrack (trackId);
    if (! track.isValid() || track.isAudio() || scene < 0 || scene >= model.numScenes() || track.clipAt (scene).isValid())
        return {}; // an audio track holds sounds, not empty MIDI clips
    begin (Strings::undoNewClip);
    return model.createClip (track, scene, model.beatsPerBar());
}

void ProjectActions::renameClip (int64_t trackId, int scene, const juce::String& name)
{
    const auto track = model.findTrack (trackId);
    const auto clip = track.isValid() ? track.clipAt (scene) : ClipRef();
    if (! clip.isValid() || name.trim().isEmpty() || name == clip.name())
        return;
    begin (Strings::undoRenameClip);
    clip.setName (name.trim(), &model.undo);
}

void ProjectActions::setClipColour (int64_t trackId, int scene, int paletteIndex)
{
    const auto track = model.findTrack (trackId);
    const auto clip = track.isValid() ? track.clipAt (scene) : ClipRef();
    if (! clip.isValid() || clip.colour() == paletteIndex)
        return;
    begin (Strings::undoClipColour);
    clip.setColour (juce::jlimit (0, 15, paletteIndex), &model.undo);
}

void ProjectActions::renameClipById (int64_t clipId, const juce::String& name)
{
    const auto clip = model.findClip (clipId);
    if (! clip.isValid() || name.trim().isEmpty() || name == clip.name())
        return;
    begin (Strings::undoRenameClip);
    clip.setName (name.trim(), &model.undo);
}

void ProjectActions::setClipColourById (int64_t clipId, int paletteIndex)
{
    const auto clip = model.findClip (clipId);
    if (! clip.isValid() || clip.colour() == paletteIndex)
        return;
    begin (Strings::undoClipColour);
    clip.setColour (juce::jlimit (0, 15, paletteIndex), &model.undo);
}

bool ProjectActions::duplicateClip (int64_t trackId, int scene)
{
    const auto track = model.findTrack (trackId);
    const auto clip = track.isValid() ? track.clipAt (scene) : ClipRef();
    if (! clip.isValid())
        return false;

    begin (Strings::undoDuplicateClip);
    int target = -1;
    for (int s = scene + 1; s < model.numScenes(); ++s)
        if (! track.clipAt (s).isValid())
        {
            target = s;
            break;
        }
    if (target < 0)
    {
        model.insertScene (-1);
        target = model.numScenes() - 1;
    }
    model.setClip (track, target, clip);
    return true;
}

bool ProjectActions::deleteClip (int64_t trackId, int scene)
{
    const auto track = model.findTrack (trackId);
    if (! track.isValid() || ! track.clipAt (scene).isValid())
        return false;
    begin (Strings::undoDeleteClip);
    model.deleteClip (track, scene);
    return true;
}

namespace
{
// A sound lives on an audio track and a MIDI clip on a MIDI track; moving one across would leave a clip that cannot play.
bool clipFitsTrack (const ClipRef& clip, const TrackRef& track) { return clip.isAudio() == track.isAudio(); }
} // namespace

bool ProjectActions::moveClip (int64_t fromTrack, int fromScene, int64_t toTrack, int toScene, bool copy)
{
    const auto src = model.findTrack (fromTrack), dst = model.findTrack (toTrack);
    if (! src.isValid() || ! dst.isValid() || ! src.clipAt (fromScene).isValid() || toScene < 0 || toScene >= model.numScenes()
        || (fromTrack == toTrack && fromScene == toScene) || ! clipFitsTrack (src.clipAt (fromScene), dst))
        return false;
    begin (copy ? Strings::undoCopyClip : Strings::undoMoveClip);
    model.moveClip (fromTrack, fromScene, toTrack, toScene, copy);
    return true;
}

bool ProjectActions::copyClip (int64_t trackId, int scene)
{
    const auto track = model.findTrack (trackId);
    const auto clip = track.isValid() ? track.clipAt (scene) : ClipRef();
    if (! clip.isValid())
        return false;
    clipboard = clip.state.createCopy();
    return true;
}

bool ProjectActions::cutClip (int64_t trackId, int scene)
{
    if (! copyClip (trackId, scene))
        return false;
    begin (Strings::undoCutClip);
    model.deleteClip (model.findTrack (trackId), scene);
    return true;
}

bool ProjectActions::pasteClip (int64_t trackId, int scene)
{
    const auto track = model.findTrack (trackId);
    if (! clipboard.isValid() || ! track.isValid() || scene < 0 || scene >= model.numScenes() || ! clipFitsTrack (ClipRef (clipboard), track))
        return false;
    begin (Strings::undoPasteClip);
    model.setClip (track, scene, ClipRef (clipboard));
    return true;
}

//==============================================================================
SceneRef ProjectActions::insertScene (int index)
{
    begin (Strings::undoNewScene);
    return model.insertScene (index);
}

SceneRef ProjectActions::captureScene (int afterScene, const std::vector<std::pair<int64_t, int>>& playing)
{
    begin (Strings::undoCaptureScene);
    const int at = (afterScene < 0 || afterScene >= model.numScenes()) ? model.numScenes() : afterScene + 1;
    auto scene = model.insertScene (at);
    for (const auto& [trackId, slot] : playing)
    {
        const auto track = model.findTrack (trackId);
        if (! track.isValid() || slot < 0)
            continue;
        const int source = slot >= at ? slot + 1 : slot; // slots below the new scene moved down by one
        const auto clip = track.clipAt (source);
        if (clip.isValid())
            model.setClip (track, at, clip);
    }
    return scene;
}

void ProjectActions::renameScene (int scene, const juce::String& name)
{
    if (scene < 0 || scene >= model.numScenes() || name.trim().isEmpty() || name == model.sceneAt (scene).name())
        return;
    begin (Strings::undoRenameScene);
    model.sceneAt (scene).setName (name.trim(), &model.undo);
}

void ProjectActions::setSceneColour (int scene, int paletteIndex)
{
    if (scene < 0 || scene >= model.numScenes() || model.sceneAt (scene).colour() == paletteIndex)
        return;
    begin (Strings::undoSceneColour);
    model.sceneAt (scene).setColour (paletteIndex, &model.undo);
}

void ProjectActions::duplicateScene (int scene)
{
    if (scene < 0 || scene >= model.numScenes())
        return;
    begin (Strings::undoDuplicateScene);
    model.duplicateScene (scene);
}

bool ProjectActions::deleteScene (int scene)
{
    if (scene < 0 || scene >= model.numScenes() || model.numScenes() <= 1)
        return false;
    begin (Strings::undoDeleteScene);
    model.deleteScene (scene);
    return true;
}

//==============================================================================
void ProjectActions::removeDevice (int64_t deviceId)
{
    const auto device = model.findDevice (deviceId);
    if (! device.isValid())
        return;
    begin (Strings::undoRemoveDevice);
    if (engine != nullptr)
        engine->removeDevice (device);
    else
        model.removeDevice (device);
}

void ProjectActions::moveDevice (int64_t deviceId, int newIndex)
{
    const auto device = model.findDevice (deviceId);
    if (! device.isValid())
        return;
    begin (Strings::undoMoveDevice);
    model.moveDevice (device, newIndex);
}

void ProjectActions::setDeviceEnabled (int64_t deviceId, bool enabled)
{
    const auto device = model.findDevice (deviceId);
    if (! device.isValid() || device.enabled() == enabled)
        return;
    begin (Strings::undoToggleDevice);
    device.setEnabled (enabled, &model.undo);
}

void ProjectActions::setDeviceFolded (int64_t deviceId, bool folded)
{
    const auto device = model.findDevice (deviceId);
    if (! device.isValid() || device.folded() == folded)
        return;
    begin (Strings::undoFoldDevice);
    device.setFolded (folded, &model.undo);
}
