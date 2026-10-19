#pragma once

#include <array>
#include <functional>

#include "Controls.h"
#include "Selection.h"

#include "../model/ClipEdit.h"
#include "../model/ProjectActions.h"

class AudioEngine;

namespace ui
{
class QuantiseWindow;
class ComingSoonView;

// The clip editor (docs/spec/02-midi-recording.md, task 2.4): properties of the clip on the left, a ruler with
// the loop brace, piano keys, the note grid, the velocity lane and the controller strip. It edits one clip of
// the ProjectModel; every mouse gesture is one undo transaction and the engine hears the edits through the model.
class PianoRoll final : public juce::Component,
                        public HelpProvider,
                        private juce::ValueTree::Listener,
                        private juce::ScrollBar::Listener,
                        private juce::Timer
{
public:
    static constexpr int kPropsWidth = 180, kKeysWidth = 56, kRulerHeight = 30, kVelocityHeight = 64;
    static constexpr int kControllerHeader = 22, kControllerBody = 52, kScrollBar = 10;
    static constexpr int kMinRowHeight = 6, kMaxRowHeight = 24;
    static constexpr double kMinNoteLength = 1.0 / 64.0;

    PianoRoll (AudioEngine&, ProjectModel&, ProjectActions&, SelectionState&);
    ~PianoRoll() override;

    // The clip to edit, by id; a clip that does not exist (or 0) shows the "no clip" page. A clip that is
    // opened for the first time is zoomed to its length and scrolled to its notes.
    void setClip (int64_t clipId);
    int64_t clipId() const { return currentClip; }
    bool hasClip() const;

    // 30 Hz while visible: the playing cursor, the pending auditions, the pending model changes.
    void tick();

    // Commands that the window's menu and shortcuts reach (Cmd+U, Cmd+Shift+U).
    bool quantizeSelected();
    void showQuantizeWindow();

    // Notes in the editor
    int numSelected() const { return (int) selected.size(); }
    const clipedit::Nodes& selection() const { return selected; }
    void selectAllNotes();
    // View state, for tests and screenshots.
    struct View
    {
        double pxPerBeat = 60.0;
        double scrollBeats = 0.0;
        int rowHeight = 14;
        int scrollY = 0; // pixels from the top row
        bool adaptiveGrid = true;
        int denominator = 16; // fixed grid 1/denominator of a whole note
        bool triplet = false;
        bool snap = true;
        bool pencil = false, preview = true, fold = false, scaleOnly = false;
        bool controllersOpen = false;
        int controller = 0; // 0 modwheel, 1 pitch bend, 2 sustain
        double editCursor = 0.0; // where Paste and Split work when the mouse is elsewhere
    };
    View& view() { return v; }
    double gridStep() const;
    juce::Rectangle<int> noteBounds (const juce::ValueTree& noteNode) const; // in the coordinates of this component
    juce::Rectangle<int> gridBounds() const;
    bool loopFlashing() const { return loopFlashUntil != 0 && (juce::int32) (juce::Time::getMillisecondCounter() - loopFlashUntil) < 0; }
    // Called (inside the editing transaction) when a note made the clip longer, with the new length in beats.
    std::function<void (double)> onClipExtended;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    juce::String helpAt (juce::Point<int>) const override { return {}; }

private:
    class Props;
    class Ruler;
    class Keys;
    class NoteGrid;
    class VelocityLane;
    class ControllerStrip;
    friend class Props;
    friend class Ruler;
    friend class Keys;
    friend class NoteGrid;
    friend class VelocityLane;
    friend class ControllerStrip;

    // juce::ValueTree::Listener: any change below the project marks the editor dirty
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { markDirty (true); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { markDirty (true); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { markDirty (false); }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { markDirty (false); }
    void valueTreeRedirected (juce::ValueTree&) override { markDirty (true); }
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void timerCallback() override;
    void markDirty (bool structure)
    {
        dirty = true;
        rowsDirty = rowsDirty || structure;
    }

    // clip and scale
    ClipRef clip() const;
    clipedit::Scale scale() const; // the scale the clip shows (its own or the project's)
    int64_t trackOfClip() const;
    int sceneOfClip() const;
    juce::Colour clipColour() const;
    double beatsPerBar() const { return model.beatsPerBar(); }

    // geometry of the view
    double beatToX (double beat) const { return (beat - v.scrollBeats) * v.pxPerBeat; }
    double xToBeat (double x) const { return x / v.pxPerBeat + v.scrollBeats; }
    int numRows() const { return (int) rowPitches.size(); }
    int rowOfPitch (int pitch) const; // -1 if the pitch is folded away
    int pitchOfRow (int row) const;
    int rowToY (int row) const { return row * v.rowHeight - v.scrollY; }
    int yToRow (int y) const { return (y + v.scrollY) / v.rowHeight; }
    double snapBeat (double beat, bool bypass) const; // nearest grid line
    double floorBeat (double beat) const;
    void rebuildRows();
    void clampView();
    void zoomHorizontally (double factor, double anchorX);
    void setRowHeight (int h, int anchorY);
    void zoomToClip();
    void showNoteRange (bool force);
    void updateScrollBars();
    void stepGrid (int direction);   // Cmd+1 / Cmd+2
    juce::String gridText() const;

    // selection
    bool isSelected (const juce::ValueTree& n) const;
    void setSelection (clipedit::Nodes nodes);
    void pruneSelection();

    // editing (each opens its own transaction; gestures open theirs in the grid)
    void deleteSelected();
    void duplicateSelected();
    void copySelected (bool cut);
    void pasteAt (double beat);
    void invertSelection();
    void selectPitch (int pitch);
    void splitAt (double beat);
    void joinSelected();
    void transposeSelected (int semitones, bool scaleStep);
    void nudgeSelected (double dBeats, int dSemitones);
    void resizeSelected (double dBeats);
    void loopAroundSelection();
    void scaleClipTime (double factor); // ×2 / ÷2: the whole clip, and its place on the timeline for an arrangement clip
    void extendClipFor (double endBeat); // inside the caller's transaction
    void flashLoop();
    void setLoopEnabled (bool);

    // listening to the notes
    void audition (int pitch, int velocity);
    void noteOnKey (int pitch);
    void noteOffKey (int pitch);

    void showContextMenu (juce::Point<int> screenPosition, double beatUnderMouse, int pitchUnderMouse);
    void layoutParts();

    AudioEngine& engine;
    ProjectModel& model;
    ProjectActions& actions;
    SelectionState& selectionState;

    int64_t currentClip = 0;
    bool viewInitialised = false;
    bool dirty = true, rowsDirty = true;
    View v;
    clipedit::Nodes selected;
    std::vector<int> rowPitches; // top to bottom
    std::array<int, 128> pitchToRow {};
    int lastVelocity = 100;
    juce::uint32 loopFlashUntil = 0;
    int shownNotes = -1;
    std::vector<juce::ValueTree> playingNotes; // the notes under the playing cursor, drawn lit
    std::array<bool, 128> playingPitches {};
    void updatePlayingNotes (double cursorBeat);
    bool isPlayingNote (const juce::ValueTree& n) const { return std::find (playingNotes.begin(), playingNotes.end(), n) != playingNotes.end(); }

    struct Pending
    {
        int pitch;
        juce::uint32 offAt; // 0: held until noteOffKey
    };
    std::vector<Pending> sounding;

    static std::vector<daw::engine::Note>& clipboard();

    std::unique_ptr<Props> props;
    std::unique_ptr<Ruler> ruler;
    std::unique_ptr<Keys> keys;
    std::unique_ptr<NoteGrid> grid;
    std::unique_ptr<VelocityLane> velocityLane;
    std::unique_ptr<ControllerStrip> controllers;
    juce::ScrollBar hbar { false }, vbar { true };
    std::unique_ptr<QuantiseWindow> quantiseWindow;
    // Glass: the Piano Roll / Score / Step switch above the editor; Score and Step are drawn, not built yet.
    class ModeBar;
    std::unique_ptr<ModeBar> modeBar;
    std::unique_ptr<ComingSoonView> scoreSoon, stepSoon;
    int editorMode = 0; // 0 piano roll, 1 score, 2 step
    void setEditorMode (int);
    void lookAndFeelChanged() override { layoutParts(); } // a theme switch shows or hides the glass mode bar

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRoll)
};
} // namespace ui
