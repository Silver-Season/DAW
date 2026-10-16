#pragma once

#include "Chrome.h"
#include "Controls.h"
#include "EditCommands.h"
#include "Selection.h"

#include "../model/ProjectActions.h"

class AudioEngine;

namespace ui
{
// The Session View: track headers on top, the clip grid, scene launchers and the master column on the right,
// the mixer strips underneath. Headers stay on top and scene buttons on the right while the grid scrolls.
class SessionView final : public juce::Component,
                          public HelpProvider,
                          private juce::ValueTree::Listener,
                          private juce::ChangeListener,
                          private juce::ScrollBar::Listener
{
public:
    static constexpr int kPad = 10, kTopPad = 8, kGap = 5;
    static constexpr int kHeaderHeight = 30, kSlotHeight = 34, kStopHeight = 22, kMasterWidth = 128;
    static constexpr int kMixerMinHeight = 190, kMixerMaxHeight = 320, kMixerGap = 14;

    SessionView (AudioEngine&, ProjectModel&, ProjectActions&, SelectionState&, EditCommands&);
    ~SessionView() override;

    // 30 Hz: applies model changes, moves the meters, repaints the cells whose play state changed.
    void tick();

    // Cmd+R: renames what is selected (track, clip or scene).
    void startRename();

    void setMixerVisible (bool);
    bool isMixerVisible() const { return mixerVisible; }
    std::function<void (bool visible)> onMixerVisibilityChanged;

    // Layout facts used by tests and the screenshot script.
    int numColumns() const { return (int) columnX.size(); }
    juce::Rectangle<int> columnBoundsInView (int index) const; // header rectangle of a track in this component's coordinates
    int scrollXPosition() const { return scrollX; }
    int scrollYPosition() const { return scrollY; }
    void setScrollPosition (int x, int y);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    juce::String helpAt (juce::Point<int>) const override;

private:
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SessionView)
};
} // namespace ui
