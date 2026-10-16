#pragma once

#include "Selection.h"

#include "../model/ProjectActions.h"

class AudioEngine;

namespace ui
{
// What the Edit and Create menus do with the current selection. Shared by the menu bar, the keyboard
// shortcuts and the context menus of the views, so that all of them behave the same.
class EditCommands
{
public:
    EditCommands (AudioEngine&, ProjectModel&, ProjectActions&, SelectionState&);

    bool canDelete() const;
    bool canDuplicate() const;
    bool canCopy() const; // also cut
    bool canPaste() const;
    bool canRename() const { return selection.get().kind != Selection::Kind::none && selection.get().kind != Selection::Kind::device; }

    void deleteSelection();
    void duplicateSelection();
    void copySelection();
    void cutSelection();
    void pasteSelection();

    // Create menu
    void newTrack (int afterTrackId = -1); // after the given track; -1: after the selected one, or at the end
    void newScene();                       // after the selected scene, or at the end
    void captureScene();
    void newClip();                        // in the selected empty slot, or the first empty slot of the selected track

    // Selects a track (and shows it in the detail panel).
    void selectTrack (int64_t trackId);
    void selectSlot (int64_t trackId, int scene);

private:
    void selectNeighbourAfterTrackRemoval (int removedIndex);

    AudioEngine& engine;
    ProjectModel& model;
    ProjectActions& actions;
    SelectionState& selection;
};
} // namespace ui
