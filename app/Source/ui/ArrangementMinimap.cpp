#include "ArrangementMinimap.h"

#include "../Strings.h"
#include "../engine/AudioEngine.h"

#include <cmath>

namespace ui
{
namespace
{
constexpr float kEdgeGrab = 6.0f;
}

ArrangementMinimap::ArrangementMinimap (ArrangementView& v) : view (v)
{
    setOpaque (true);
}

double ArrangementMinimap::beatToX (double beat) const
{
    return kPad + beat / view.totalBeats() * (double) (getWidth() - 2 * kPad);
}

double ArrangementMinimap::xToBeat (double x) const
{
    return (x - kPad) / (double) juce::jmax (1, getWidth() - 2 * kPad) * view.totalBeats();
}

juce::Rectangle<float> ArrangementMinimap::frameBounds() const
{
    const float x1 = (float) beatToX (view.v.scrollBeats);
    const float x2 = (float) beatToX (view.v.scrollBeats + view.visibleBeats());
    return { x1, (float) kTop - 2.0f, juce::jmax (6.0f, x2 - x1), (float) getHeight() - (float) kTop - (float) kBottom + 4.0f };
}

//==============================================================================
void ArrangementMinimap::paint (juce::Graphics& g)
{
    const auto& t = theme();
    g.fillAll (t.chrome);
    g.setColour (t.line);
    g.fillRect (0, 0, getWidth(), 1);
    const bool soft = t.glass; // glass: pastel clip rows, lighter bar lines

    const double total = view.totalBeats(), bar = view.beatsPerBar();
    const auto area = juce::Rectangle<float> ((float) kPad, (float) kTop, (float) (getWidth() - 2 * kPad), (float) (getHeight() - kTop - kBottom));
    g.setColour (t.panel);
    g.fillRoundedRectangle (area, 2.0f);

    // bar numbers, every n-th bar so that they do not touch
    int stride = 1;
    while (stride < 4096 && bar * stride / total * area.getWidth() < 34.0)
        stride *= 2;
    g.setFont (t.numFont (9.5f));
    for (long long b = 0; (double) b * bar < total; b += stride)
    {
        const float x = (float) beatToX ((double) b * bar);
        g.setColour (soft ? t.line.withAlpha (0.5f) : t.line);
        g.fillRect (x, area.getY(), 1.0f, area.getHeight());
        g.setColour (t.dimText);
        g.drawText (juce::String (b + 1), juce::Rectangle<int> (juce::roundToInt (x) + 2, 0, 40, kTop), juce::Justification::centredLeft, false);
    }

    // the loop
    {
        const float x1 = (float) beatToX (view.model.arrangementLoopStart()), x2 = (float) beatToX (view.model.arrangementLoopEnd());
        g.setColour (t.accent.withAlpha (view.model.arrangementLoopOn() ? 0.18f : 0.07f));
        g.fillRect (x1, area.getY(), x2 - x1, area.getHeight());
    }

    // clips, one thin row per track
    const auto tracks = view.model.tracks();
    if (! tracks.isEmpty())
    {
        const float rowH = area.getHeight() / (float) tracks.size();
        for (int i = 0; i < tracks.size(); ++i)
        {
            const float y = area.getY() + rowH * (float) i;
            for (const auto& c : tracks[i].arrangementClips())
            {
                const float x1 = (float) beatToX (c.start()), x2 = (float) beatToX (c.end());
                g.setColour (t.palette (c.clip().isValid() ? c.clip().colour() : tracks[i].colour()).withAlpha (soft ? 0.55f : 0.9f));
                g.fillRect (x1, y + 0.5f, juce::jmax (1.0f, x2 - x1 - 0.5f), juce::jmax (1.5f, rowH - 1.0f));
            }
        }
    }

    for (const auto& m : view.model.markers())
    {
        g.setColour (m.colour() >= 0 ? t.palette (m.colour()) : t.accent2);
        g.fillRect ((float) beatToX (m.beat()), area.getY(), 1.5f, area.getHeight());
    }

    // the playhead
    g.setColour (t.text);
    g.fillRect ((float) beatToX (view.engine.getSongPositionBeats()), area.getY(), 1.0f, area.getHeight());

    // the frame of the lanes
    const auto frame = frameBounds();
    if (t.glass)
    {
        // glass: a pane of clear glass over the overview, its edge quietly in the accent
        glass::pane (g, frame.toFloat().reduced (0.5f), 5.0f, t.accentFill, 0.45f, 0.55f);
        return;
    }
    g.setColour (t.text.withAlpha (0.09f));
    g.fillRect (frame);
    g.setColour (t.text.withAlpha (0.75f));
    g.drawRect (frame, 1.0f);
    g.setColour (t.text);
    g.fillRect (frame.withWidth (3.0f));
    g.fillRect (frame.withLeft (frame.getRight() - 3.0f));
}

void ArrangementMinimap::mouseMove (const juce::MouseEvent& e)
{
    const auto frame = frameBounds();
    const bool edge = std::abs ((float) e.x - frame.getX()) <= kEdgeGrab || std::abs ((float) e.x - frame.getRight()) <= kEdgeGrab;
    setMouseCursor (edge ? juce::MouseCursor::LeftRightResizeCursor : (frame.contains (e.position) ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor));
}

void ArrangementMinimap::mouseDown (const juce::MouseEvent& e)
{
    view.takeFocus();
    const auto frame = frameBounds();
    if (std::abs ((float) e.x - frame.getX()) <= kEdgeGrab)
    {
        gesture = Gesture::zoomLeft;
        view.autoZoom = false;
        fixedBeat = view.v.scrollBeats + view.visibleBeats();
        view.remember();
    }
    else if (std::abs ((float) e.x - frame.getRight()) <= kEdgeGrab)
    {
        gesture = Gesture::zoomRight;
        view.autoZoom = false;
        fixedBeat = view.v.scrollBeats;
        view.remember();
    }
    else
    {
        gesture = Gesture::scroll;
        if (! frame.contains (e.position)) // a click outside the frame centres it there
            view.setScrollBeats (xToBeat (e.x) - view.visibleBeats() * 0.5);
        grabOffset = xToBeat (e.x) - view.v.scrollBeats;
    }
}

void ArrangementMinimap::mouseDrag (const juce::MouseEvent& e)
{
    switch (gesture)
    {
        case Gesture::scroll: view.setScrollBeats (xToBeat (e.x) - grabOffset); break;
        case Gesture::zoomLeft:
        {
            const double left = juce::jmin (xToBeat (e.x), fixedBeat - 0.25);
            view.v.pxPerBeat = (double) view.lanesWidth() / (fixedBeat - left);
            view.v.scrollBeats = left; // clamped after the zoom limit is applied
            view.setScrollBeats (juce::jmax (0.0, fixedBeat - view.visibleBeats()));
            break;
        }
        case Gesture::zoomRight:
        {
            const double right = juce::jmax (xToBeat (e.x), fixedBeat + 0.25);
            view.v.pxPerBeat = (double) view.lanesWidth() / (right - fixedBeat);
            view.setScrollBeats (fixedBeat);
            break;
        }
        case Gesture::none: break;
    }
}

void ArrangementMinimap::mouseUp (const juce::MouseEvent&)
{
    gesture = Gesture::none;
}

juce::String ArrangementMinimap::helpAt (juce::Point<int>) const
{
    return Strings::help::arrMinimap;
}
} // namespace ui
