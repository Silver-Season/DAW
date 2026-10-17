#pragma once

#include "Controls.h"
#include "Selection.h"

#include "../model/ArrangementEdit.h"
#include "../model/ProjectActions.h"

#include <optional>

class AudioEngine;
class SessionRecorder;

namespace ui
{
class ArrangementLanes;
class ArrangementRuler;
class ArrangementHeaders;
class ArrangementCorner;
class ArrangementMinimap;
class ArrangementPlayhead;

// The Arrangement View (docs/spec/03-arrangement.md, task 3.3): track headers on the left, the marker lane, the loop
// brace and the bar ruler on top, the lanes with the clips, a minimap at the bottom and the playhead as a light
// component over the lanes. The view owns everything the parts share: the zoom and scroll, the grid and snapping,
// the selection (clips, a time range, a marker) and every command. The parts only draw and turn the mouse into calls.
//
// The view reads and writes the ProjectModel and talks to the engine through its transport methods. A mouse gesture
// that edits clips shows a preview while it runs and writes the model once when it ends: one undo transaction.
class ArrangementView final : public juce::Component,
                              public HelpProvider,
                              public juce::DragAndDropTarget,
                              private juce::ValueTree::Listener,
                              private juce::ScrollBar::Listener
{
public:
    static constexpr int kHeaderWidth = 208, kMarkerLaneHeight = 20, kBraceHeight = 8, kBarRulerHeight = 24;
    static constexpr int kRulerHeight = kMarkerLaneHeight + kBraceHeight + kBarRulerHeight;
    static constexpr int kMinimapHeight = 36, kScrollBar = 10;
    static constexpr int kMinTrackHeight = 24, kMaxTrackHeight = 240, kDefaultTrackHeight = 84;
    static constexpr double kMinPxPerBeat = 0.2, kMaxPxPerBeat = 600.0;
    static constexpr double kMinGridPixels = 12.0; // grid lines are never closer than this when the grid is adaptive

    ArrangementView (AudioEngine&, ProjectModel&, ProjectActions&, SelectionState&);
    ~ArrangementView() override;

    // 30 Hz while shown: applies model changes, follows the playhead, refreshes the headers.
    void tick();

    //==========================================================================
    // View state (the view keeps it in the window, not in the project)
    struct View
    {
        double pxPerBeat = 16.0;
        double scrollBeats = 0.0;
        int scrollY = 0;
        bool adaptiveGrid = true;
        double fixedStep = 0.25; // beats, when the grid is fixed (Cmd+1 / Cmd+2)
        bool triplet = false;
        bool snap = true;
        bool uniformHeights = true; // H: every lane has uniformHeight; otherwise each has its own (set, or fitted to its notes)
        int uniformHeight = kDefaultTrackHeight;
    };
    View& view() { return v; }
    const View& view() const { return v; }

    struct TimeSelection
    {
        double start = 0.0, end = 0.0;
        std::vector<int64_t> tracks; // track ids
        bool isEmpty() const { return tracks.empty() || end - start < 1.0e-6; }
    };

    struct Row
    {
        int64_t trackId = -1;
        int y = 0, height = 0; // in content coordinates, before the vertical scroll
    };

    //==========================================================================
    // Geometry. x is in the coordinates of the lanes (and of the ruler), which start at kHeaderWidth.
    double beatToX (double beat) const { return (beat - v.scrollBeats) * v.pxPerBeat; }
    double xToBeat (double x) const { return x / v.pxPerBeat + v.scrollBeats; }
    int lanesWidth() const;
    int lanesHeight() const;
    double visibleBeats() const { return (double) lanesWidth() / v.pxPerBeat; }
    // The whole scrollable time: the song and eight empty bars after it.
    double totalBeats() const;
    double beatsPerBar() const { return model.beatsPerBar(); }

    const std::vector<Row>& rows() const { return layoutRows; }
    int rowIndexOfTrack (int64_t trackId) const;
    // The row under a y of the lanes (scroll applied), or -1 below the last one.
    int rowAtY (int y) const;
    int contentHeight() const;

    // The grid step in beats, and the grid line nearest to a beat (bypass = Cmd held: no snapping)
    double gridStep() const;
    double snapBeat (double beat, bool bypass = false) const;
    double snapFloor (double beat) const;
    juce::String gridText() const;
    // Bar numbers and bar lines are drawn every n-th bar (a power of two) so that they do not crowd when zoomed out.
    int barLabelStride() const;
    int barLineStride() const;
    void takeFocus() { grabKeyboardFocus(); }

    // Zoom and scroll. All of them keep the view inside the song.
    void setZoom (double pxPerBeat, double anchorX); // keeps the beat under anchorX where it is
    void zoomBy (double factor, double anchorX);
    void setScrollBeats (double beats);
    void setScrollY (int y);
    void scrollIntoView (double beat);
    void zoomToSelection();  // Z
    void zoomBack();         // X: the zoom before the last Z or zoom of the minimap
    void zoomToSong();
    bool hasPreviousZoom() const { return previousZoom.has_value(); }
    void remember();         // stores the current zoom for X
    void stepGrid (int direction); // Cmd+1 finer, Cmd+2 coarser: the grid becomes fixed
    void toggleTriplet();
    void toggleSnap();
    void toggleHeightMode(); // H
    void setTrackHeight (int64_t trackId, int height, bool commit);
    int trackHeight (const TrackRef&) const;

    //==========================================================================
    // Selection
    const std::vector<int64_t>& selectedClips() const { return selectedAClips; }
    const TimeSelection& timeSelection() const { return timeSel; }
    int64_t selectedMarker() const { return markerSel; }
    bool isClipSelected (int64_t aclipId) const;
    void selectClip (int64_t aclipId, bool add);
    void selectClips (const std::vector<int64_t>& ids);
    void setTimeSelection (double start, double end, const std::vector<int64_t>& trackIds);
    void selectMarker (int64_t markerId);
    void clearSelection();
    void selectAllClips();
    std::vector<ArrangementClipRef> selectedClipRefs() const;
    // The tracks the commands work on: the time selection's, the selected clips', or the selected track.
    std::vector<TrackRef> targetTracks() const;

    //==========================================================================
    // Time. The cursor is the play start while the transport stands and the playhead while it runs.
    double cursorBeat() const;
    // Sets the play start and moves the song position: quantised while playing, at once with `immediate` (Shift).
    void setCursor (double beat, bool immediate = false);
    void jumpToBeat (double beat, bool immediate);
    // Only the play start (a click on the lanes, a paste position): the transport is not disturbed while it plays.
    void placeCursor (double beat);
    void playHome();
    void playEnd();
    void jumpToMarker (bool next);
    bool followsPlayhead() const { return model.followPlayhead(); }
    void toggleFollow();

    //==========================================================================
    // Commands (menus, keys and context menus call these; each is one undo transaction)
    bool deleteSelection();
    bool duplicateSelection();
    bool copySelection (bool cut);
    bool pasteAtCursor();
    bool hasClipboard() const { return clipboard.isValid(); }
    bool splitAtCursor();
    bool joinSelection();
    bool loopSelection();
    bool insertTimeAtSelection();
    bool removeTimeAtSelection();
    bool addMarkerAtCursor();
    bool renameSelection();
    bool nudgeSelection (int direction, bool extend);
    void sendSelectionToSession();
    // Shows another take of a clip that was recorded in a loop (one undo transaction).
    bool switchTake (int64_t aclipId, int number);

    // The edits of the gestures
    struct Dragged
    {
        int64_t clipId = -1;
        int64_t trackId = -1;
        double start = 0.0;
    };
    void commitMove (const std::vector<Dragged>& clips, double deltaBeats, int deltaTracks, bool copy);
    void commitResizeRight (int64_t aclipId, double newEnd);
    void commitResizeLeft (int64_t aclipId, double newStart);
    int64_t drawClipAt (int64_t trackId, double beat); // double click on an empty lane: one bar
    void setLoopBrace (double start, double end, bool on, bool newTransaction);
    void moveMarkerTo (int64_t markerId, double beat);

    // Keyboard. Returns true if the key was used. Keys are compared by key code, not by the typed character.
    bool handleKey (const juce::KeyPress&);
    bool keyPressed (const juce::KeyPress& key) override { return handleKey (key); }
    // Bare letters (H, Z, X) are left alone while the computer keyboard plays notes.
    std::function<bool()> lettersReserved;

    // Double-click on a clip: the window opens the clip editor.
    std::function<void (int64_t trackId, int64_t aclipId)> onOpenClip;
    std::function<void (const juce::String&)> onMessage;

    //==========================================================================
    // Parts, for the window, the tests and the screenshot script
    ArrangementLanes& lanes() { return *lanesPart; }
    ArrangementRuler& ruler() { return *rulerPart; }
    ArrangementHeaders& headers() { return *headersPart; }
    ArrangementMinimap& minimap() { return *minimapPart; }
    ArrangementPlayhead& playhead() { return *playheadPart; }
    juce::Rectangle<int> lanesBounds() const;

    // Recording onto the timeline (task 3.4): the playhead layer draws the clips that grow while it runs.
    void setRecorder (SessionRecorder* r) { recorder = r; }
    SessionRecorder* getRecorder() const { return recorder; }

    AudioEngine& getEngine() { return engine; }
    ProjectModel& getModel() { return model; }
    SelectionState& getSelection() { return selectionState; }

    // A session clip is being dragged over the lanes (Session View -> Arrangement)
    struct DropHint
    {
        bool valid = false;
        int row = -1;
        double beat = 0.0, length = 0.0;
    };
    const DropHint& dropHint() const { return drop; }

    void paint (juce::Graphics&) override;
    void resized() override;
    juce::String helpAt (juce::Point<int>) const override;

    // juce::DragAndDropTarget: "sessionclip:<track>:<scene>" descriptions
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails& d) override { itemDragMove (d); }
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override { setDropHint ({}); }
    void itemDropped (const SourceDetails&) override;
    static juce::String sessionClipDescription (int64_t trackId, int scene);
    static bool parseSessionClip (const juce::var& description, int64_t& trackId, int& scene);

private:
    friend class ArrangementLanes;
    friend class ArrangementRuler;
    friend class ArrangementHeaders;
    friend class ArrangementCorner;
    friend class ArrangementMinimap;
    friend class ArrangementPlayhead;

    // juce::ValueTree::Listener
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { structureDirty = true; }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { structureDirty = true; }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { structureDirty = true; }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { propertiesDirty = true; }
    void valueTreeRedirected (juce::ValueTree&) override { structureDirty = true; }
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

    void rebuildRows();
    void applyModelChanges();
    void clampView();
    void updateScrollBar();
    void repaintAll();
    void repaintTimeParts();
    void fitToSong();
    void followPlayhead();
    void selectionChanged();
    void setDropHint (const DropHint&);
    int fittedHeight (const TrackRef&) const;
    void message (const juce::String& text) const
    {
        if (onMessage)
            onMessage (text);
    }
    bool isLetterKey (const juce::KeyPress&, char letter) const;
    juce::PopupMenu markersMenu();

    AudioEngine& engine;
    ProjectModel& model;
    SessionRecorder* recorder = nullptr;
    ProjectActions& actions;
    SelectionState& selectionState;

    View v;
    std::vector<Row> layoutRows;
    int rowsTotalHeight = 0;
    std::vector<int64_t> selectedAClips;
    TimeSelection timeSel;
    int64_t markerSel = -1;
    struct Zoom
    {
        double px, scroll;
    };
    std::optional<Zoom> previousZoom;
    bool structureDirty = true, propertiesDirty = true;
    bool autoZoom = true; // until the user zooms, the view keeps fitting the song to the window
    std::vector<uint8_t> lastInSession;

    // The clipboard: copies of arrangement clips with their place relative to the first one.
    struct ClipboardEntry
    {
        juce::ValueTree snapshot;
        int trackOffset = 0; // rows below the first clip's track
        double beatOffset = 0.0;
    };
    std::vector<ClipboardEntry> clipboardEntries;
    juce::ValueTree clipboard; // marker that something was copied
    DropHint drop;

    std::unique_ptr<ArrangementLanes> lanesPart;
    std::unique_ptr<ArrangementRuler> rulerPart;
    std::unique_ptr<ArrangementHeaders> headersPart;
    std::unique_ptr<ArrangementCorner> cornerPart;
    std::unique_ptr<ArrangementMinimap> minimapPart;
    std::unique_ptr<ArrangementPlayhead> playheadPart;
    juce::ScrollBar vbar { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangementView)
};
} // namespace ui
