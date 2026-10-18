#pragma once

#include "Chrome.h"
#include "Controls.h"
#include "EditCommands.h"
#include "Selection.h"

#include "../browser/BrowserActions.h"
#include "../model/ProjectActions.h"

class AudioEngine;
class SessionRecorder;

namespace ui
{
// The Session View: track headers on top, the clip grid, scene launchers and the master column on the right,
// the mixer strips underneath. Headers stay on top and scene buttons on the right while the grid scrolls.
class SessionView final : public juce::Component,
                          public HelpProvider,
                          public juce::DragAndDropTarget,
                          public juce::FileDragAndDropTarget,
                          private juce::ValueTree::Listener,
                          private juce::ChangeListener,
                          private juce::ScrollBar::Listener
{
public:
    static constexpr int kPad = 10, kTopPad = 8, kGap = 5;
    static constexpr int kHeaderHeight = 36, kSlotHeight = 40, kStopHeight = 22, kMasterWidth = 128;
    static constexpr int kMixerMinHeight = 190, kMixerMaxHeight = 320, kMixerGap = 14;

    SessionView (AudioEngine&, ProjectModel&, ProjectActions&, SelectionState&, EditCommands&);
    ~SessionView() override;

    // 30 Hz: applies model changes, moves the meters, repaints the cells whose play state changed.
    void tick();

    // Cmd+R: renames what is selected (track, clip or scene).
    void startRename();
    void startRenameNow();

    void setMixerVisible (bool);
    bool isMixerVisible() const { return mixerVisible; }
    std::function<void (bool visible)> onMixerVisibilityChanged;

    // Drops from the browser and from Finder (.mid): the view is a drop target once these are set.
    void setBrowserActions (browser::BrowserActions* a) { dropActions = a; }
    std::function<void (const browser::DropResult&, const browser::DragItem&)> onDropped;
    // "Save chain..." in the menu of a track header.
    std::function<void (int64_t trackId)> onSaveChain;
    // Where the record circles of the slots lead; the grid also draws the recordings in progress from it.
    void setRecorder (SessionRecorder* r) { recorder = r; }
    // Double-click on a clip (or on an empty slot, which makes a clip): open it in the clip editor.
    std::function<void (int64_t trackId, int scene)> onOpenClip;

    // Layout facts used by tests and the screenshot script.
    int numColumns() const { return (int) columnX.size(); }
    juce::Rectangle<int> columnBoundsInView (int index) const; // header rectangle of a track in this component's coordinates
    int scrollXPosition() const { return scrollX; }
    int scrollYPosition() const { return scrollY; }
    void setScrollPosition (int x, int y);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    juce::String helpAt (juce::Point<int>) const override;

    // juce::DragAndDropTarget / FileDragAndDropTarget
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails& d) override { itemDragMove (d); }
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override { setDropSpot ({}); }
    void itemDropped (const SourceDetails&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override { fileDragMove (files, x, y); }
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override { setDropSpot ({}); }
    void filesDropped (const juce::StringArray&, int x, int y) override;

private:
    struct DropSpot
    {
        bool valid = false;
        browser::DropTarget target = browser::DropTarget::newTrack;
        int64_t trackId = -1;
        int scene = -1;
        bool operator== (const DropSpot& o) const { return valid == o.valid && target == o.target && trackId == o.trackId && scene == o.scene; }
    };
    DropSpot spotAt (juce::Point<int>) const;
    void setDropSpot (const DropSpot&);
    juce::Rectangle<int> spotBounds (const DropSpot&) const;

    class Headers;
    class Grid;
    class Scenes;
    class MixerRow;
    class TrackStrip;
    class MasterStrip;
    class StripButton;
    friend class Headers;
    friend class Grid;
    friend class Scenes;
    friend class MixerRow;
    friend class TrackStrip;
    friend class MasterStrip;

    // juce::ValueTree::Listener
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&) override { structureChanged (parent); }
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int) override { structureChanged (parent); }
    void valueTreeChildOrderChanged (juce::ValueTree& parent, int, int) override { structureChanged (parent); }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { propertiesDirty = true; }
    void valueTreeRedirected (juce::ValueTree&) override { structureDirty = true; }
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

    void structureChanged (const juce::ValueTree& parent);
    void rebuildStrips();
    void updateLayout();
    void applyScroll();
    void repaintViews();
    void clampScroll();

    int contentWidth() const { return columnX.empty() ? 0 : columnX.back() + columnW.back(); }
    int contentHeight() const;
    juce::Rectangle<int> gridRect() const { return gridArea; }

    // Edit the name of an item in place; `host` is the component the item is painted in.
    void openRenameEditor (juce::Component& host, juce::Rectangle<int> boundsInHost, const juce::String& text,
                           std::function<void (const juce::String&)> commit);

    juce::PopupMenu colourSubMenu (int current, std::function<void (int)> pick) const;
    void showTrackMenu (juce::Point<int> screenPosition, int64_t trackId);
    void showClipMenu (juce::Point<int> screenPosition, int64_t trackId, int scene);
    void showSceneMenu (juce::Point<int> screenPosition, int scene);
    std::vector<std::pair<int64_t, int>> playingClips() const;

    AudioEngine& engine;
    ProjectModel& model;
    ProjectActions& actions;
    SelectionState& selection;
    EditCommands& commands;

    std::unique_ptr<Headers> headers;
    std::unique_ptr<Grid> grid;
    std::unique_ptr<Scenes> scenes;
    std::unique_ptr<MixerRow> mixer;
    std::unique_ptr<MasterStrip> masterStrip;
    EdgeTab mixerTab { 2 };
    juce::ScrollBar hbar { false }, vbar { true };
    std::unique_ptr<InlineEditor> renameEditor;

    // Column geometry in content coordinates (before scrolling), shared by headers, grid and mixer.
    std::vector<int> columnX, columnW;
    std::vector<int64_t> columnTrack;
    juce::Rectangle<int> gridArea, headerArea, sceneArea, mixerArea, masterHeaderArea, masterStripArea;
    double stretch = 1.0; // columns grow to fill the view when the tracks are narrower than it
    int scrollX = 0, scrollY = 0;
    bool mixerVisible = true;
    bool structureDirty = true, propertiesDirty = true;
    browser::BrowserActions* dropActions = nullptr;
    SessionRecorder* recorder = nullptr;
    DropSpot dropSpot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SessionView)
};
} // namespace ui
