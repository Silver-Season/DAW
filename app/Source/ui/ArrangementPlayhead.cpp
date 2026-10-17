#include "ArrangementPlayhead.h"

#include "../Strings.h"
#include "../engine/AudioEngine.h"
#include "../engine/SessionRecorder.h"

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
    // The clips that grow while a recording runs are drawn by paint(); they change with every poll of the recorder.
    if (auto* recorder = view.getRecorder(); recorder != nullptr && recorder->arrangement().isActive() && ++recordingTick % 2 == 0)
        repaint();
    const float x = beamX();
    if (std::abs (x - lastX) < 0.25f && lastRows == (int) view.layoutRows.size())
        return;
    repaint (stripAround (lastX, getHeight()));
    repaint (stripAround (x, getHeight()));
    lastX = x;
    lastRows = (int) view.layoutRows.size();
}

// The clip that is being recorded on each track: from where the recording began to where it is now (with a loop, over the
// whole loop, showing the pass that runs), in the colour of recording, with the notes played so far.
void ArrangementPlayhead::paintRecording (juce::Graphics& g) const
{
    auto* recorder = view.getRecorder();
    if (recorder == nullptr || ! recorder->arrangement().isActive())
        return;
    const auto& t = theme();
    for (const auto& row : view.layoutRows)
    {
        ArrangementRecorder::Progress p;
        if (! recorder->arrangement().getProgress (row.trackId, p) || p.waiting || p.endBeat - p.startBeat < 1.0e-3)
            continue;
        const float top = (float) (ArrangementView::kRulerHeight + row.y - view.v.scrollY) + 3.0f;
        const float h = (float) row.height - 6.0f;
        if (top + h < (float) ArrangementView::kRulerHeight || top > (float) getHeight())
            continue;
        const float x0 = (float) view.beatToX (p.startBeat), x1 = (float) view.beatToX (p.endBeat);
        if (x1 < 0.0f || x0 > (float) getWidth())
            continue;
        const juce::Rectangle<float> r (x0, top, juce::jmax (2.0f, x1 - x0), h);
        g.setColour (t.record.withAlpha (0.2f));
        g.fillRoundedRectangle (r, juce::jmin (t.radius, 4.0f));
        g.setColour (t.record.withAlpha (0.9f));
        g.drawRoundedRectangle (r.reduced (0.75f), juce::jmin (t.radius, 4.0f), 1.5f);

        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (r.toNearestInt());
        const bool nameRow = r.getHeight() >= 34.0f;
        if (! p.notes.empty() && r.getWidth() > 3.0f)
        {
            int lowest = 127, highest = 0;
            for (const auto& n : p.notes)
            {
                lowest = std::min (lowest, n.pitch);
                highest = std::max (highest, n.pitch);
            }
            const auto area = r.reduced (2.0f, 2.0f).withTrimmedTop (nameRow ? 15.0f : 1.0f);
            const int span = std::max (highest - lowest, 7) + 1;
            const float rowH = area.getHeight() / (float) span;
            const float noteH = juce::jlimit (1.5f, 4.0f, rowH);
            g.setColour (t.text.withAlpha (0.75f));
            for (const auto& n : p.notes)
            {
                const float nx = (float) view.beatToX (p.startBeat + n.start);
                const float w = juce::jmax (1.5f, (float) (n.length * view.v.pxPerBeat));
                const float y = area.getBottom() - ((float) (n.pitch - lowest) + 0.5f) * rowH - noteH * 0.5f;
                g.fillRect (nx, y, w, noteH);
            }
        }
        if (nameRow && r.getWidth() > 40.0f)
        {
            g.setColour (t.text);
            g.setFont (t.uiFont (11.0f, 500));
            auto label = Strings::arrRec::clipName;
            if (p.numPasses > 1)
                label += " · " + Strings::arrRec::take (p.pass);
            g.drawText (label, r.toNearestInt().withTrimmedLeft (6).withHeight (15), juce::Justification::centredLeft, true);
        }
    }
}

void ArrangementPlayhead::paint (juce::Graphics& g)
{
    const auto& t = theme();
    paintRecording (g);
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
