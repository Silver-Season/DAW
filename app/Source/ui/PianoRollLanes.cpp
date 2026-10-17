#include "PianoRoll.h"

#include "PianoRollParts.h"

#include "../engine/AudioEngine.h"

namespace ui
{
namespace
{
constexpr int kBraceHeight = 11;
constexpr int kGrab = 5;

bool isBlackKey (int pitch)
{
    const int pc = pitch % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}
} // namespace

//==============================================================================
// Ruler
//==============================================================================
PianoRoll::Ruler::Part PianoRoll::Ruler::partAt (juce::Point<int> p) const
{
    const auto c = owner.clip();
    if (! c.isValid())
        return Part::none;
    const double sx = owner.beatToX (c.loopStart()), ex = owner.beatToX (c.lengthBeats());
    if (braceArea().contains (p))
    {
        if (std::abs (p.x - sx) <= kGrab)
            return Part::braceStart;
        if (std::abs (p.x - ex) <= kGrab)
            return Part::braceEnd;
        if (p.x > sx && p.x < ex)
            return Part::braceBody;
        return Part::none;
    }
    if (std::abs (p.x - owner.beatToX (c.startOffset())) <= 7)
        return Part::startPoint;
    return Part::none;
}

void PianoRoll::Ruler::updateCursor (Part part)
{
    switch (part)
    {
        case Part::braceStart:
        case Part::braceEnd: setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;
        case Part::braceBody: setMouseCursor (juce::MouseCursor::DraggingHandCursor); break;
        case Part::startPoint: setMouseCursor (juce::MouseCursor::PointingHandCursor); break;
        case Part::none: setMouseCursor (juce::MouseCursor::NormalCursor); break;
    }
}

void PianoRoll::Ruler::paint (juce::Graphics& g)
{
    const auto& t = theme();
    const auto c = owner.clip();
    g.fillAll (t.chrome);
    g.setColour (t.line);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);
    if (! c.isValid())
        return;

    // bar numbers and ticks
    const double bar = owner.beatsPerBar();
    const double first = std::floor (owner.xToBeat (0.0)), last = owner.xToBeat ((double) getWidth());
    g.setFont (t.numFont (10.5f));
    const int labelEvery = owner.v.pxPerBeat * bar < 34.0 ? (owner.v.pxPerBeat * bar < 17.0 ? 4 : 2) : 1;
    for (double b = first; b <= last; b += 1.0)
    {
        const int x = juce::roundToInt (owner.beatToX (b));
        const double barIndex = b / bar;
        const bool isBar = std::abs (barIndex - std::round (barIndex)) < 1.0e-6;
        g.setColour (t.line);
        g.fillRect (x, isBar ? 2 : getHeight() - kBraceHeight - 6, 1, isBar ? getHeight() - kBraceHeight - 2 : 5);
        if (isBar && ((int) std::lround (barIndex)) % labelEvery == 0)
        {
            g.setColour (t.dimText);
            g.drawText (juce::String ((int) std::lround (barIndex) + 1), x + 3, 1, 40, 14, juce::Justification::centredLeft, false);
        }
    }

    // the loop brace
    const auto area = braceArea();
    const float sx = (float) owner.beatToX (c.loopStart()), ex = (float) owner.beatToX (c.lengthBeats());
    const bool on = c.loopEnabled();
    const bool flash = owner.loopFlashing();
    const auto colour = flash ? t.accent2 : (on ? t.accent : t.dimText);
    auto brace = juce::Rectangle<float>::leftTopRightBottom (sx, (float) area.getY() + 1.0f, ex, (float) area.getBottom() - 2.0f);
    if (brace.getWidth() > 0.0f)
    {
        g.setColour (colour.withAlpha (on ? 0.45f : 0.22f));
        g.fillRoundedRectangle (brace, 2.0f);
        g.setColour (colour);
        g.fillRect (brace.withWidth (3.0f));
        g.fillRect (brace.withLeft (brace.getRight() - 3.0f));
    }

    // the start point: a flag above the brace
    const float px = (float) owner.beatToX (c.startOffset());
    juce::Path flag;
    flag.addTriangle (px - 5.0f, 15.0f, px + 5.0f, 15.0f, px, 24.0f);
    g.setColour (t.play);
    g.fillPath (flag);
    g.fillRect (px - 0.5f, 15.0f, 1.0f, 2.0f);

    if (owner.hasClip() && owner.engine.isPlaying())
    {
        // the playing position is drawn by the grid; nothing here
    }
}

void PianoRoll::Ruler::mouseMove (const juce::MouseEvent& e)
{
    updateCursor (partAt (e.getPosition()));
}

void PianoRoll::Ruler::mouseDown (const juce::MouseEvent& e)
{
    owner.grabKeyboardFocus();
    changed = false;
    dragPart = partAt (e.getPosition());
    const auto c = owner.clip();
    if (! c.isValid())
        return;
    origStart = c.loopStart();
    origEnd = c.lengthBeats();
    origOffset = c.startOffset();
    anchorBeat = owner.xToBeat (e.x);
    if (e.getNumberOfClicks() >= 2 && dragPart == Part::braceBody)
    {
        owner.model.undo.beginNewTransaction (Strings::undoLoopSwitch);
        c.setLoopEnabled (! c.loopEnabled(), &owner.model.undo);
        dragPart = Part::none;
    }
}

void PianoRoll::Ruler::mouseDrag (const juce::MouseEvent& e)
{
    const auto c = owner.clip();
    if (! c.isValid() || dragPart == Part::none)
        return;
    const bool bypass = e.mods.isCommandDown();
    const double step = owner.gridStep();
    const double beat = owner.snapBeat (owner.xToBeat (e.x), bypass);
    auto& um = owner.model.undo;
    const auto open = [&] (const juce::String& name) {
        if (! changed)
            um.beginNewTransaction (name);
        changed = true;
    };
    const double minLength = owner.v.snap && ! bypass ? step : kMinNoteLength;

    switch (dragPart)
    {
        case Part::braceStart:
        {
            const double value = juce::jlimit (0.0, origEnd - minLength, beat);
            if (std::abs (value - c.loopStart()) > 1.0e-12)
            {
                open (Strings::undoLoopRange);
                c.setLoopStart (value, &um);
            }
            break;
        }
        case Part::braceEnd:
        {
            const double value = juce::jmax (origStart + minLength, beat);
            if (std::abs (value - c.lengthBeats()) > 1.0e-12)
            {
                open (Strings::undoLoopRange);
                c.setLengthBeats (value, &um);
                if (c.startOffset() >= value)
                    c.setStartOffset (origStart, &um);
            }
            break;
        }
        case Part::braceBody:
        {
            // slides the whole brace; the length of the clip moves with its end
            const double delta = owner.snapBeat (origStart + (owner.xToBeat (e.x) - anchorBeat), bypass) - origStart;
            const double d = juce::jmax (delta, -origStart);
            if (std::abs (c.loopStart() - (origStart + d)) > 1.0e-12)
            {
                open (Strings::undoLoopRange);
                c.setLoopStart (origStart + d, &um);
                c.setLengthBeats (origEnd + d, &um);
            }
            break;
        }
        case Part::startPoint:
        {
            const double value = juce::jlimit (0.0, juce::jmax (0.0, c.lengthBeats() - minLength), beat);
            if (std::abs (value - c.startOffset()) > 1.0e-12)
            {
                open (Strings::undoStartPoint);
                c.setStartOffset (value, &um);
            }
            break;
        }
        case Part::none: break;
    }
}

void PianoRoll::Ruler::mouseUp (const juce::MouseEvent&)
{
    dragPart = Part::none;
    changed = false;
}

//==============================================================================
// Keys
//==============================================================================
int PianoRoll::Keys::pitchAt (int y) const
{
    return owner.pitchOfRow (juce::jlimit (0, owner.numRows() - 1, owner.yToRow (y)));
}

void PianoRoll::Keys::paint (juce::Graphics& g)
{
    const auto& t = theme();
    g.fillAll (t.panel);
    if (! owner.hasClip())
        return;
    const auto& vw = owner.v;
    const auto scale = owner.scale();
    const int firstRow = juce::jmax (0, owner.yToRow (0)), lastRow = juce::jmin (owner.numRows() - 1, owner.yToRow (getHeight()));
    const auto accent = owner.clipColour();
    const bool dark = t.background.getBrightness() < 0.5f;
    const auto whiteKey = dark ? t.text.withAlpha (0.82f) : t.panel, blackKey = dark ? t.background : t.text.withAlpha (0.8f);
    const auto keyInk = dark ? t.background : t.text;
    for (int row = firstRow; row <= lastRow; ++row)
    {
        const int pitch = owner.pitchOfRow (row), y = owner.rowToY (row);
        const bool black = isBlackKey (pitch);
        const bool inScale = scale.scale != nullptr && daw::music::inScale (pitch, scale.root, *scale.scale);
        const bool held = pitch == heldPitch;

        if (held)
        {
            g.setColour (accent);
            g.fillRect (0, y, getWidth(), vw.rowHeight);
        }
        else
        {
            // white keys light, black keys dark, whatever the skin: the lightness decides which way round
            g.setColour (whiteKey);
            g.fillRect (0, y, getWidth(), vw.rowHeight);
            if (black)
            {
                g.setColour (blackKey);
                g.fillRect (0, y, getWidth() * 3 / 5, vw.rowHeight);
            }
            if (inScale && daw::music::pitchClass (pitch, scale.root) == 0)
            {
                g.setColour (t.accent.withAlpha (0.45f));
                g.fillRect (0, y, getWidth(), vw.rowHeight);
            }
        }
        g.setColour (t.line);
        g.fillRect (0, y + vw.rowHeight - 1, getWidth(), 1);

        const bool label = pitch % 12 == 0 || vw.fold || (inScale && vw.rowHeight >= 14);
        if (label && vw.rowHeight >= 9)
        {
            g.setColour (held ? t.clipInk : keyInk.withAlpha (pitch % 12 == 0 ? 1.0f : 0.6f));
            g.setFont (t.numFont (juce::jmin (11.0f, (float) vw.rowHeight - 3.0f), pitch % 12 == 0 ? 600 : 400));
            g.drawText (juce::MidiMessage::getMidiNoteName (pitch, true, true, 3), getLocalBounds().withY (y).withHeight (vw.rowHeight).withTrimmedRight (4),
                        juce::Justification::centredRight, false);
        }
    }
    g.setColour (t.line);
    g.fillRect (getWidth() - 1, 0, 1, getHeight());
}

void PianoRoll::Keys::mouseDown (const juce::MouseEvent& e)
{
    owner.grabKeyboardFocus();
    if (! owner.hasClip())
        return;
    heldPitch = pitchAt (e.y);
    owner.noteOnKey (heldPitch);
    owner.selectPitch (heldPitch);
    repaint();
}

void PianoRoll::Keys::mouseDrag (const juce::MouseEvent& e)
{
    if (heldPitch < 0)
        return;
    const int p = pitchAt (e.y);
    if (p != heldPitch)
    {
        owner.noteOffKey (heldPitch);
        heldPitch = p;
        owner.noteOnKey (p);
        owner.selectPitch (p);
        repaint();
    }
}

void PianoRoll::Keys::mouseUp (const juce::MouseEvent&)
{
    if (heldPitch >= 0)
        owner.noteOffKey (heldPitch);
    heldPitch = -1;
    repaint();
}

void PianoRoll::Keys::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    owner.grid->mouseWheelMove (e.getEventRelativeTo (owner.grid.get()), w);
}

//==============================================================================
// Velocity lane
//==============================================================================
int PianoRoll::VelocityLane::barX (const juce::ValueTree& n) const
{
    return juce::roundToInt (owner.beatToX (ClipRef::readNote (n).start));
}

int PianoRoll::VelocityLane::velocityAt (int y) const
{
    const int top = 4, bottom = getHeight() - 2;
    return juce::jlimit (1, 127, juce::roundToInt (127.0 * (double) (bottom - y) / (double) (bottom - top)));
}

juce::ValueTree PianoRoll::VelocityLane::noteNear (int x) const
{
    const auto c = owner.clip();
    juce::ValueTree best;
    int bestDistance = 6;
    for (int i = 0; i < c.numNotes(); ++i)
    {
        const auto node = c.state.getChild (i);
        const int d = std::abs (barX (node) + 1 - x);
        // among stacked notes (a chord) the later one wins: the lane shows the loudest, and a tie goes to the last
        if (d <= bestDistance)
        {
            bestDistance = d;
            best = node;
        }
    }
    return best;
}

void PianoRoll::VelocityLane::paint (juce::Graphics& g)
{
    const auto& t = theme();
    g.fillAll (t.panel);
    g.setColour (t.line);
    g.fillRect (0, 0, getWidth(), 1);
    const auto c = owner.clip();
    if (! c.isValid())
        return;
    const int left = PianoRoll::kKeysWidth;
    g.setColour (t.chrome);
    g.fillRect (0, 1, left, getHeight() - 1);
    g.setColour (t.dimText);
    g.setFont (t.uiFont (10.5f, 500));
    g.drawText (Strings::editor::velocity, juce::Rectangle<int> (0, 1, left - 2, getHeight() - 1).reduced (4, 0), juce::Justification::centredLeft, false);

    g.saveState();
    g.reduceClipRegion (left, 1, getWidth() - left, getHeight() - 1);
    const int bottom = getHeight() - 2, top = 4;
    // the text variant of the colour: pale clip colours of the light skins would vanish on the panel
    const auto base = t.textPalette (owner.clip().colour());
    // the gridlines at 25 / 50 / 75 %
    for (int q = 1; q <= 3; ++q)
    {
        g.setColour (t.line.withAlpha (0.4f));
        g.fillRect (left, bottom - (bottom - top) * q / 4, getWidth() - left, 1);
    }
    for (int i = 0; i < c.numNotes(); ++i)
    {
        const auto node = c.state.getChild (i);
        const auto note = ClipRef::readNote (node);
        const int x = left + barX (node);
        if (x < left - 4 || x > getWidth())
            continue;
        const int h = juce::roundToInt ((double) (bottom - top) * note.velocity / 127.0);
        const bool sel = owner.isSelected (node);
        g.setColour (base.withAlpha (sel ? 1.0f : 0.7f));
        g.fillRect (x, bottom - h, 3, h);
        g.fillEllipse ((float) x - 1.5f, (float) (bottom - h) - 3.0f, 6.0f, 6.0f);
        if (sel)
        {
            g.setColour (t.text);
            g.drawEllipse ((float) x - 1.5f, (float) (bottom - h) - 3.0f, 6.0f, 6.0f, 1.0f);
        }
    }
    g.restoreState();
}

void PianoRoll::VelocityLane::mouseDown (const juce::MouseEvent& e)
{
    owner.grabKeyboardFocus();
    const auto c = owner.clip();
    transactionOpen = false;
    originals.clear();
    grabbed = {};
    ramp = e.mods.isAltDown();
    downPosition = e.getPosition();
    if (! c.isValid())
        return;
    if (ramp)
    {
        for (const auto& n : clipedit::selectionOrAll (c, owner.selected))
            originals.emplace_back (n, ClipRef::readNote (n).velocity);
        return;
    }
    grabbed = noteNear (e.x - PianoRoll::kKeysWidth);
    if (! grabbed.isValid())
        return;
    if (! owner.isSelected (grabbed))
        owner.setSelection ({ grabbed });
    grabbedOriginal = ClipRef::readNote (grabbed).velocity;
    for (const auto& n : owner.selected)
        originals.emplace_back (n, ClipRef::readNote (n).velocity);
    mouseDrag (e);
}

void PianoRoll::VelocityLane::applyRamp (const juce::MouseEvent& e)
{
    const auto c = owner.clip();
    // A line from where the drag began to where it is now; the notes between get the height of the line.
    const double x0 = downPosition.x, x1 = e.x;
    const int v0 = velocityAt (downPosition.y), v1 = velocityAt (e.y);
    if (std::abs (x1 - x0) < 2.0)
        return;
    for (const auto& [node, original] : originals)
    {
        juce::ignoreUnused (original);
        const double x = barX (node) + PianoRoll::kKeysWidth + 1;
        const double f = (x - x0) / (x1 - x0);
        if (f < 0.0 || f > 1.0)
            continue;
        const int velocity = juce::jlimit (1, 127, juce::roundToInt (v0 + f * (v1 - v0)));
        if (c.hasNote (node) && ClipRef::readNote (node).velocity != velocity)
        {
            if (! transactionOpen)
                owner.model.undo.beginNewTransaction (Strings::undoVelocity);
            transactionOpen = true;
            clipedit::setVelocity (c, node, velocity, &owner.model.undo);
        }
    }
}

void PianoRoll::VelocityLane::mouseDrag (const juce::MouseEvent& e)
{
    const auto c = owner.clip();
    if (! c.isValid())
        return;
    if (ramp)
    {
        applyRamp (e);
        repaint();
        return;
    }
    if (! grabbed.isValid() || ! c.hasNote (grabbed))
        return;
    // The grabbed bar follows the mouse; the other selected notes keep their distance from it.
    const int delta = velocityAt (e.y) - grabbedOriginal;
    for (const auto& [node, original] : originals)
    {
        const int velocity = juce::jlimit (1, 127, original + delta);
        if (c.hasNote (node) && ClipRef::readNote (node).velocity != velocity)
        {
            if (! transactionOpen)
                owner.model.undo.beginNewTransaction (Strings::undoVelocity);
            transactionOpen = true;
            clipedit::setVelocity (c, node, velocity, &owner.model.undo);
        }
    }
    owner.lastVelocity = ClipRef::readNote (grabbed).velocity;
    repaint();
}

void PianoRoll::VelocityLane::mouseUp (const juce::MouseEvent&)
{
    if (grabbed.isValid() && transactionOpen)
        owner.audition (ClipRef::readNote (grabbed).pitch, ClipRef::readNote (grabbed).velocity);
    transactionOpen = false;
    originals.clear();
    grabbed = {};
    ramp = false;
}

void PianoRoll::VelocityLane::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    owner.grid->mouseWheelMove (e.getEventRelativeTo (owner.grid.get()), w);
}

//==============================================================================
// Controller strip
//==============================================================================
juce::Rectangle<int> PianoRoll::ControllerStrip::tabArea (int i) const
{
    return { PianoRoll::kKeysWidth + 8 + i * 92, 2, 88, PianoRoll::kControllerHeader - 4 };
}

juce::Rectangle<int> PianoRoll::ControllerStrip::bodyArea() const
{
    return { PianoRoll::kKeysWidth, PianoRoll::kControllerHeader, juce::jmax (0, getWidth() - PianoRoll::kKeysWidth - PianoRoll::kScrollBar),
             juce::jmax (0, getHeight() - PianoRoll::kControllerHeader) };
}

void PianoRoll::ControllerStrip::paint (juce::Graphics& g)
{
    const auto& t = theme();
    g.fillAll (t.chrome);
    g.setColour (t.line);
    g.fillRect (0, 0, getWidth(), 1);
    const bool open = owner.v.controllersOpen;
    juce::Path arrow = icons::triangle ({ 8.0f, 6.0f, 9.0f, 9.0f }, open ? 2 : 1);
    g.setColour (t.dimText);
    g.fillPath (arrow);
    g.setFont (t.uiFont (10.5f, 500));
    g.drawText (Strings::editor::controllers, 22, 0, PianoRoll::kKeysWidth, PianoRoll::kControllerHeader, juce::Justification::centredLeft, false);
    if (! open)
        return;
    static const juce::String* names[] = { &Strings::editor::modwheel, &Strings::editor::pitchBend, &Strings::editor::sustain };
    for (int i = 0; i < 3; ++i)
    {
        const bool on = owner.v.controller == i;
        auto r = tabArea (i).toFloat();
        fillCell (g, r, on ? t.panel2.brighter (0.1f) : t.panel2, 0.6f);
        strokeCell (g, r, on ? t.accent : t.line, 1.0f, 0.6f);
        g.setColour (on ? t.text : t.dimText);
        g.drawText (*names[i], r.toNearestInt(), juce::Justification::centred, false);
    }

    const auto body = bodyArea();
    if (body.isEmpty())
        return;
    g.setColour (t.panel);
    g.fillRect (body);
    const auto clip = owner.clip();
    if (! clip.isValid())
        return;

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (body);
    const int top = body.getY() + 3, bottom = body.getBottom() - 3;
    const auto xOf = [this] (double beat) { return (double) PianoRoll::kKeysWidth + owner.beatToX (beat); };
    const auto yOf = [top, bottom] (double v) { return (float) bottom - (float) v * (float) (bottom - top); };

    // bar lines full, beat lines faint (the same rhythm as the grid above)
    const double bpb = owner.beatsPerBar();
    const double firstBeat = std::floor (owner.xToBeat (0.0)), lastBeat = std::ceil (owner.xToBeat ((double) body.getWidth()));
    for (double b = juce::jmax (0.0, firstBeat); b <= lastBeat; b += 1.0)
    {
        const bool barLine = std::abs (std::fmod (b, bpb)) < 1.0e-6;
        if (! barLine && owner.v.pxPerBeat < 14.0)
            continue;
        g.setColour (t.line.withAlpha (barLine ? 1.0f : 0.4f));
        g.fillRect ((int) std::round (xOf (b)), body.getY(), 1, body.getHeight());
    }
    for (int q = 1; q <= 3; ++q)
    {
        g.setColour (t.line.withAlpha (q == 2 && lane() == clipedit::ControllerLane::pitchBend ? 0.9f : 0.3f));
        g.fillRect (body.getX(), (int) yOf ((double) q / 4.0), body.getWidth(), 1);
    }

    const float clipEnd = (float) xOf (clip.lengthBeats());
    if (clipEnd < (float) body.getRight())
    {
        g.setColour (t.chrome.withAlpha (0.6f));
        g.fillRect (juce::jmax ((float) body.getX(), clipEnd), (float) body.getY(), (float) body.getRight() - juce::jmax ((float) body.getX(), clipEnd), (float) body.getHeight());
    }

    // The step line: each event holds its value until the next one (or the end of the clip).
    std::vector<daw::engine::ControllerEvent> events;
    for (const auto& e : clip.controllers())
        if (clipedit::isLaneEvent (e, lane()))
            events.push_back (e);
    if (events.empty())
    {
        g.setColour (t.dimText);
        g.setFont (t.uiFont (11.0f));
        g.drawText (Strings::editor::controllersEmpty, body.reduced (8, 0), juce::Justification::centredLeft, true);
        return;
    }
    const auto colour = t.textPalette (clip.colour());
    juce::Path line;
    float lastY = 0.0f;
    for (size_t i = 0; i < events.size(); ++i)
    {
        const float x = (float) xOf (events[i].time);
        const float y = yOf (clipedit::laneValue (events[i], lane()));
        if (i == 0)
            line.startNewSubPath (x, y);
        else
        {
            line.lineTo (x, lastY);
            line.lineTo (x, y);
        }
        lastY = y;
    }
    line.lineTo ((float) xOf (juce::jmax (clip.lengthBeats(), events.back().time)), lastY);

    juce::Path area (line);
    area.lineTo (area.getCurrentPosition().x, (float) bottom);
    area.lineTo ((float) xOf (events.front().time), (float) bottom);
    area.closeSubPath();
    g.setColour (colour.withAlpha (0.22f));
    g.fillPath (area);
    g.setColour (colour);
    g.strokePath (line, juce::PathStrokeType (1.5f));
    for (const auto& e : events)
        g.fillEllipse ((float) xOf (e.time) - 2.0f, yOf (clipedit::laneValue (e, lane())) - 2.0f, 4.0f, 4.0f);
}

void PianoRoll::ControllerStrip::addStrokePoint (juce::Point<int> p)
{
    const auto body = bodyArea();
    const auto clip = owner.clip();
    if (! clip.isValid() || body.isEmpty())
        return;
    const int top = body.getY() + 3, bottom = body.getBottom() - 3;
    const double beat = owner.xToBeat ((double) (p.x - PianoRoll::kKeysWidth));
    const double value = juce::jlimit (0.0, 1.0, (double) (bottom - p.y) / (double) juce::jmax (1, bottom - top));
    stroke.emplace_back (juce::jlimit (0.0, clip.lengthBeats(), beat), value);

    // Redrawn from what the clip held when the stroke began, in the one transaction of the stroke.
    const auto events = clipedit::drawControllerStroke (original, lane(), stroke, clip.lengthBeats());
    if (! transactionOpen)
    {
        owner.model.undo.beginNewTransaction (Strings::recording::undoDrawController);
        transactionOpen = true;
    }
    clip.setControllers (events, &owner.model.undo);
    repaint();
}

void PianoRoll::ControllerStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (drawing)
        addStrokePoint (e.getPosition());
}

void PianoRoll::ControllerStrip::mouseUp (const juce::MouseEvent&)
{
    drawing = transactionOpen = false;
    stroke.clear();
    original.clear();
}

void PianoRoll::ControllerStrip::mouseDown (const juce::MouseEvent& e)
{
    owner.grabKeyboardFocus();
    if (owner.v.controllersOpen)
        for (int i = 0; i < 3; ++i)
            if (tabArea (i).contains (e.getPosition()))
            {
                owner.v.controller = i;
                repaint();
                return;
            }
    if (e.y < PianoRoll::kControllerHeader)
    {
        owner.v.controllersOpen = ! owner.v.controllersOpen;
        owner.layoutParts();
        return;
    }
    if (owner.v.controllersOpen && bodyArea().contains (e.getPosition()))
        if (const auto clip = owner.clip(); clip.isValid())
        {
            drawing = true;
            transactionOpen = false;
            stroke.clear();
            original = clip.controllers();
            addStrokePoint (e.getPosition());
        }
}
} // namespace ui
