#include "PianoRoll.h"

#include "PianoRollParts.h"

#include "../engine/AudioEngine.h"

namespace ui
{
namespace
{
constexpr int kEdgeGrab = 6; // px at the right end of a note that resize it

bool isBlackKey (int pitch)
{
    const int pc = pitch % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}
} // namespace

PianoRoll::NoteGrid::NoteGrid (PianoRoll& o) : owner (o)
{
    setHelpText (Strings::help::editorGridArea);
    setWantsKeyboardFocus (true);
}

//==============================================================================
void PianoRoll::NoteGrid::setCursorBeat (double beat)
{
    const int px = beat < 0.0 ? -100 : juce::roundToInt (owner.beatToX (beat));
    if (px == cursorPixel)
        return;
    const int old = cursorPixel;
    cursorPixel = px;
    cursorBeat = beat;
    repaint (juce::Rectangle<int> (old - 2, 0, 5, getHeight()));
    repaint (juce::Rectangle<int> (px - 2, 0, 5, getHeight()));
}

juce::Point<int> PianoRoll::NoteGrid::mousePositionOrCentre() const
{
    const auto p = getMouseXYRelative();
    return getLocalBounds().contains (p) ? p : getLocalBounds().getCentre();
}

juce::ValueTree PianoRoll::NoteGrid::noteAt (juce::Point<int> p) const
{
    const auto c = owner.clip();
    if (! c.isValid())
        return {};
    // topmost = latest in the list; the notes are few enough to scan
    for (int i = c.numNotes() - 1; i >= 0; --i)
    {
        const auto node = c.state.getChild (i);
        if (owner.noteBounds (node).translated (-getX(), -getY()).contains (p))
            return node;
    }
    return {};
}

bool PianoRoll::NoteGrid::onRightEdge (const juce::ValueTree& n, juce::Point<int> p) const
{
    const auto b = owner.noteBounds (n).translated (-getX(), -getY());
    return p.x >= b.getRight() - juce::jmin (kEdgeGrab, b.getWidth() / 2);
}

//==============================================================================
void PianoRoll::NoteGrid::paint (juce::Graphics& g)
{
    const auto& t = theme();
    const auto c = owner.clip();
    if (! c.isValid())
        return;
    const auto& vw = owner.v;
    const auto scale = owner.scale();
    const int w = getWidth(), h = getHeight();

    // rows: the black keys a little darker, notes outside the scale darker still, the tonic tinted
    g.fillAll (t.panel);
    const int firstRow = juce::jmax (0, owner.yToRow (0)), lastRow = juce::jmin (owner.numRows() - 1, owner.yToRow (h));
    for (int row = firstRow; row <= lastRow; ++row)
    {
        const int pitch = owner.pitchOfRow (row);
        const int y = owner.rowToY (row);
        if (isBlackKey (pitch))
        {
            g.setColour (t.panel2.withAlpha (0.55f));
            g.fillRect (0, y, w, vw.rowHeight);
        }
        if (scale.scale != nullptr)
        {
            if (! daw::music::inScale (pitch, scale.root, *scale.scale))
            {
                g.setColour (t.background.withAlpha (0.45f));
                g.fillRect (0, y, w, vw.rowHeight);
            }
            else if (daw::music::pitchClass (pitch, scale.root) == 0)
            {
                g.setColour (t.accent.withAlpha (0.14f));
                g.fillRect (0, y, w, vw.rowHeight);
            }
        }
        g.setColour (t.line.withAlpha (pitch % 12 == 0 ? 0.9f : 0.35f));
        g.fillRect (0, y + vw.rowHeight - 1, w, 1);
    }

    // the part of the grid past the end of the clip
    const int endX = juce::roundToInt (owner.beatToX (c.lengthBeats()));
    if (endX < w)
    {
        g.setColour (t.background.withAlpha (0.5f));
        g.fillRect (juce::jmax (0, endX), 0, w - juce::jmax (0, endX), h);
    }

    // vertical lines: bars, beats, grid steps
    const double step = owner.gridStep(), bar = owner.beatsPerBar();
    const double first = std::floor (owner.xToBeat (0.0) / step) * step, last = owner.xToBeat ((double) w);
    for (double b = first; b <= last; b += step)
    {
        const int x = juce::roundToInt (owner.beatToX (b));
        const double inBar = std::fmod (b / bar + 1.0e-6, 1.0);
        const bool isBar = inBar < 2.0e-6 || std::abs (std::fmod (b, bar)) < 1.0e-6;
        const bool isBeat = std::abs (b - std::round (b)) < 1.0e-6;
        g.setColour (t.line.withAlpha (isBar ? 1.0f : (isBeat ? 0.6f : 0.28f)));
        g.fillRect (x, 0, 1, h);
    }

    // the loop brace limits and the end of the clip
    for (const double b : { c.loopStart(), c.lengthBeats() })
    {
        g.setColour (t.accent.withAlpha (0.55f));
        g.fillRect (juce::roundToInt (owner.beatToX (b)), 0, 1, h);
    }

    // notes
    const auto base = owner.clipColour();
    g.saveState();
    for (int i = 0; i < c.numNotes(); ++i)
    {
        const auto node = c.state.getChild (i);
        const auto b = owner.noteBounds (node).translated (-getX(), -getY());
        if (b.isEmpty() || ! b.intersects (getLocalBounds()))
            continue;
        const auto note = ClipRef::readNote (node);
        const bool sel = owner.isSelected (node);
        const auto r = b.toFloat().reduced (0.0f, 0.5f).withTrimmedRight (1.0f);
        const bool lit = owner.isPlayingNote (node);
        fillCell (g, r, lit ? base.brighter (0.45f) : base.withAlpha (0.4f + 0.6f * (float) note.velocity / 127.0f), 0.35f);
        if (lit)
            strokeCell (g, r.expanded (1.0f), t.play, 2.0f, 0.35f); // the note sounds right now
        else if (sel)
            strokeCell (g, r, t.text, 1.5f, 0.35f);
        else
            strokeCell (g, r, base.darker (0.5f), 1.0f, 0.35f);
        if (vw.rowHeight >= 12 && r.getWidth() > 22.0f)
        {
            g.setColour (t.clipInk);
            g.setFont (t.numFont ((float) vw.rowHeight - 5.0f));
            g.drawText (juce::MidiMessage::getMidiNoteName (note.pitch, true, true, 3), r.toNearestInt().reduced (3, 0),
                        juce::Justification::centredLeft, false);
        }
    }
    g.restoreState();

    if (gesture == Gesture::rubber)
    {
        const auto r = rubberRect().toFloat();
        g.setColour (t.accent.withAlpha (0.16f));
        g.fillRect (r);
        g.setColour (t.accent);
        g.drawRect (r, 1.0f);
    }

    if (cursorPixel >= 0)
    {
        g.setColour (t.play);
        g.fillRect (cursorPixel, 0, 2, h);
    }
    if (owner.hasKeyboardFocus (true))
    {
        g.setColour (t.accent.withAlpha (0.5f));
        g.drawRect (getLocalBounds(), 1);
    }
}

juce::Rectangle<int> PianoRoll::NoteGrid::rubberRect() const
{
    return juce::Rectangle<int>::leftTopRightBottom (juce::jmin (downPosition.x, dragPosition.x), juce::jmin (downPosition.y, dragPosition.y),
                                                     juce::jmax (downPosition.x, dragPosition.x), juce::jmax (downPosition.y, dragPosition.y));
}

//==============================================================================
int PianoRoll::NoteGrid::pitchForRow (int row, bool constrain) const
{
    int pitch = owner.pitchOfRow (row);
    if (constrain && owner.v.scaleOnly)
        if (const auto s = owner.scale(); s.scale != nullptr)
            pitch = daw::music::snapToScale (pitch, s.root, *s.scale, 0);
    return juce::jlimit (0, 127, pitch);
}

void PianoRoll::NoteGrid::beginGesture (const juce::String& name)
{
    if (transactionOpen)
        return;
    transactionOpen = true;
    owner.model.undo.beginNewTransaction (name);
}

juce::ValueTree PianoRoll::NoteGrid::addNoteAt (juce::Point<int> p, bool floorToGrid)
{
    const auto c = owner.clip();
    if (! c.isValid() || owner.numRows() == 0)
        return {};
    const int row = juce::jlimit (0, owner.numRows() - 1, owner.yToRow (p.y));
    daw::engine::Note n;
    n.pitch = pitchForRow (row, true);
    const double raw = juce::jmax (0.0, owner.xToBeat ((double) p.x));
    n.start = floorToGrid ? owner.floorBeat (raw) : owner.snapBeat (raw, false);
    n.length = owner.gridStep();
    n.velocity = owner.lastVelocity;
    beginGesture (Strings::undoNewNote);
    auto node = c.addNote (n, &owner.model.undo);
    owner.extendClipFor (n.start + n.length);
    owner.audition (n.pitch, n.velocity);
    lastAuditioned = n.pitch;
    return node;
}

//==============================================================================
void PianoRoll::NoteGrid::mouseDown (const juce::MouseEvent& e)
{
    owner.grabKeyboardFocus();
    gesture = Gesture::none;
    transactionOpen = moved = copied = false;
    origins.clear();
    drawnNote = {};
    downPosition = dragPosition = e.getPosition();
    lastAuditioned = -1;
    const auto c = owner.clip();
    if (! c.isValid())
        return;

    const auto hit = noteAt (e.getPosition());
    owner.v.editCursor = owner.snapBeat (owner.xToBeat (e.x), true);

    if (e.mods.isPopupMenu())
    {
        if (hit.isValid() && ! owner.isSelected (hit))
            owner.setSelection ({ hit });
        const int row = juce::jlimit (0, owner.numRows() - 1, owner.yToRow (e.y));
        owner.showContextMenu (e.getScreenPosition(), owner.snapBeat (owner.xToBeat (e.x), false), owner.pitchOfRow (row));
        return;
    }
    if (e.getNumberOfClicks() >= 2)
        return; // handled by mouseDoubleClick

    if (hit.isValid())
    {
        pressed = hit;
        wasSelectedOnDown = owner.isSelected (hit);
        if (e.mods.isShiftDown())
        {
            auto sel = owner.selected;
            if (wasSelectedOnDown)
                sel.erase (std::remove (sel.begin(), sel.end(), hit), sel.end());
            else
                sel.push_back (hit);
            owner.setSelection (std::move (sel));
            return;
        }
        if (! wasSelectedOnDown)
            owner.setSelection ({ hit });
        const auto note = ClipRef::readNote (hit);
        owner.lastVelocity = note.velocity;
        origins.clear();
        for (const auto& n : owner.selected)
            origins.push_back ({ n, ClipRef::readNote (n) });
        anchorStart = note.start;
        anchorEnd = note.start + note.length;
        anchorPitch = note.pitch;
        gesture = onRightEdge (hit, e.getPosition()) ? Gesture::resize : Gesture::move;
        if (gesture == Gesture::move)
        {
            owner.audition (note.pitch, note.velocity);
            lastAuditioned = note.pitch;
        }
        return;
    }

    pressed = {};
    if (owner.v.pencil)
    {
        drawnNote = addNoteAt (e.getPosition(), true);
        if (drawnNote.isValid())
        {
            owner.setSelection ({ drawnNote });
            anchorStart = ClipRef::readNote (drawnNote).start;
            gesture = Gesture::pencil;
        }
        return;
    }
    selectionBeforeRubber = e.mods.isShiftDown() ? owner.selected : clipedit::Nodes {};
    if (! e.mods.isShiftDown())
        owner.setSelection ({});
    gesture = Gesture::rubber;
}

void PianoRoll::NoteGrid::applyMove (const juce::MouseEvent& e)
{
    const auto c = owner.clip();
    if (! c.isValid() || origins.empty())
        return;
    auto& um = owner.model.undo;

    if (! copied && e.mods.isAltDown())
    {
        // Alt-drag copies: the copies are what moves from now on
        beginGesture (Strings::undoCopyNotes);
        clipedit::Nodes nodes;
        for (const auto& o : origins)
            nodes.push_back (o.node);
        auto copies = clipedit::duplicateNotes (c, nodes, 0.0, &um);
        for (size_t i = 0; i < origins.size() && i < copies.size(); ++i)
            origins[i].node = copies[i];
        owner.setSelection (copies);
        copied = true;
    }
    else
        beginGesture (Strings::undoMoveNotes);

    const bool bypass = e.mods.isCommandDown();
    const double rawStart = anchorStart + (double) (e.x - downPosition.x) / owner.v.pxPerBeat;
    double dBeats = owner.snapBeat (rawStart, bypass) - anchorStart;

    const int anchorRow = owner.rowOfPitch (anchorPitch);
    int dSemis = 0;
    if (anchorRow >= 0)
    {
        const int row = juce::jlimit (0, owner.numRows() - 1, anchorRow + (owner.yToRow (e.y) - owner.yToRow (downPosition.y)));
        dSemis = pitchForRow (row, true) - anchorPitch;
    }

    // the group stays inside 0..127 and does not start before the clip
    double minStart = 1.0e9;
    int minPitch = 127, maxPitch = 0;
    for (const auto& o : origins)
    {
        minStart = juce::jmin (minStart, o.note.start);
        minPitch = juce::jmin (minPitch, o.note.pitch);
        maxPitch = juce::jmax (maxPitch, o.note.pitch);
    }
    dBeats = juce::jmax (dBeats, -minStart);
    dSemis = juce::jlimit (-minPitch, 127 - maxPitch, dSemis);

    for (const auto& o : origins)
    {
        auto n = o.note;
        n.start += dBeats;
        n.pitch += dSemis;
        if (c.hasNote (o.node))
        {
            const auto now = ClipRef::readNote (o.node);
            if (! juce::exactlyEqual (now.start, n.start) || now.pitch != n.pitch)
                c.updateNote (o.node, n, &um);
        }
    }
    if (anchorPitch + dSemis != lastAuditioned)
    {
        lastAuditioned = anchorPitch + dSemis;
        owner.audition (lastAuditioned, ClipRef::readNote (pressed).velocity);
    }
    owner.extendClipFor (clipedit::lastNoteEnd (c));
}

void PianoRoll::NoteGrid::applyResize (const juce::MouseEvent& e)
{
    const auto c = owner.clip();
    if (! c.isValid() || origins.empty())
        return;
    beginGesture (Strings::undoResizeNotes);
    const double rawEnd = anchorEnd + (double) (e.x - downPosition.x) / owner.v.pxPerBeat;
    const double dBeats = owner.snapBeat (rawEnd, e.mods.isCommandDown()) - anchorEnd;
    for (const auto& o : origins)
    {
        auto n = o.note;
        n.length = juce::jmax (kMinNoteLength, o.note.length + dBeats);
        if (c.hasNote (o.node) && std::abs (ClipRef::readNote (o.node).length - n.length) > 1.0e-12)
            c.updateNote (o.node, n, &owner.model.undo);
    }
    owner.extendClipFor (clipedit::lastNoteEnd (c));
}

void PianoRoll::NoteGrid::applyPencil (const juce::MouseEvent& e)
{
    const auto c = owner.clip();
    if (! c.isValid() || ! c.hasNote (drawnNote))
        return;
    auto n = ClipRef::readNote (drawnNote);
    const double end = owner.snapBeat (owner.xToBeat (e.x), e.mods.isCommandDown());
    n.length = juce::jmax (owner.v.snap ? owner.gridStep() : kMinNoteLength, end - n.start);
    if (std::abs (n.length - ClipRef::readNote (drawnNote).length) > 1.0e-12)
    {
        c.updateNote (drawnNote, n, &owner.model.undo);
        owner.extendClipFor (n.start + n.length);
    }
}

void PianoRoll::NoteGrid::mouseDrag (const juce::MouseEvent& e)
{
    dragPosition = e.getPosition();
    if (gesture == Gesture::none)
        return;
    if (! moved && e.getDistanceFromDragStart() < 3)
        return;
    moved = true;
    switch (gesture)
    {
        case Gesture::move: applyMove (e); break;
        case Gesture::resize: applyResize (e); break;
        case Gesture::pencil: applyPencil (e); break;
        case Gesture::rubber:
        {
            const auto r = rubberRect();
            auto sel = selectionBeforeRubber;
            const auto c = owner.clip();
            for (int i = 0; i < c.numNotes(); ++i)
            {
                const auto node = c.state.getChild (i);
                if (owner.noteBounds (node).translated (-getX(), -getY()).intersects (r) && std::find (sel.begin(), sel.end(), node) == sel.end())
                    sel.push_back (node);
            }
            owner.selected = std::move (sel);
            owner.props->refresh();
            owner.velocityLane->repaint();
            repaint();
            break;
        }
        case Gesture::none: break;
    }
    // follow the mouse beyond the visible area
    const double margin = 24.0, scrollStep = 15.0 / owner.v.pxPerBeat;
    if (e.x > getWidth() - margin)
        owner.v.scrollBeats += scrollStep;
    else if (e.x < margin)
        owner.v.scrollBeats = juce::jmax (0.0, owner.v.scrollBeats - scrollStep);
    owner.updateScrollBars();
    repaint();
}

void PianoRoll::NoteGrid::finishGesture()
{
    gesture = Gesture::none;
    transactionOpen = false;
}

void PianoRoll::NoteGrid::mouseUp (const juce::MouseEvent& e)
{
    if (gesture == Gesture::move && ! moved && pressed.isValid() && wasSelectedOnDown && ! e.mods.isShiftDown())
        owner.setSelection ({ pressed }); // a click on a note of the group selects just that one
    if (gesture == Gesture::rubber)
        owner.setSelection (owner.selected);
    finishGesture();
    repaint();
}

void PianoRoll::NoteGrid::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto c = owner.clip();
    if (! c.isValid() || e.mods.isPopupMenu())
        return;
    if (const auto hit = noteAt (e.getPosition()); hit.isValid())
    {
        owner.model.undo.beginNewTransaction (Strings::undoDeleteNotes);
        clipedit::deleteNotes (c, { hit }, &owner.model.undo);
        owner.setSelection ({});
        return;
    }
    transactionOpen = false;
    if (auto node = addNoteAt (e.getPosition(), true); node.isValid())
        owner.setSelection ({ node });
    transactionOpen = false;
}

void PianoRoll::NoteGrid::updateCursor (juce::Point<int> p)
{
    juce::MouseCursor shape = juce::MouseCursor::NormalCursor;
    if (const auto hit = noteAt (p); hit.isValid())
        shape = onRightEdge (hit, p) ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor;
    else if (owner.v.pencil)
        shape = juce::MouseCursor::CrosshairCursor;
    setMouseCursor (shape);
}

void PianoRoll::NoteGrid::mouseMove (const juce::MouseEvent& e)
{
    updateCursor (e.getPosition());
}

void PianoRoll::NoteGrid::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown())
        owner.zoomHorizontally (w.deltaY > 0 ? 1.15 : 1.0 / 1.15, (double) e.x);
    else if (e.mods.isAltDown())
        owner.setRowHeight (owner.v.rowHeight + (w.deltaY > 0 ? 1 : -1), e.y);
    else
    {
        const bool horizontal = e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY);
        const float d = horizontal ? (std::abs (w.deltaX) > 0.0f ? w.deltaX : w.deltaY) : w.deltaY;
        if (horizontal)
            owner.v.scrollBeats = juce::jmax (0.0, owner.v.scrollBeats - (double) d * 400.0 / owner.v.pxPerBeat);
        else
            owner.v.scrollY -= juce::roundToInt (d * 300.0f);
        owner.clampView();
        owner.updateScrollBars();
        owner.ruler->repaint();
        owner.keys->repaint();
        owner.velocityLane->repaint();
        repaint();
    }
}

void PianoRoll::NoteGrid::mouseMagnify (const juce::MouseEvent& e, float factor)
{
    owner.zoomHorizontally ((double) factor, (double) e.x);
}
} // namespace ui
