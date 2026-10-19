#include "ArrangementRuler.h"

#include "../Strings.h"
#include "../engine/AudioEngine.h"
#include "ArrangementLanes.h"
#include "PaletteMenu.h"

#include <cmath>

namespace ui
{
namespace
{
constexpr int kBraceTop = ArrangementView::kMarkerLaneHeight;
constexpr int kBarsTop = ArrangementView::kMarkerLaneHeight + ArrangementView::kBraceHeight;
constexpr int kEdgeGrab = 7;

juce::Colour markerColour (const MarkerRef& m)
{
    return m.colour() >= 0 ? theme().palette (m.colour()) : theme().accent2;
}
} // namespace

ArrangementRuler::ArrangementRuler (ArrangementView& v) : view (v)
{
    setOpaque (true);
}

//==============================================================================
juce::Rectangle<int> ArrangementRuler::markerBounds (int64_t markerId) const
{
    for (const auto& m : view.model.markers())
        if (m.id() == markerId)
        {
            const int x = juce::roundToInt (view.beatToX (m.beat()));
            const int w = 12 + juce::roundToInt (juce::GlyphArrangement::getStringWidth (theme().uiFont (11.0f, 500), m.name()));
            if (theme().glass)
            {
                // glass: a section band reaching to the next marker (the last one runs eight bars)
                double next = m.beat() + 8.0 * view.beatsPerBar();
                bool found = false;
                for (const auto& other : view.model.markers())
                    if (other.beat() > m.beat() + 1.0e-6 && (! found || other.beat() < next))
                    {
                        next = other.beat();
                        found = true;
                    }
                const int x2 = juce::roundToInt (view.beatToX (next));
                return { x + 1, 2, juce::jmax (juce::jmax (14, w), x2 - x - 2), ArrangementView::kMarkerLaneHeight - 4 };
            }
            return { x, 1, juce::jmax (14, w), ArrangementView::kMarkerLaneHeight - 2 };
        }
    return {};
}

int64_t ArrangementRuler::markerAt (juce::Point<int> p) const
{
    int64_t best = -1;
    for (const auto& m : view.model.markers())
    {
        const auto r = markerBounds (m.id()).expanded (3, 0);
        if (r.contains (p))
            best = m.id(); // later markers are drawn on top
    }
    return best;
}

int ArrangementRuler::braceEdgeAt (int x) const
{
    const int x1 = juce::roundToInt (view.beatToX (view.model.arrangementLoopStart()));
    const int x2 = juce::roundToInt (view.beatToX (view.model.arrangementLoopEnd()));
    if (std::abs (x - x1) <= kEdgeGrab)
        return 1;
    if (std::abs (x - x2) <= kEdgeGrab)
        return 2;
    return x > x1 && x < x2 ? 3 : 0;
}

//==============================================================================
void ArrangementRuler::paintBars (juce::Graphics& g, juce::Rectangle<int> area) const
{
    const auto& t = theme();
    const double px = view.v.pxPerBeat, bar = view.beatsPerBar();
    const double from = view.xToBeat ((double) area.getX()), to = view.xToBeat ((double) area.getRight());
    const int labelStride = view.barLabelStride(), lineStride = view.barLineStride();
    const int h = ArrangementView::kBarRulerHeight;

    // selected time
    if (! view.timeSel.isEmpty())
    {
        const float x1 = (float) view.beatToX (view.timeSel.start), x2 = (float) view.beatToX (view.timeSel.end);
        g.setColour (t.accent.withAlpha (0.28f));
        g.fillRect (x1, (float) kBarsTop, x2 - x1, (float) h);
    }

    g.setFont (t.numFont (11.0f));
    const long long firstBar = (long long) std::floor (from / bar);
    for (long long b = firstBar - (firstBar % lineStride); b * bar <= to + bar; b += lineStride)
    {
        if (b < 0)
            continue;
        const int x = juce::roundToInt (view.beatToX ((double) b * bar));
        g.setColour (t.line);
        g.fillRect (x, kBarsTop + 10, 1, h - 10);
        if (b % labelStride == 0)
        {
            g.setColour (t.dimText);
            g.drawText (juce::String (b + 1), x + 4, kBarsTop + 4, 60, 14, juce::Justification::centredLeft, false);
        }
    }
    // beats and finer ticks when there is room
    const double step = view.gridStep();
    if (step * px >= 6.0)
    {
        g.setColour (t.line.withAlpha (0.8f));
        for (double beat = std::floor (from / step) * step; beat <= to + step; beat += step)
        {
            const double barPos = beat / bar;
            if (std::abs (barPos - std::round (barPos)) < 1.0e-6)
                continue;
            const bool onBeat = std::abs (beat - std::round (beat)) < 1.0e-6;
            const int x = juce::roundToInt (view.beatToX (beat));
            g.fillRect (x, kBarsTop + (onBeat ? h - 7 : h - 4), 1, onBeat ? 7 : 4);
        }
    }
}

void ArrangementRuler::paintBrace (juce::Graphics& g) const
{
    const auto& t = theme();
    const auto& m = view.model;
    const float x1 = (float) view.beatToX (m.arrangementLoopStart()), x2 = (float) view.beatToX (m.arrangementLoopEnd());
    const bool on = m.arrangementLoopOn();
    const juce::Rectangle<float> r (x1, (float) kBraceTop + 1.0f, juce::jmax (2.0f, x2 - x1), (float) ArrangementView::kBraceHeight - 2.0f);
    if (t.glass)
    {
        // glass: a soft accent band with round handles at the ends
        g.setColour (t.accentFill.withAlpha (on ? 0.32f : 0.12f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        for (const float x : { r.getX(), r.getRight() })
        {
            const auto handle = juce::Rectangle<float> (8.0f, r.getHeight()).withCentre ({ x, r.getCentreY() });
            g.setColour (on ? t.accentFill.interpolatedWith (t.text, 0.15f) : t.dimText.withAlpha (0.6f));
            g.fillRoundedRectangle (handle, 4.0f);
        }
        return;
    }
    g.setColour (t.accent.withAlpha (on ? 0.7f : 0.2f));
    g.fillRoundedRectangle (r, 2.0f);
    if (! on)
    {
        g.setColour (t.accent.withAlpha (0.45f));
        g.drawRoundedRectangle (r.reduced (0.5f), 2.0f, 1.0f);
    }
    g.setColour (t.accent.withAlpha (on ? 1.0f : 0.5f));
    g.fillRect (r.withWidth (3.0f));
    g.fillRect (r.withLeft (r.getRight() - 3.0f));
}

void ArrangementRuler::paintMarkers (juce::Graphics& g) const
{
    const auto& t = theme();
    g.setFont (t.uiFont (11.0f, 500));
    for (const auto& m : view.model.markers())
    {
        const auto r = markerBounds (m.id());
        if (r.getRight() < 0 || r.getX() > getWidth())
            continue;
        const auto colour = markerColour (m);
        const bool selected = m.id() == view.markerSel;
        if (t.glass)
        {
            // a pastel section band, its name in the middle
            const auto band = r.toFloat();
            const auto tone = m.colour() >= 0 ? colour : t.palette ((int) (std::abs (m.id()) * 3 % 16));
            g.setColour (t.panel.interpolatedWith (tone, selected ? 0.55f : 0.32f));
            g.fillRoundedRectangle (band, 6.0f);
            if (selected)
            {
                g.setColour (tone.interpolatedWith (t.text, 0.3f));
                g.drawRoundedRectangle (band.reduced (0.5f), 6.0f, 1.2f);
            }
            g.setColour (t.text);
            g.drawText (m.name(), r.reduced (6, 0), band.getWidth() > 90.0f ? juce::Justification::centred : juce::Justification::centredLeft, true);
            continue;
        }
        g.setColour (colour);
        g.fillRect (r.getX(), 0, 2, ArrangementView::kMarkerLaneHeight);
        const auto pill = r.withTrimmedLeft (2).toFloat();
        g.setColour (colour.withAlpha (selected ? 0.55f : 0.22f));
        g.fillRoundedRectangle (pill, 3.0f);
        if (selected)
        {
            g.setColour (t.text);
            g.drawRoundedRectangle (pill.reduced (0.5f), 3.0f, 1.0f);
        }
        g.setColour (selected ? t.text : (m.colour() >= 0 ? t.textPalette (m.colour()) : t.accent2));
        g.drawText (m.name(), r.withTrimmedLeft (7), juce::Justification::centredLeft, true);
    }
}

void ArrangementRuler::paint (juce::Graphics& g)
{
    const auto& t = theme();
    g.fillAll (t.chrome);
    g.setColour (t.panel);
    g.fillRect (0, 0, getWidth(), ArrangementView::kMarkerLaneHeight);
    g.setColour (t.line);
    g.fillRect (0, ArrangementView::kMarkerLaneHeight - 1, getWidth(), 1);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);

    paintBars (g, g.getClipBounds());
    paintBrace (g);

    // the play start
    {
        const float x = (float) view.beatToX (view.model.arrangementPlayStart());
        if (x >= -6.0f && x <= (float) getWidth() + 6.0f)
        {
            juce::Path tri;
            tri.addTriangle (x - 4.5f, (float) kBarsTop, x + 4.5f, (float) kBarsTop, x, (float) kBarsTop + 7.0f);
            g.setColour (t.text);
            g.fillPath (tri);
        }
    }
    paintMarkers (g);
}

//==============================================================================
void ArrangementRuler::mouseMove (const juce::MouseEvent& e)
{
    auto shape = juce::MouseCursor::NormalCursor;
    switch (zoneAt (e.y))
    {
        case Zone::markers:
            if (markerAt (e.getPosition()) >= 0)
                shape = juce::MouseCursor::PointingHandCursor;
            break;
        case Zone::brace:
        {
            const int edge = braceEdgeAt (e.x);
            shape = edge == 1 || edge == 2 ? juce::MouseCursor::LeftRightResizeCursor : (edge == 3 ? juce::MouseCursor::DraggingHandCursor : shape);
            break;
        }
        case Zone::bars: break;
    }
    setMouseCursor (shape);
}

void ArrangementRuler::mouseDown (const juce::MouseEvent& e)
{
    view.takeFocus();
    gesture = Gesture::none;
    moved = false;
    transactionOpen = false;
    pressedMarker = -1;
    pressBeat = view.xToBeat (e.x);

    switch (zoneAt (e.y))
    {
        case Zone::markers:
        {
            const auto id = markerAt (e.getPosition());
            if (id < 0)
            {
                view.selectMarker (-1);
                return;
            }
            view.selectMarker (id);
            if (e.mods.isPopupMenu())
            {
                showMarkerMenu (e.getScreenPosition(), id);
                return;
            }
            gesture = Gesture::marker;
            pressedMarker = id;
            for (const auto& m : view.model.markers())
                if (m.id() == id)
                    markerStartBeat = m.beat();
            return;
        }
        case Zone::brace:
        {
            if (e.mods.isPopupMenu())
                return;
            const int edge = braceEdgeAt (e.x);
            braceStartAtPress = view.model.arrangementLoopStart();
            braceEndAtPress = view.model.arrangementLoopEnd();
            if (edge == 0)
                break; // outside the brace: acts like the bar ruler
            gesture = edge == 1 ? Gesture::braceStart : (edge == 2 ? Gesture::braceEnd : Gesture::braceMove);
            return;
        }
        case Zone::bars: break;
    }
    if (e.mods.isPopupMenu())
        return;
    gesture = Gesture::seek;
    anchorBeat = view.snapBeat (pressBeat, e.mods.isCommandDown());
}

void ArrangementRuler::mouseDrag (const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() < 3 && ! moved)
        return;
    const bool bypass = e.mods.isCommandDown();
    const double beat = view.xToBeat (e.x);
    switch (gesture)
    {
        case Gesture::marker:
        {
            if (! moved)
                view.model.undo.beginNewTransaction (Strings::undoArrMarkerMove);
            moved = true;
            view.moveMarkerTo (pressedMarker, view.snapBeat (beat, bypass));
            break;
        }
        case Gesture::braceStart:
        case Gesture::braceEnd:
        case Gesture::braceMove:
        {
            const bool first = ! moved;
            moved = true;
            const bool on = view.model.arrangementLoopOn();
            if (gesture == Gesture::braceStart)
                view.setLoopBrace (juce::jmin (view.snapBeat (beat, bypass), braceEndAtPress - ProjectModel::kMinArrangementLoopBeats), braceEndAtPress, on, first);
            else if (gesture == Gesture::braceEnd)
                view.setLoopBrace (braceStartAtPress, juce::jmax (view.snapBeat (beat, bypass), braceStartAtPress + ProjectModel::kMinArrangementLoopBeats), on, first);
            else
            {
                const double shift = juce::jmax (-braceStartAtPress, view.snapBeat (braceStartAtPress + beat - pressBeat, bypass) - braceStartAtPress);
                view.setLoopBrace (braceStartAtPress + shift, braceEndAtPress + shift, on, first);
            }
            break;
        }
        case Gesture::seek:
        {
            moved = true;
            std::vector<int64_t> all;
            for (const auto& r : view.layoutRows)
                all.push_back (r.trackId);
            if (! all.empty())
                view.setTimeSelection (anchorBeat, view.snapBeat (beat, bypass), all);
            break;
        }
        case Gesture::none: break;
    }
}

void ArrangementRuler::mouseUp (const juce::MouseEvent& e)
{
    const auto finished = gesture;
    gesture = Gesture::none;
    if (e.mods.isPopupMenu())
        return;
    switch (finished)
    {
        case Gesture::marker:
            if (! moved) // a click jumps to the marker: quantised while playing, at once with Shift
            {
                view.setCursor (markerStartBeat, e.mods.isShiftDown());
                view.scrollIntoView (markerStartBeat);
            }
            break;
        case Gesture::braceStart:
        case Gesture::braceEnd:
        case Gesture::braceMove:
            if (! moved) // a click on the brace switches the loop
            {
                view.model.undo.beginNewTransaction (Strings::undoArrLoop);
                view.model.setArrangementLoopOn (! view.model.arrangementLoopOn());
                repaint();
                view.lanesPart->repaint();
            }
            break;
        case Gesture::seek:
            if (! moved)
                view.setCursor (anchorBeat, e.mods.isShiftDown());
            else if (! view.timeSel.isEmpty())
                view.placeCursor (view.timeSel.start);
            break;
        case Gesture::none: break;
    }
}

void ArrangementRuler::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (zoneAt (e.y) != Zone::markers || e.mods.isPopupMenu())
        return;
    if (const auto id = markerAt (e.getPosition()); id >= 0)
        beginRenameMarker (id);
}

void ArrangementRuler::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown())
    {
        view.zoomBy (w.deltaY > 0 ? 1.15 : 1.0 / 1.15, (double) e.x);
        return;
    }
    const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
    view.setScrollBeats (view.v.scrollBeats - (double) d * 400.0 / view.v.pxPerBeat);
}

void ArrangementRuler::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    view.zoomBy ((double) scaleFactor, (double) e.x);
}

juce::String ArrangementRuler::helpAt (juce::Point<int> p) const
{
    switch (zoneAt (p.y))
    {
        case Zone::markers: return markerAt (p) >= 0 ? Strings::help::arrMarker : Strings::help::arrMarkerLane;
        case Zone::brace: return Strings::help::arrBrace;
        case Zone::bars: return Strings::help::arrRuler;
    }
    return {};
}

//==============================================================================
void ArrangementRuler::beginRenameMarker (int64_t markerId)
{
    const auto bounds = markerBounds (markerId);
    if (bounds.isEmpty())
        return;
    juce::String current;
    for (const auto& m : view.model.markers())
        if (m.id() == markerId)
            current = m.name();
    renameEditor = std::make_unique<InlineEditor>();
    renameEditor->setText (current, false);
    renameEditor->onCommit = [this, markerId] (const juce::String& text) { finishRename (text, markerId); };
    renameEditor->onCancel = [this, markerId] { finishRename ({}, markerId); };
    addAndMakeVisible (*renameEditor);
    renameEditor->setBounds (bounds.withWidth (juce::jmax (110, bounds.getWidth())).withHeight (ArrangementView::kMarkerLaneHeight - 2));
    renameEditor->grabKeyboardFocus();
    renameEditor->selectAll();
}

void ArrangementRuler::finishRename (const juce::String& text, int64_t markerId)
{
    auto* dying = renameEditor.release();
    removeChildComponent (dying);
    juce::MessageManager::callAsync ([dying] { delete dying; });
    if (text.trim().isNotEmpty())
        for (const auto& m : view.model.markers())
            if (m.id() == markerId && m.name() != text.trim())
            {
                view.model.undo.beginNewTransaction (Strings::undoArrMarkerRename);
                arrangement::renameMarker (view.model, m, text.trim());
                break;
            }
    view.takeFocus();
    repaint();
}

void ArrangementRuler::showMarkerMenu (juce::Point<int> screenPosition, int64_t markerId)
{
    namespace A = Strings::arrangement;
    juce::PopupMenu menu;
    const juce::Component::SafePointer<ArrangementRuler> safe (this);
    MarkerRef marker;
    for (const auto& m : view.model.markers())
        if (m.id() == markerId)
            marker = m;
    if (! marker.isValid())
        return;
    const double beat = marker.beat();
    menu.addItem (A::menuJumpToMarker, [safe, beat] {
        if (safe != nullptr)
            safe->view.setCursor (beat);
    });
    menu.addItem (A::menuRenameMarker, [safe, markerId] {
        if (safe != nullptr)
            safe->beginRenameMarker (markerId);
    });
    menu.addSubMenu (Strings::menu::colour, makePaletteMenu (marker.colour(), [safe, markerId] (int index) {
        if (safe == nullptr)
            return;
        for (const auto& m : safe->view.model.markers())
            if (m.id() == markerId)
            {
                safe->view.model.undo.beginNewTransaction (Strings::undoArrMarkerColour);
                arrangement::setMarkerColour (safe->view.model, m, index);
                break;
            }
    }));
    menu.addSeparator();
    menu.addItem (Strings::menu::remove, [safe, markerId] {
        if (safe == nullptr)
            return;
        safe->view.selectMarker (markerId);
        safe->view.deleteSelection();
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (screenPosition.x, screenPosition.y, 1, 1)));
}
} // namespace ui
