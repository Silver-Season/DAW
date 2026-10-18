// Stage 3, task 3.3 of docs/spec/03-arrangement.md: the Arrangement View without a window on screen. The view is
// created with fixed bounds and driven through its API, its keys and synthetic mouse events; every gesture must be
// exactly one undo transaction.
#include "TestSupport.h"

#include "../app/Source/Strings.h"
#include "../app/Source/model/ArrangementEdit.h"
#include "../app/Source/model/Automation.h"
#include "../app/Source/model/ProjectActions.h"
#include "../app/Source/ui/ArrangementLanes.h"
#include "../app/Source/ui/ArrangementView.h"
#include "../app/Source/ui/Theme.h"

using namespace tests;

namespace
{
bool near (double a, double b)
{
    return std::abs (a - b) < 1.0e-6;
}

struct ViewRig
{
    ViewRig()
    {
        ThemeManager::get().reload();
        a = fx.model.addTrack ("midi");
        b = fx.model.addTrack ("midi");
        fx.model.undo.clearUndoHistory();
        view = std::make_unique<ui::ArrangementView> (fx.engine, fx.model, actions, selection);
        view->setBounds (0, 0, 1400, 700);
        view->setZoom (20.0, 0.0);
        view->setScrollBeats (0.0);
        view->tick();
    }

    // Number of undo steps available: undoing until empty
    int undoDepth()
    {
        int n = 0;
        while (fx.model.undo.canUndo() && n < 1000)
        {
            fx.model.undo.undo();
            ++n;
        }
        for (int i = 0; i < n; ++i)
            fx.model.undo.redo();
        return n;
    }

    bool key (int code, juce::ModifierKeys mods = {}) { return view->handleKey (juce::KeyPress (code, mods, 0)); }

    Fixture fx;
    ProjectActions actions { fx.model, &fx.engine };
    ui::SelectionState selection;
    TrackRef a, b;
    std::unique_ptr<ui::ArrangementView> view;
};

const juce::ModifierKeys kCmd (juce::ModifierKeys::commandModifier);
const juce::ModifierKeys kCmdShift (juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);

void testGeometry()
{
    std::printf ("[3.3] geometry, grid and snapping\n");
    ViewRig r;
    auto& v = *r.view;
    check (near (v.xToBeat (v.beatToX (7.25)), 7.25), "beat to x and back");
    v.setScrollBeats (10.0);
    check (near (v.beatToX (10.0), 0.0), "scroll moves the origin");
    v.setZoom (40.0, 200.0);
    const double under = v.xToBeat (200.0);
    v.setZoom (80.0, 200.0);
    check (near (v.xToBeat (200.0), under), "zoom keeps the beat under the pointer");

    v.setScrollBeats (0.0);
    v.setZoom (10.0, 0.0);
    const double coarse = v.gridStep();
    v.setZoom (200.0, 0.0);
    check (v.gridStep() < coarse, "adaptive grid gets finer when zoomed in");
    check (v.gridStep() * 200.0 >= ui::ArrangementView::kMinGridPixels - 1.0e-9, "grid lines never closer than the minimum");

    v.stepGrid (-1);
    const double fixedStep = v.gridStep();
    v.setZoom (10.0, 0.0);
    check (near (v.gridStep(), fixedStep), "a fixed grid does not follow the zoom");

    const double step = v.gridStep();
    check (near (std::fmod (v.snapBeat (3.37), step), 0.0) || near (std::fmod (v.snapBeat (3.37), step), step), "snap lands on the grid");
    check (near (v.snapBeat (3.37, true), 3.37), "bypass (Cmd) does not snap");
    v.toggleSnap();
    check (near (v.snapBeat (3.37), 3.37), "snap off");
}

void testClipCommands()
{
    std::printf ("[3.3] keyboard commands on clips: split, duplicate, delete, join, loop\n");
    ViewRig r;
    auto& v = *r.view;
    auto& m = r.fx.model;
    auto clip = arrangement::addEmptyClip (m, r.a, 0.0, 8.0);
    v.tick();
    m.undo.clearUndoHistory();

    v.selectClip (clip.id(), false);
    v.placeCursor (4.0);
    check (r.key ('e', kCmd), "Cmd+E is used");
    check (r.a.arrangementClips().size() == 2, "Cmd+E splits at the cursor");
    check (r.undoDepth() == 1, "split is one undo step");
    check (r.a.arrangementClips().size() == 2 && near (r.a.arrangementClips()[0].end(), 4.0), "split pieces meet at the cursor");

    v.selectAllClips();
    check (v.selectedClips().size() == 2, "Cmd+A selects every clip");
    r.key ('j', kCmd);
    check (r.a.arrangementClips().size() == 1, "Cmd+J joins them again");

    v.selectAllClips();
    const int before = (int) r.a.arrangementClips().size();
    r.key ('d', kCmd);
    check ((int) r.a.arrangementClips().size() == before + 1, "Cmd+D adds a copy");

    v.selectAllClips();
    check (r.key (juce::KeyPress::deleteKey), "Delete is used");
    check (r.a.arrangementClips().empty(), "Delete removes the selected clips");
    m.undo.undo();
    check (! r.a.arrangementClips().empty(), "and undo brings them back");

    v.selectAllClips();
    r.key ('l', kCmd);
    check (m.arrangementLoopOn() && near (m.arrangementLoopStart(), r.a.arrangementClips()[0].start())
               && near (m.arrangementLoopEnd(), r.a.arrangementClips().back().end()),
           "Cmd+L puts the loop brace around the selection");
}

void testGestures()
{
    std::printf ("[3.3] gestures: one gesture = one transaction\n");
    ViewRig r;
    auto& v = *r.view;
    auto& m = r.fx.model;
    auto clip = arrangement::addEmptyClip (m, r.a, 4.0, 4.0);
    m.undo.clearUndoHistory();

    ui::ArrangementView::Dragged d { clip.id(), r.a.id(), 4.0 };
    m.undo.beginNewTransaction ("x");
    v.commitMove ({ d }, 4.0, 0, false);
    check (near (r.a.arrangementClips()[0].start(), 8.0), "move by 4 beats");
    m.undo.undo();
    check (near (r.a.arrangementClips()[0].start(), 4.0), "undo restores the position");

    v.commitMove ({ d }, 0.0, 1, true);
    check (r.b.arrangementClips().size() == 1 && r.a.arrangementClips().size() == 1, "copy to the next track keeps the original");

    v.commitResizeRight (clip.id(), 12.0);
    check (near (r.a.arrangementClips()[0].end(), 12.0), "resize right");
    v.commitResizeLeft (clip.id(), 6.0);
    check (near (r.a.arrangementClips()[0].start(), 6.0), "resize left");

    const auto drawn = v.drawClipAt (r.b.id(), 33.4);
    check (drawn >= 0, "double click on an empty lane draws a clip");
    bool found = false;
    for (const auto& c : r.b.arrangementClips())
        if (c.id() == drawn)
            found = near (c.start(), 32.0) && near (c.length(), 4.0);
    check (found, "the drawn clip is one bar, floored to the bar");

    // a real mouse drag over the lanes is a single undo step
    ui::ArrangementLanes* lanes = nullptr;
    for (auto* c : v.getChildren())
        if (auto* l = dynamic_cast<ui::ArrangementLanes*> (c))
            lanes = l;
    check (lanes != nullptr, "the view owns lanes");
    if (lanes == nullptr)
        return;
    v.clearSelection();
    m.undo.clearUndoHistory();
    const auto bounds = lanes->clipBounds (clip.id());
    check (! bounds.isEmpty(), "a shown clip has bounds");
    const auto start = bounds.getCentre();
    auto src = juce::Desktop::getInstance().getMainMouseSource();
    auto ev = [&] (juce::Point<int> p, juce::Time t) {
        return juce::MouseEvent (src, p.toFloat(), juce::ModifierKeys::leftButtonModifier, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, lanes, lanes,
                                 t, start.toFloat(), t, 1, false);
    };
    const auto now = juce::Time::getCurrentTime();
    lanes->mouseDown (ev (start, now));
    for (int i = 1; i <= 10; ++i)
        lanes->mouseDrag (ev (start + juce::Point<int> (i * 10, 0), now));
    lanes->mouseUp (ev (start + juce::Point<int> (100, 0), now));
    check (r.undoDepth() == 1, "a mouse drag of a clip is one undo step");
    check (r.a.arrangementClips()[0].start() > 6.0, "the clip moved");
}

void testAutomationEditing()
{
    std::printf ("[6.7] automation envelope in the Arrangement View\n");
    ViewRig r;
    auto& v = *r.view;
    auto& m = r.fx.model;
    check (! v.automationMode(), "automation mode is off at the start");
    check (r.key ('a'), "the key A is used");
    check (v.automationMode(), "A turns automation mode on");
    check (v.automationTarget (r.a.id()) == automation::volumeTarget, "the default target is the volume");

    auto* lanes = &v.lanes();
    auto src = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    juce::Point<int> pressedAt;
    auto ev = [&] (juce::Point<int> p, int clicks = 1) {
        return juce::MouseEvent (src, p.toFloat(), juce::ModifierKeys::leftButtonModifier, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, lanes, lanes, now,
                                 pressedAt.toFloat(), now, clicks, false);
    };
    m.undo.clearUndoHistory();

    // a click on the dimmed line of the current volume adds a point (and a start point, so that the line does not jump)
    const auto area = lanes->envelopeArea (0);
    const float currentY = area.getBottom() - automation::normalisedVolume (r.a.volumeDb()) * area.getHeight();
    const juce::Point<int> click ((int) v.beatToX (8.0), juce::roundToInt (currentY));
    pressedAt = click;
    lanes->mouseDown (ev (click));
    lanes->mouseUp (ev (click));
    auto lane = automation::findLane (r.a, automation::volumeTarget);
    check (lane.isValid() && lane.getNumChildren() == 2, "a click on the line adds a point and a start point");
    check (r.undoDepth() == 1, "adding a point is one undo step");
    if (! lane.isValid() || lane.getNumChildren() != 2)
        return;
    check (near ((double) lane.getChild (0).getProperty (ids::beat), 0.0), "the start point is at beat 0");
    check (near ((double) lane.getChild (1).getProperty (ids::beat), 8.0), "the new point is snapped to the grid");

    // a drag of the point changes its value, in one undo step
    const float before = (float) (double) lane.getChild (1).getProperty (ids::value);
    m.undo.clearUndoHistory();
    const juce::Point<int> dot ((int) v.beatToX (8.0), juce::roundToInt (area.getBottom() - before * area.getHeight()));
    pressedAt = dot;
    lanes->mouseDown (ev (dot));
    for (int i = 1; i <= 10; ++i)
        lanes->mouseDrag (ev (dot + juce::Point<int> (0, i * 3)));
    lanes->mouseUp (ev (dot + juce::Point<int> (0, 30)));
    check ((float) (double) lane.getChild (1).getProperty (ids::value) < before - 0.1f, "dragging down lowers the value");
    check (near ((double) lane.getChild (1).getProperty (ids::beat), 8.0), "a vertical drag keeps the beat");
    check (r.undoDepth() == 1, "a point drag is one undo step");
    m.undo.undo();
    check (near ((double) lane.getChild (1).getProperty (ids::value), before), "undo restores the value");

    // a click away from the line does not touch the automation
    lanes->mouseDown (ev ({ (int) v.beatToX (20.0), (int) area.getBottom() - 1 }));
    lanes->mouseUp (ev ({ (int) v.beatToX (20.0), (int) area.getBottom() - 1 }));
    check (lane.getNumChildren() == 2, "a click far from the line adds nothing");

    // a double click on a point deletes it
    m.undo.clearUndoHistory();
    const juce::Point<int> dot2 ((int) v.beatToX (8.0), juce::roundToInt (area.getBottom() - before * area.getHeight()));
    lanes->mouseDoubleClick (ev (dot2, 2));
    check (lane.getNumChildren() == 1, "a double click deletes the point");
    check (r.undoDepth() == 1, "deleting a point is one undo step");

    // Delete with a time selection removes the points in it
    automation::addPoint (m, lane, { 4.0, 0.4f, 0.0f });
    automation::addPoint (m, lane, { 6.0, 0.6f, 0.0f });
    v.setTimeSelection (3.0, 7.0, { r.a.id() });
    check (r.key (juce::KeyPress::deleteKey), "Delete is used with a time selection");
    check (lane.getNumChildren() == 1, "the points in the selection are gone");

    r.key ('a');
    check (! v.automationMode(), "A again leaves the mode");
}

void testMarkersAndLoop()
{
    std::printf ("[3.3] markers, loop brace, cursor\n");
    ViewRig r;
    auto& v = *r.view;
    auto& m = r.fx.model;

    v.placeCursor (8.0);
    check (r.key ('m', kCmd), "Cmd+M is used");
    check (m.markers().size() == 1 && near (m.markers()[0].beat(), 8.0), "marker at the cursor");
    v.placeCursor (16.0);
    r.key ('m', kCmd);
    v.placeCursor (24.0);
    r.key ('m', kCmd);
    check (m.markers().size() == 3, "three markers");

    v.placeCursor (10.0);
    v.jumpToMarker (true);
    check (near (v.cursorBeat(), 16.0), "next marker");
    v.jumpToMarker (false);
    check (near (v.cursorBeat(), 8.0), "previous marker");

    m.undo.clearUndoHistory();
    v.moveMarkerTo (m.markers()[0].id(), 12.0);
    check (near (m.markers()[0].beat(), 12.0), "marker moves");
    check (r.undoDepth() == 1, "moving a marker is one undo step");

    v.setLoopBrace (4.0, 12.0, true, true);
    check (m.arrangementLoopOn() && near (m.arrangementLoopStart(), 4.0) && near (m.arrangementLoopEnd(), 12.0), "loop brace set");

    v.setTimeSelection (4.0, 8.0, { r.a.id() });
    auto clip = arrangement::addEmptyClip (m, r.a, 8.0, 4.0);
    (void) clip;
    m.undo.clearUndoHistory();
    check (r.key ('i', kCmd), "Cmd+I inserts time");
    check (r.undoDepth() == 1, "insert time is one undo step");
    check (near (r.a.arrangementClips().back().start(), 12.0), "later clips move by the inserted length");
    check (r.key (juce::KeyPress::deleteKey, kCmdShift), "Cmd+Shift+Delete removes time");
    check (near (r.a.arrangementClips().back().start(), 8.0), "and brings them back");
}

void testFollowPlayhead()
{
    std::printf ("[3.3] follow playhead flips the page\n");
    ViewRig r;
    auto& v = *r.view;
    auto& m = r.fx.model;
    arrangement::addEmptyClip (m, r.a, 0.0, 4.0);
    m.setFollowPlayhead (true);
    v.setZoom (20.0, 0.0);
    v.setScrollBeats (0.0);
    const double visible = v.visibleBeats();

    r.fx.engine.play();
    Runner runner (r.fx.engine);
    runner.run (2);
    r.fx.engine.seek (visible + 4.0);
    runner.run (4);
    v.tick();
    check (v.view().scrollBeats > 0.0, "the view follows a playhead that left the page");
    r.fx.engine.stop();

    m.setFollowPlayhead (false);
    v.setScrollBeats (0.0);
    r.fx.engine.play();
    runner.run (2);
    r.fx.engine.seek (visible + 4.0);
    runner.run (4);
    v.tick();
    check (near (v.view().scrollBeats, 0.0), "follow off leaves the view alone");
    r.fx.engine.stop();
}

void testUndoOfHeights()
{
    std::printf ("[3.3] track height is stored and undoable\n");
    ViewRig r;
    auto& v = *r.view;
    v.toggleHeightMode(); // by content: heights live in the model
    v.toggleHeightMode();
    v.toggleHeightMode();
    r.fx.model.undo.clearUndoHistory();
    v.setTrackHeight (r.a.id(), 140, true);
    check (v.trackHeight (r.a) == 140, "height set");
    check (r.undoDepth() == 1, "height change is one undo step");
    v.toggleHeightMode();
    v.toggleHeightMode();
    check (v.trackHeight (r.a) >= ui::ArrangementView::kMinTrackHeight, "height stays in range after H");
}
} // namespace

namespace tests
{
void runArrangementUiTests()
{
    testGeometry();
    testClipCommands();
    testGestures();
    testMarkersAndLoop();
    testFollowPlayhead();
    testUndoOfHeights();
    testAutomationEditing();
}
} // namespace tests
