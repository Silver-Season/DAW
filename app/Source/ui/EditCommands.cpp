#include "EditCommands.h"

#include "../engine/AudioEngine.h"

namespace ui
{
EditCommands::EditCommands (AudioEngine& e, ProjectModel& m, ProjectActions& a, SelectionState& s)
    : engine (e), model (m), actions (a), selection (s)
{
}

void EditCommands::selectTrack (int64_t trackId)
{
    model.setSelectedTrack (trackId);
    selection.selectTrack (trackId);
}

void EditCommands::selectSlot (int64_t trackId, int scene)
{
    model.setSelectedTrack (trackId);
    selection.selectSlot (trackId, scene);
}

bool EditCommands::canDelete() const
{
    const auto& s = selection.get();
    switch (s.kind)
    {
        case Selection::Kind::track: return model.findTrack (s.trackId).isValid();
        case Selection::Kind::clip: return model.findTrack (s.trackId).clipAt (s.scene).isValid();
        case Selection::Kind::scene: return model.numScenes() > 1 && s.scene >= 0 && s.scene < model.numScenes();
        case Selection::Kind::device: return model.findDevice (s.deviceId).isValid();
        case Selection::Kind::aclip:
        case Selection::Kind::none: break;
    }
    return false;
}

bool EditCommands::canDuplicate() const
{
    const auto& s = selection.get();
    switch (s.kind)
    {
        case Selection::Kind::track: return model.findTrack (s.trackId).isValid();
        case Selection::Kind::clip: return model.findTrack (s.trackId).clipAt (s.scene).isValid();
        case Selection::Kind::scene: return s.scene >= 0 && s.scene < model.numScenes();
        case Selection::Kind::device:
        case Selection::Kind::aclip:
        case Selection::Kind::none: break;
    }
    return false;
}

bool EditCommands::canCopy() const
{
    const auto& s = selection.get();
    return s.kind == Selection::Kind::clip && model.findTrack (s.trackId).clipAt (s.scene).isValid();
}

bool EditCommands::canPaste() const
{
    const auto& s = selection.get();
    return s.kind == Selection::Kind::clip && actions.hasClipboard();
}

void EditCommands::selectNeighbourAfterTrackRemoval (int removedIndex)
{
    const int n = model.numTracks();
    if (n == 0)
    {
        model.setSelectedTrack (-1);
        selection.clear();
        return;
    }
    selectTrack (model.trackAt (juce::jmin (removedIndex, n - 1)).id());
}

void EditCommands::deleteSelection()
{
    const auto s = selection.get();
    switch (s.kind)
    {
        case Selection::Kind::track:
        {
            const int index = model.indexOfTrack (s.trackId);
            actions.deleteTrack (s.trackId);
            selectNeighbourAfterTrackRemoval (index);
            break;
        }
        case Selection::Kind::clip: actions.deleteClip (s.trackId, s.scene); break;
        case Selection::Kind::scene:
            if (actions.deleteScene (s.scene))
                selection.selectScene (juce::jmin (s.scene, model.numScenes() - 1));
            break;
        case Selection::Kind::device:
            actions.removeDevice (s.deviceId);
            selection.selectTrack (s.trackId);
            break;
        case Selection::Kind::aclip:
        case Selection::Kind::none: break;
    }
}

void EditCommands::duplicateSelection()
{
    const auto s = selection.get();
    switch (s.kind)
    {
        case Selection::Kind::track:
            if (auto copy = actions.duplicateTrack (s.trackId); copy.isValid())
                selectTrack (copy.id());
            break;
        case Selection::Kind::clip: actions.duplicateClip (s.trackId, s.scene); break;
        case Selection::Kind::scene:
            actions.duplicateScene (s.scene);
            selection.selectScene (s.scene + 1);
            break;
        case Selection::Kind::device:
        case Selection::Kind::aclip:
        case Selection::Kind::none: break;
    }
}

void EditCommands::copySelection()
{
    const auto s = selection.get();
    if (s.kind == Selection::Kind::clip)
        actions.copyClip (s.trackId, s.scene);
}

void EditCommands::cutSelection()
{
    const auto s = selection.get();
    if (s.kind == Selection::Kind::clip)
        actions.cutClip (s.trackId, s.scene);
}

void EditCommands::pasteSelection()
{
    const auto s = selection.get();
    if (s.kind == Selection::Kind::clip)
        actions.pasteClip (s.trackId, s.scene);
}

void EditCommands::newTrack (int afterTrackId)
{
    int index = -1;
    const int64_t reference = afterTrackId >= 0 ? afterTrackId : model.selectedTrack();
    if (const int i = model.indexOfTrack (reference); i >= 0)
        index = i + 1;
    auto track = actions.addMidiTrack (index);
    selectTrack (track.id());
}

void EditCommands::newAudioTrack (int afterTrackId)
{
    int index = -1;
    const int64_t reference = afterTrackId >= 0 ? afterTrackId : model.selectedTrack();
    if (const int i = model.indexOfTrack (reference); i >= 0)
        index = i + 1;
    auto track = actions.addAudioTrack (index);
    selectTrack (track.id());
}

void EditCommands::newScene()
{
    const auto& s = selection.get();
    const int at = s.kind == Selection::Kind::scene || s.kind == Selection::Kind::clip ? s.scene + 1 : -1;
    const auto scene = actions.insertScene (at);
    juce::ignoreUnused (scene);
    const int index = at < 0 ? model.numScenes() - 1 : at;
    selection.selectScene (index);
}

void EditCommands::captureScene()
{
    std::vector<std::pair<int64_t, int>> playing;
    for (const auto& t : model.tracks())
        if (auto* rt = engine.findRuntime (t.id()))
            playing.emplace_back (t.id(), rt->playingSlot.load());
    const auto& s = selection.get();
    const int after = s.kind == Selection::Kind::scene || s.kind == Selection::Kind::clip ? s.scene : model.numScenes() - 1;
    actions.captureScene (after, playing);
    selection.selectScene (after + 1);
}

void EditCommands::newClip()
{
    const auto& s = selection.get();
    if (s.kind == Selection::Kind::clip)
    {
        if (actions.createEmptyClip (s.trackId, s.scene).isValid())
            return;
    }
    const auto track = model.findTrack (s.kind == Selection::Kind::none ? model.selectedTrack() : (s.trackId >= 0 ? s.trackId : model.selectedTrack()));
    if (! track.isValid())
        return;
    for (int scene = 0; scene < model.numScenes(); ++scene)
        if (! track.clipAt (scene).isValid())
        {
            if (actions.createEmptyClip (track.id(), scene).isValid())
                selectSlot (track.id(), scene);
            return;
        }
}
} // namespace ui
