#include "ArrangementPlayhead.h"

#include "../engine/AudioEngine.h"

#include <cmath>

namespace ui
{
namespace
{
// The six stripes follow the theme's own spectrum (palette entries spread over the hue list).
constexpr int kSpectrum[6] = { 0, 3, 6, 9, 12, 15 };
constexpr float kStripeStep = 1.6f, kStripeWidth = 1.4f;
// The beam starts at the bar ruler: the marker lane and the loop brace above stay readable.
constexpr float kBeamTop = (float) (ArrangementView::kMarkerLaneHeight + ArrangementView::kBraceHeight);
} // namespace

ArrangementPlayhead::ArrangementPlayhead (ArrangementView& v) : view (v)
{
    setInterceptsMouseClicks (false, false);
    setOpaque (false);
}

ArrangementPlayhead::~ArrangementPlayhead()
{
    stopTimer();
}

void ArrangementPlayhead::visibilityChanged()
{
    if (isVisible())
        startTimerHz (60);
    else
        stopTimer();
}

float ArrangementPlayhead::beamX() const
{
    return (float) view.beatToX (view.engine.getSongPositionBeats());
}

juce::Rectangle<int> ArrangementPlayhead::stripAround (float x, int height)
{
    return { juce::roundToInt (x) - 10, 0, 28, height };
}

void ArrangementPlayhead::timerCallback()
{
    if (! isShowing())
        return;
    const float x = beamX();
    if (std::abs (x - lastX) < 0.25f && lastRows == (int) view.layoutRows.size())
        return;
    repaint (stripAround (lastX, getHeight()));
    repaint (stripAround (x, getHeight()));
    lastX = x;
    lastRows = (int) view.layoutRows.size();
}

void ArrangementPlayhead::paint (juce::Graphics& g)
{
    const auto& t = theme();
    const float x = beamX();
    if (x < -12.0f || x > (float) getWidth() + 12.0f)
        return;
    lastX = x;
    const double beat = view.engine.getSongPositionBeats();

    // where the beam crosses a clip it splits into the spectrum, like light through the prism of the logo
    for (const auto& row : view.layoutRows)
    {
        const auto track = view.model.findTrack (row.trackId);
        if (! arrangement::clipAt (track, beat).isValid())
            continue;
        const float top = (float) (ArrangementView::kRulerHeight + row.y - view.v.scrollY) + 3.0f;
        const float h = (float) row.height - 6.0f;
        if (top + h < (float) ArrangementView::kRulerHeight || top > (float) getHeight())
            continue;
        for (int k = 0; k < 6; ++k)
        {
            g.setColour (t.palette (kSpectrum[k]).withAlpha (t.glow > 0.0f ? 0.6f : 0.45f));
            g.fillRect (x + 1.0f + (float) k * kStripeStep, top, kStripeWidth, h);
        }
    }

    if (t.glow > 0.0f)
    {
        g.setColour (t.accent.withAlpha (0.07f * juce::jmin (t.glow, 2.0f)));
        g.fillRect (x - 6.0f, kBeamTop, 12.0f, (float) getHeight() - kBeamTop);
        g.setColour (t.accent.withAlpha (0.14f * juce::jmin (t.glow, 2.0f)));
        g.fillRect (x - 3.0f, kBeamTop, 6.0f, (float) getHeight() - kBeamTop);
    }
    g.setColour (t.text);
    g.fillRect (x - 0.5f, kBeamTop, 1.5f, (float) getHeight() - kBeamTop);
}
} // namespace ui
