#include "ArrangementLanes.h"

#include "../Strings.h"
#include "../engine/AudioEngine.h"
#include "../model/Automation.h"
#include "PaletteMenu.h"
#include "Waveform.h"

#include "daw/engine/Arrangement.h"

#include <algorithm>
#include <cmath>

namespace ui
{
namespace
{
constexpr double kEpsilon = 1.0e-6;
constexpr float kLanePad = 3.0f;
constexpr float kEnvelopeMargin = 7.0f, kLineGrab = 6.0f, kDotGrab = 7.0f, kDotRadius = 4.0f;

juce::String shortcutText (juce::juce_wchar key, int modifiers = juce::ModifierKeys::commandModifier)
{
    return juce::KeyPress (key, modifiers, 0).getTextDescriptionWithIcons();
}

// A loop icon: a ring with an arrowhead, small enough for a clip corner.
juce::Path loopMark (juce::Rectangle<float> a)
{
    return icons::loop (a);
}
} // namespace

ArrangementLanes::ArrangementLanes (ArrangementView& v) : view (v)
{
    setOpaque (true);
    setBufferedToImage (true); // the playhead repaints a thin strip over this; the clips are drawn only when they changed
}

//==============================================================================
juce::Rectangle<float> ArrangementLanes::rectFor (int row, double start, double length) const
{
    const auto& r = view.layoutRows[(size_t) row];
    const float x = (float) view.beatToX (start) + 0.5f;
    const float w = juce::jmax (1.0f, (float) (length * view.v.pxPerBeat) - 1.5f);
    return { x, (float) (r.y - view.v.scrollY) + kLanePad, w, (float) r.height - 2.0f * kLanePad };
}

juce::Rectangle<int> ArrangementLanes::clipBounds (int64_t aclipId) const
{
    const auto clip = arrangement::findClip (view.model, aclipId);
    if (! clip.isValid())
        return {};
    const int row = view.rowIndexOfTrack (arrangement::trackOf (view.model, clip).id());
    return row < 0 ? juce::Rectangle<int>() : rectFor (row, clip.start(), clip.length()).toNearestInt();
}

ArrangementLanes::Hit ArrangementLanes::clipHit (juce::Point<int> p) const
{
    Hit hit;
    hit.row = view.rowAtY (p.y);
    if (hit.row < 0)
        return hit;
    const auto track = view.model.findTrack (view.layoutRows[(size_t) hit.row].trackId);
    for (const auto& c : track.arrangementClips())
    {
        const auto r = rectFor (hit.row, c.start(), c.length());
        if (p.x < r.getX() - 1.0f || p.x > r.getRight() + 1.0f)
            continue;
        hit.clip = c;
        const float edge = juce::jmin (6.0f, r.getWidth() / 3.0f);
        hit.zone = p.x - r.getX() <= edge ? Zone::leftEdge : (r.getRight() - p.x <= edge ? Zone::rightEdge : Zone::body);
        break;
    }
    return hit;
}

//==============================================================================
void ArrangementLanes::paintGrid (juce::Graphics& g, juce::Rectangle<int> area, juce::Colour colour, float strength) const
{
    const double px = view.v.pxPerBeat, bar = view.beatsPerBar();
    const double from = view.xToBeat ((double) area.getX()), to = view.xToBeat ((double) area.getRight());
    const double step = view.gridStep();
    const int barStride = view.barLineStride();
    const bool subdivisions = step * px >= 4.0;

    const double first = std::floor (from / step) * step;
    for (double beat = first; beat <= to + step; beat += step)
    {
        const double barPos = beat / bar;
        const bool onBar = std::abs (barPos - std::round (barPos)) < 1.0e-6;
        const bool onBeat = std::abs (beat - std::round (beat)) < 1.0e-6;
        float alpha;
        if (onBar)
        {
            if (((long long) std::llround (barPos)) % barStride != 0)
                continue;
            alpha = 0.95f;
        }
        else if (! subdivisions)
            continue;
        else
            alpha = onBeat ? 0.5f : 0.22f;
        const int x = juce::roundToInt (view.beatToX (beat));
        g.setColour (colour.withAlpha (alpha * strength));
        g.fillRect (x, area.getY(), 1, area.getHeight());
    }
}

void ArrangementLanes::paintNotes (juce::Graphics& g, const ArrangementClipRef& c, juce::Rectangle<float> r, double clipStart, double clipLength,
                                   double clipOffset, juce::Colour ink) const
{
    if (! c.clip().isValid() || r.getWidth() < 3.0f)
        return;
    const auto flat = daw::engine::flattenArrangementClip (c.clip().toMidiClip(), clipOffset, c.loop(), clipLength);
    if (flat.notes.empty())
        return;
    int lowest = 127, highest = 0;
    for (const auto& n : flat.notes)
    {
        lowest = std::min (lowest, n.pitch);
        highest = std::max (highest, n.pitch);
    }
    const bool nameRow = r.getHeight() >= 34.0f;
    const auto area = r.reduced (2.0f, 2.0f).withTrimmedTop (nameRow ? 15.0f : 1.0f);
    const int span = std::max (highest - lowest, 7) + 1;
    const float rowH = area.getHeight() / (float) span;
    const float noteH = juce::jlimit (1.5f, 4.0f, rowH);
    const bool narrow = r.getWidth() < 40.0f;
    const auto clipBox = g.getClipBounds().toFloat();
    g.setColour (ink.withAlpha (nameRow ? 0.6f : 0.45f));
    const double px = view.v.pxPerBeat;
    int drawn = 0;
    (void) clipStart;
    for (const auto& n : flat.notes)
    {
        const float x = r.getX() + 1.0f + (float) (n.start * px);
        if (x > r.getRight() - 1.0f || x > clipBox.getRight())
            break; // notes are sorted by start
        const float w = juce::jmax (1.5f, (float) (n.length * px));
        if (x + w < clipBox.getX())
            continue;
        const float xr = juce::jmin (x + w, r.getRight() - 1.0f);
        const float y = area.getBottom() - ((float) (n.pitch - lowest) + 0.5f) * rowH - noteH * 0.5f;
        if (narrow) // stripes: a clip this narrow only says "something plays here"
            g.fillRect (x, area.getY(), 1.0f, area.getHeight());
        else
            g.fillRect (x, y, xr - x, noteH);
        if (++drawn > 6000)
            break;
    }
}

bool ArrangementLanes::paintAudio (juce::Graphics& g, const ArrangementClipRef& c, juce::Rectangle<float> r, double clipLength, double clipOffset,
                                   juce::Colour ink) const
{
    const auto clip = c.clip();
    const auto source = view.engine.peekAudioSource (clip.fileId());
    if (source == nullptr)
        return false;
    if (r.getWidth() < 3.0f)
        return true;
    const double px = view.v.pxPerBeat;
    const bool nameRow = r.getHeight() >= 34.0f;
    const float top = r.getY() + (nameRow ? 15.0f : 2.0f), bottom = r.getBottom() - 2.0f;
    const juce::Rectangle<float> area (r.getX(), top, (float) (clipLength * px), bottom - top);
    const double entry = daw::engine::normaliseOffset (clip.toMidiClip(), clipOffset, c.loop());
    drawAudioWaveform (g, area, *source, clip, entry, c.loop(), clipLength, clip.contentTempo (view.model.tempo()), ink.withAlpha (nameRow ? 0.75f : 0.6f));

    // The fades of the clip: the part of the waveform they take away is darkened and the ramp is drawn as a line.
    const auto& t = theme();
    const float maxFade = r.getWidth() * 0.5f;
    const float fadeIn = juce::jmin (maxFade, (float) (clip.fadeIn() * px)), fadeOut = juce::jmin (maxFade, (float) (clip.fadeOut() * px));
    const float x0 = r.getX(), x1 = r.getRight();
    if (fadeIn >= 1.0f)
    {
        juce::Path ramp;
        ramp.addTriangle (x0, r.getY(), x0 + fadeIn, r.getY(), x0, r.getBottom());
        g.setColour (t.background.withAlpha (0.4f));
        g.fillPath (ramp);
        g.setColour (ink.withAlpha (0.8f));
        g.drawLine (x0, r.getBottom(), x0 + fadeIn, r.getY(), 1.0f);
    }
    if (fadeOut >= 1.0f)
    {
        juce::Path ramp;
        ramp.addTriangle (x1, r.getY(), x1 - fadeOut, r.getY(), x1, r.getBottom());
        g.setColour (t.background.withAlpha (0.4f));
        g.fillPath (ramp);
        g.setColour (ink.withAlpha (0.8f));
        g.drawLine (x1 - fadeOut, r.getY(), x1, r.getBottom(), 1.0f);
    }
    return true;
}

void ArrangementLanes::paintClip (juce::Graphics& g, const ArrangementClipRef& c, juce::Rectangle<float> r, double clipStart, double clipLength,
                                  double clipOffset, float alpha, bool selected, bool inSession, bool muted) const
{
    const auto& t = theme();
    const float radius = juce::jmin (t.radius, 4.0f);
    auto base = t.palette (c.clip().isValid() ? c.clip().colour() : 0);
    const bool audio = c.clip().isValid() && c.clip().isAudio();
    bool missing = false;
    if (audio && view.engine.peekAudioSource (c.clip().fileId()) == nullptr)
    {
        missing = true;
        base = base.withMultipliedSaturation (0.1f).withMultipliedBrightness (0.7f);
    }
    if (inSession)
        base = base.withMultipliedSaturation (0.15f).withMultipliedBrightness (0.8f);
    const float a = alpha * (muted && ! inSession ? 0.55f : 1.0f);
    g.setColour (base.withAlpha (a));
    g.fillRoundedRectangle (r, radius);

    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (r.toNearestInt());
        // the grid goes on under the notes and the waveform, so they can be lined up by eye
        paintGrid (g, r.toNearestInt(), t.clipInk, 0.22f * a);
        if (audio)
            paintAudio (g, c, r, clipLength, clipOffset, t.clipInk);
        else
            paintNotes (g, c, r, clipStart, clipLength, clipOffset, t.clipInk);

        if (r.getWidth() > 26.0f)
        {
            g.setColour (t.clipInk.withAlpha (a));
            g.setFont (t.uiFont (11.0f, 500));
            const bool looping = c.loop() && c.clip().isValid() && clipLength > c.clip().lengthBeats() - c.clip().loopStart() + kEpsilon;
            auto text = r.toNearestInt().withTrimmedLeft (6).withHeight (15).withTrimmedRight (looping ? 16 : 4);
            auto label = c.clip().isValid() ? c.clip().name() : juce::String();
            if (const auto takes = arrangement::takesOf (c); takes.size() > 1 && r.getWidth() > 90.0f)
            {
                int number = 0;
                for (const auto& take : takes)
                    number = take.active ? take.number : number;
                label += "  " + Strings::arrRec::takeBadge (number, (int) takes.size());
            }
            g.drawText (label, text, juce::Justification::centredLeft, true);
            if (missing)
            {
                g.setFont (t.uiFont (11.0f));
                g.setColour (t.clipInk.withAlpha (0.8f * a));
                g.drawText (Strings::audioUi::fileMissing, r.toNearestInt().withTrimmedLeft (6).withTrimmedTop (15).withTrimmedRight (4),
                            juce::Justification::centredLeft, true);
            }
            if (looping)
            {
                g.setColour (t.clipInk.withAlpha (0.8f * a));
                g.fillPath (loopMark (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ r.getRight() - 9.0f, r.getY() + 8.0f })));
            }
        }
    }
    if (selected)
        strokeCell (g, r, t.text, 1.5f, 0.6f);
}

void ArrangementLanes::paint (juce::Graphics& g)
{
    const auto& t = theme();
    const auto bounds = g.getClipBounds();
    g.fillAll (t.background);

    // lanes
    for (size_t i = 0; i < view.layoutRows.size(); ++i)
    {
        const auto& row = view.layoutRows[i];
        const int y = row.y - view.v.scrollY;
        if (y + row.height < bounds.getY() || y > bounds.getBottom())
            continue;
        g.setColour (i % 2 == 0 ? t.panel : t.panel.interpolatedWith (t.background, 0.45f));
        g.fillRect (0, y, getWidth(), row.height - 1);
        g.setColour (t.line);
        g.fillRect (0, y + row.height - 1, getWidth(), 1);
    }
    paintGrid (g, bounds, t.line);

    // under the last track: the place for a new one (drops from the browser or Finder land here; the headers have the buttons)
    if (! view.drop.valid)
    {
        const int top = view.contentHeight() - view.v.scrollY;
        const juce::Rectangle<float> area (4.0f, (float) top + 8.0f, (float) getWidth() - 8.0f, (float) ArrangementView::kAddTrackArea - 16.0f);
        if (area.getY() < (float) getHeight())
        {
            juce::Path outline;
            outline.addRoundedRectangle (area, t.radius);
            juce::Path dashed;
            const float dashes[] = { 5.0f, 4.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (t.line);
            g.fillPath (dashed);
            g.setColour (t.dimText.withAlpha (0.7f));
            g.setFont (t.uiFont (12.0f));
            g.drawText (Strings::arrangement::dropForNewTrack, area.toNearestInt(), juce::Justification::centred, true);
        }
    }

    // the loop of the arrangement tints the time under it
    {
        const auto& m = view.model;
        const float x1 = (float) view.beatToX (m.arrangementLoopStart()), x2 = (float) view.beatToX (m.arrangementLoopEnd());
        if (x2 >= (float) bounds.getX() && x1 <= (float) bounds.getRight())
        {
            const bool on = m.arrangementLoopOn();
            g.setColour (t.accent.withAlpha (on ? 0.07f : 0.025f));
            g.fillRect (x1, 0.0f, x2 - x1, (float) getHeight());
            g.setColour (t.accent.withAlpha (on ? 0.5f : 0.18f));
            g.fillRect (x1, 0.0f, 1.0f, (float) getHeight());
            g.fillRect (x2 - 1.0f, 0.0f, 1.0f, (float) getHeight());
        }
    }

    // time selection
    if (! view.timeSel.isEmpty())
    {
        const float x1 = (float) view.beatToX (view.timeSel.start), x2 = (float) view.beatToX (view.timeSel.end);
        for (const auto id : view.timeSel.tracks)
        {
            const int row = view.rowIndexOfTrack (id);
            if (row < 0)
                continue;
            const auto& lane = view.layoutRows[(size_t) row];
            const juce::Rectangle<float> sel (x1, (float) (lane.y - view.v.scrollY), x2 - x1, (float) lane.height - 1.0f);
            g.setColour (t.accent.withAlpha (0.16f));
            g.fillRect (sel);
            g.setColour (t.accent.withAlpha (0.55f));
            g.fillRect (sel.withWidth (1.0f));
            g.fillRect (sel.withLeft (sel.getRight() - 1.0f));
        }
    }

    // clips
    const bool previewMove = gesture == Gesture::move && movedEnough;
    for (size_t i = 0; i < view.layoutRows.size(); ++i)
    {
        const auto& lane = view.layoutRows[i];
        const int y = lane.y - view.v.scrollY;
        if (y + lane.height < bounds.getY() || y > bounds.getBottom())
            continue;
        const auto track = view.model.findTrack (lane.trackId);
        auto* rt = view.engine.findRuntime (lane.trackId);
        const bool inSession = rt != nullptr && rt->inSession.load();
        for (const auto& c : track.arrangementClips())
        {
            double start = c.start(), length = c.length(), offset = c.offset();
            // a stretch shows the new edges while the model still has the old ones
            if (gesture == Gesture::resizeRight && c.id() == stretchClip)
                length = juce::jmax (arrangement::kMinClipBeats, stretchValue - start);
            else if (gesture == Gesture::resizeLeft && c.id() == stretchClip)
            {
                const double end = c.end();
                offset += stretchValue - start;
                start = stretchValue;
                length = end - start;
            }
            const auto r = rectFor ((int) i, start, length);
            if (r.getRight() < (float) bounds.getX() || r.getX() > (float) bounds.getRight())
                continue;
            const bool isDragged = previewMove && std::any_of (dragged.begin(), dragged.end(), [&] (const auto& d) { return d.clipId == c.id(); });
            const float alpha = (isDragged && ! copying ? 0.3f : 1.0f) * (view.automationMode() ? 0.4f : 1.0f);
            paintClip (g, c, r, start, length, offset, alpha, view.isClipSelected (c.id()), inSession, track.mute());
        }
        if (view.automationMode())
            paintEnvelope (g, (int) i);
        if (inSession) // the grey bar over the lane: these clips do not sound until "Back to arrangement"
        {
            g.setColour (t.dimText.withAlpha (0.55f));
            g.fillRect (0, y, getWidth(), 3);
        }
    }

    // the clips being moved or copied, at their new place
    if (previewMove)
    {
        for (const auto& d : dragged)
        {
            const auto c = arrangement::findClip (view.model, d.clipId);
            const int row = view.rowIndexOfTrack (d.trackId) + deltaTracks;
            if (! c.isValid() || row < 0 || row >= (int) view.layoutRows.size())
                continue;
            const auto r = rectFor (row, juce::jmax (0.0, d.start + deltaBeats), c.length());
            paintClip (g, c, r, d.start + deltaBeats, c.length(), c.offset(), 0.78f, true, false, false);
        }
    }

    // a session clip on its way in
    if (view.drop.valid && view.drop.audio)
    {
        // sound files: the lane they will land on, or the empty space that makes new audio tracks
        if (view.drop.newTracks)
        {
            const int top = view.layoutRows.empty() ? 0 : view.layoutRows.back().y + view.layoutRows.back().height - view.v.scrollY;
            const juce::Rectangle<float> area (2.0f, (float) top + 2.0f, (float) getWidth() - 4.0f, juce::jmax (24.0f, (float) (getHeight() - top) - 4.0f));
            g.setColour (t.accent.withAlpha (0.10f));
            g.fillRoundedRectangle (area, t.radius);
            strokeCell (g, area, t.accent, 1.5f, 0.6f);
            g.setColour (t.accent);
            g.setFont (t.uiFont (12.0f, 500));
            g.drawText (Strings::audioUi::dropNewTracks, area.toNearestInt(), juce::Justification::centred, true);
        }
        else if (view.drop.row >= 0 && view.drop.row < (int) view.layoutRows.size())
        {
            const auto& lane = view.layoutRows[(size_t) view.drop.row];
            const juce::Rectangle<float> area (0.0f, (float) (lane.y - view.v.scrollY), (float) getWidth(), (float) lane.height - 1.0f);
            g.setColour (t.accent.withAlpha (0.12f));
            g.fillRect (area);
            g.setColour (t.accent);
            g.fillRect ((float) view.beatToX (view.drop.beat), area.getY(), 2.0f, area.getHeight());
        }
    }
    else if (view.drop.valid && view.drop.row >= 0 && view.drop.row < (int) view.layoutRows.size())
    {
        const auto r = rectFor (view.drop.row, view.drop.beat, juce::jmax (0.25, view.drop.length));
        g.setColour (t.accent.withAlpha (0.25f));
        g.fillRoundedRectangle (r, juce::jmin (t.radius, 4.0f));
        strokeCell (g, r, t.accent, 1.5f, 0.6f);
    }

    if (view.layoutRows.empty())
    {
        g.setColour (t.dimText);
        g.setFont (t.uiFont (13.0f));
        g.drawText (Strings::arrangement::noTracks, getLocalBounds(), juce::Justification::centred, true);
    }

    // the value under the pointer while a point is dragged
    if ((gesture == Gesture::autoMove || gesture == Gesture::autoBend) && readoutText.isNotEmpty())
    {
        const auto font = t.numFont (11.5f);
        g.setFont (font);
        const float w = juce::GlyphArrangement::getStringWidth (font, readoutText) + 14.0f;
        juce::Rectangle<float> box (readoutAt.x + 10.0f, readoutAt.y - 26.0f, w, 20.0f);
        if (box.getRight() > (float) getWidth())
            box.setX (readoutAt.x - 10.0f - w);
        if (box.getY() < 0.0f)
            box.setY (readoutAt.y + 10.0f);
        g.setColour (t.panel2);
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (t.line);
        g.drawRoundedRectangle (box, 4.0f, 1.0f);
        g.setColour (t.text);
        g.drawText (readoutText, box.toNearestInt(), juce::Justification::centred, false);
    }
}

//==============================================================================
// Automation
juce::Rectangle<float> ArrangementLanes::envelopeArea (int row) const
{
    const auto& r = view.layoutRows[(size_t) row];
    const float h = juce::jmax (8.0f, (float) r.height - 1.0f - 2.0f * kEnvelopeMargin);
    return { 0.0f, (float) (r.y - view.v.scrollY) + kEnvelopeMargin, (float) getWidth(), h };
}

float ArrangementLanes::valueForY (int row, float y) const
{
    const auto a = envelopeArea (row);
    return juce::jlimit (0.0f, 1.0f, 1.0f - (y - a.getY()) / a.getHeight());
}

ArrangementLanes::AutoHit ArrangementLanes::autoHitAt (juce::Point<int> p) const
{
    AutoHit hit;
    hit.row = view.rowAtY (p.y);
    if (hit.row < 0)
        return hit;
    const auto track = view.model.findTrackOrMaster (view.layoutRows[(size_t) hit.row].trackId);
    if (! track.isValid())
        return hit;
    const auto target = view.automationTarget (track.id());
    const auto lane = automation::findLane (track, target);
    const auto pts = lane.isValid() ? automation::points (lane) : std::vector<daw::engine::AutomationPoint>();
    const auto area = envelopeArea (hit.row);
    float best = kDotGrab;
    for (size_t i = 0; i < pts.size(); ++i)
    {
        const juce::Point<float> dot ((float) view.beatToX (pts[i].beat), area.getBottom() - pts[i].value * area.getHeight());
        const float d = dot.getDistanceFrom (p.toFloat());
        if (d <= best)
        {
            best = d;
            hit.point = (int) i;
        }
    }
    const float v = daw::engine::automationValueAt (pts, view.xToBeat ((double) p.x), view.automationCurrentValue (track, target));
    hit.onLine = std::abs ((float) p.y - (area.getBottom() - v * area.getHeight())) <= kLineGrab;
    return hit;
}

void ArrangementLanes::paintEnvelope (juce::Graphics& g, int row) const
{
    const auto& t = theme();
    const auto track = view.model.findTrackOrMaster (view.layoutRows[(size_t) row].trackId);
    if (! track.isValid())
        return;
    const auto target = view.automationTarget (track.id());
    const auto lane = automation::findLane (track, target);
    const auto pts = lane.isValid() ? automation::points (lane) : std::vector<daw::engine::AutomationPoint>();
    const float current = view.automationCurrentValue (track, target);
    const auto area = envelopeArea (row);
    // light themes have pastel track colours: the line is darkened there so that it reads over the pale clip
    auto colour = t.palette (track.colour());
    if (t.background.getPerceivedBrightness() > 0.5f)
        colour = colour.darker (0.6f);
    auto yOf = [&] (float v) { return area.getBottom() - v * area.getHeight(); };

    if (pts.empty())
    {
        // no envelope yet: the mixer's own value, dimmed
        g.setColour (t.background.withAlpha (0.6f));
        g.fillRect (0.0f, yOf (current) - 1.5f, (float) getWidth(), 3.0f);
        g.setColour (colour.withAlpha (0.5f));
        g.fillRect (0.0f, yOf (current) - 0.5f, (float) getWidth(), 1.0f);
        return;
    }

    const auto clipBox = g.getClipBounds();
    std::vector<float> xs;
    const float x0 = (float) clipBox.getX() - 2.0f, x1 = (float) clipBox.getRight() + 2.0f;
    for (float x = std::floor (x0 / 2.0f) * 2.0f; x <= x1; x += 2.0f)
        xs.push_back (x);
    for (const auto& p : pts)
        if (const float x = (float) view.beatToX (p.beat); x >= x0 && x <= x1)
            xs.push_back (x);
    std::sort (xs.begin(), xs.end());

    juce::Path line;
    for (size_t i = 0; i < xs.size(); ++i)
    {
        const float y = yOf (daw::engine::automationValueAt (pts, view.xToBeat ((double) xs[i]), current));
        if (i == 0)
            line.startNewSubPath (xs[i], y);
        else
            line.lineTo (xs[i], y);
    }
    juce::Path fill (line);
    fill.lineTo (xs.back(), area.getBottom());
    fill.lineTo (xs.front(), area.getBottom());
    fill.closeSubPath();
    g.setColour (colour.withAlpha (0.14f));
    g.fillPath (fill);
    // a dark halo under the line keeps it readable over a clip of its own colour
    g.setColour (t.background.withAlpha (0.75f));
    g.strokePath (line, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (colour);
    g.strokePath (line, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    for (size_t i = 0; i < pts.size(); ++i)
    {
        const float x = (float) view.beatToX (pts[i].beat);
        if (x < x0 || x > x1)
            continue;
        const bool held = (gesture == Gesture::autoMove || gesture == Gesture::autoBend) && autoPointNode.isValid()
                          && autoPointNode.getParent() == lane && lane.indexOf (autoPointNode) == (int) i;
        const float r = held ? kDotRadius + 1.5f : kDotRadius;
        const juce::Rectangle<float> dot (2.0f * r, 2.0f * r);
        g.setColour (t.background);
        g.fillEllipse (dot.withCentre ({ x, yOf (pts[i].value) }).expanded (1.5f));
        g.setColour (colour);
        g.fillEllipse (dot.withCentre ({ x, yOf (pts[i].value) }));
    }
}

bool ArrangementLanes::beginAutomationGesture (const juce::MouseEvent& e)
{
    const auto hit = autoHitAt (e.getPosition());
    if (hit.row < 0 || (hit.point < 0 && ! hit.onLine))
        return false;
    const auto trackId = view.layoutRows[(size_t) hit.row].trackId;
    const auto track = view.model.findTrackOrMaster (trackId);
    if (! track.isValid())
        return false;
    const auto target = view.automationTarget (trackId);
    auto lane = automation::findLane (track, target);
    autoTrackId = trackId;
    autoTargetId = target;
    movedEnough = false;
    readoutText = {};
    readoutAt = e.position;

    // one gesture, one undo transaction: opened here, everything until the button goes up joins it
    view.model.undo.beginNewTransaction (Strings::undoAutoEdit);
    if (hit.point >= 0)
    {
        autoPointNode = lane.getChild (hit.point);
        gesture = Gesture::autoMove;
        return true;
    }
    if (e.mods.isAltDown() && lane.isValid())
    {
        // bend the segment that starts at the nearest point on the left
        const double beat = view.xToBeat ((double) e.x);
        int left = -1;
        for (int i = 0; i < lane.getNumChildren(); ++i)
            if ((double) lane.getChild (i).getProperty (ids::beat, 0.0) <= beat)
                left = i;
        if (left >= 0 && left + 1 < lane.getNumChildren())
        {
            autoPointNode = lane.getChild (left);
            bendStart = (float) (double) autoPointNode.getProperty (ids::curve, 0.0);
            const float a = (float) (double) autoPointNode.getProperty (ids::value, 0.0);
            const float b = (float) (double) lane.getChild (left + 1).getProperty (ids::value, 0.0);
            bendDirection = b >= a ? 1.0f : -1.0f; // dragging up pushes the line up, whichever way it runs
            gesture = Gesture::autoBend;
            return true;
        }
    }

    // a click on the line: a new point under the pointer; a lane without points first gets one at the start with the current value
    lane = automation::getOrAddLane (view.model, track, target);
    if (lane.getNumChildren() == 0)
        automation::addPoint (view.model, lane, { 0.0, view.automationCurrentValue (track, target), 0.0f });
    const double beat = view.snapBeat (view.xToBeat ((double) e.x), e.mods.isCommandDown() || e.mods.isAltDown());
    autoPointNode = automation::addPoint (view.model, lane, { beat, valueForY (hit.row, (float) e.y), 0.0f });
    gesture = Gesture::autoMove;
    repaint();
    return true;
}

void ArrangementLanes::dragAutomation (const juce::MouseEvent& e)
{
    if (! autoPointNode.isValid() || ! autoPointNode.getParent().isValid())
        return;
    const auto track = view.model.findTrackOrMaster (autoTrackId);
    if (! track.isValid())
        return;
    if (! movedEnough && e.getDistanceFromDragStart() < 2)
        return;
    movedEnough = true;
    readoutAt = e.position;
    if (gesture == Gesture::autoBend)
    {
        const float curve = juce::jlimit (-1.0f, 1.0f, bendStart + bendDirection * (float) -e.getDistanceFromDragStartY() / 80.0f);
        automation::setCurve (view.model, autoPointNode, curve);
        readoutText = Strings::arrangement::autoCurve + " " + juce::String (curve, 2);
    }
    else
    {
        const int row = view.rowIndexOfTrack (autoTrackId);
        if (row < 0)
            return;
        const double beat = view.snapBeat (juce::jmax (0.0, view.xToBeat ((double) e.x)), e.mods.isCommandDown() || e.mods.isAltDown());
        const float value = valueForY (row, (float) e.y);
        automation::movePoint (view.model, autoPointNode, beat, value);
        readoutText = view.automationValueText (track, autoTargetId, value);
    }
    repaint();
}

//==============================================================================
void ArrangementLanes::mouseMove (const juce::MouseEvent& e)
{
    if (view.automationMode())
    {
        const auto hit = autoHitAt (e.getPosition());
        if (hit.point >= 0 || hit.onLine)
        {
            setMouseCursor (hit.point >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::CrosshairCursor);
            return;
        }
    }
    const auto hit = clipHit (e.getPosition());
    auto shape = juce::MouseCursor::NormalCursor;
    if (hit.clip.isValid())
        shape = hit.zone == Zone::body ? (e.mods.isAltDown() ? juce::MouseCursor::CopyingCursor : juce::MouseCursor::NormalCursor)
                                        : juce::MouseCursor::LeftRightResizeCursor;
    setMouseCursor (shape);
}

void ArrangementLanes::mouseDown (const juce::MouseEvent& e)
{
    view.takeFocus();
    gesture = Gesture::none;
    deltaBeats = 0.0;
    deltaTracks = 0;
    copying = false;
    movedEnough = false;
    pressedOnEmpty = false;
    dragged.clear();
    autoPointNode = {};

    // the envelope is on top of the clips: a press on it edits it, any other press still selects clips
    if (view.automationMode() && ! e.mods.isPopupMenu() && ! e.mods.isShiftDown() && beginAutomationGesture (e))
        return;

    const auto hit = clipHit (e.getPosition());
    if (e.mods.isPopupMenu())
    {
        if (hit.clip.isValid())
        {
            if (! view.isClipSelected (hit.clip.id()))
                view.selectClip (hit.clip.id(), false);
            showClipMenu (e.getScreenPosition());
        }
        else
            showEmptyMenu (e.getScreenPosition(), view.snapBeat (view.xToBeat (e.x), false));
        return;
    }

    if (hit.clip.isValid())
    {
        const auto id = hit.clip.id();
        if (e.mods.isShiftDown())
        {
            view.selectClip (id, true);
            return;
        }
        if (! view.isClipSelected (id))
            view.selectClip (id, false);
        else
            view.selectionChanged(); // a click inside a selection keeps it and makes this clip the one the editor shows

        const auto track = arrangement::trackOf (view.model, hit.clip);
        switch (hit.zone)
        {
            case Zone::leftEdge:
                gesture = Gesture::resizeLeft;
                stretchClip = id;
                stretchValue = hit.clip.start();
                break;
            case Zone::rightEdge:
                gesture = Gesture::resizeRight;
                stretchClip = id;
                stretchValue = hit.clip.end();
                break;
            case Zone::none:
            case Zone::body:
                gesture = Gesture::move;
                anchorClip = id;
                anchorStart = hit.clip.start();
                anchorRow = view.rowIndexOfTrack (track.id());
                pointerBeatOffset = view.xToBeat (e.x) - hit.clip.start();
                for (const auto& c : view.selectedClipRefs())
                    dragged.push_back ({ c.id(), arrangement::trackOf (view.model, c).id(), c.start() });
                break;
        }
        return;
    }

    // empty space: a drag selects time on the lanes it crosses; a click only moves the cursor
    pressedOnEmpty = true;
    gesture = Gesture::selectTime;
    timeAnchor = view.snapBeat (view.xToBeat (e.x), e.mods.isCommandDown());
    timeRowAnchor = hit.row;
}

void ArrangementLanes::updateMoveGesture (const juce::MouseEvent& e)
{
    const double wanted = view.xToBeat (e.x) - pointerBeatOffset;
    double delta = view.snapBeat (wanted, e.mods.isCommandDown()) - anchorStart;
    double earliest = dragged.front().start;
    for (const auto& d : dragged)
        earliest = std::min (earliest, d.start);
    delta = juce::jmax (delta, -earliest);
    deltaBeats = delta;

    const int row = view.rowAtY (e.y);
    if (row < 0)
        return;
    const int candidate = row - anchorRow;
    bool ok = true;
    for (const auto& d : dragged)
    {
        const int target = view.rowIndexOfTrack (d.trackId) + candidate;
        if (target < 0 || target >= (int) view.layoutRows.size())
        {
            ok = false;
            break;
        }
        const auto from = view.model.findTrackOrMaster (d.trackId), to = view.model.findTrackOrMaster (view.layoutRows[(size_t) target].trackId);
        if (from.type() != to.type()) // a MIDI clip does not go to an audio lane
            ok = false;
    }
    if (ok)
        deltaTracks = candidate;
}

void ArrangementLanes::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;
    switch (gesture)
    {
        case Gesture::autoMove:
        case Gesture::autoBend:
            dragAutomation (e);
            break;
        case Gesture::move:
            if (! movedEnough && e.getDistanceFromDragStart() < 4)
                return;
            movedEnough = true;
            copying = e.mods.isAltDown();
            updateMoveGesture (e);
            setMouseCursor (copying ? juce::MouseCursor::CopyingCursor : juce::MouseCursor::DraggingHandCursor);
            repaint();
            break;
        case Gesture::resizeRight:
        case Gesture::resizeLeft:
        {
            if (! movedEnough && e.getDistanceFromDragStart() < 3)
                return;
            movedEnough = true;
            const auto clip = arrangement::findClip (view.model, stretchClip);
            if (! clip.isValid())
                return;
            const double beat = view.snapBeat (view.xToBeat (e.x), e.mods.isCommandDown());
            stretchValue = gesture == Gesture::resizeRight ? juce::jmax (clip.start() + arrangement::kMinClipBeats, beat)
                                                           : juce::jmin (clip.end() - arrangement::kMinClipBeats, beat);
            repaint();
            break;
        }
        case Gesture::selectTime:
        {
            if (! movedEnough && e.getDistanceFromDragStart() < 4)
                return;
            movedEnough = true;
            if (timeRowAnchor < 0 || view.layoutRows.empty())
                return;
            int row = view.rowAtY (e.y);
            if (row < 0)
                row = e.y < 0 ? 0 : (int) view.layoutRows.size() - 1;
            std::vector<int64_t> tracks;
            for (int i = std::min (row, timeRowAnchor); i <= std::max (row, timeRowAnchor); ++i)
                tracks.push_back (view.layoutRows[(size_t) i].trackId);
            view.setTimeSelection (timeAnchor, view.snapBeat (view.xToBeat (e.x), e.mods.isCommandDown()), tracks);
            break;
        }
        case Gesture::none: break;
    }
}

void ArrangementLanes::mouseUp (const juce::MouseEvent& e)
{
    const auto finished = gesture;
    gesture = Gesture::none;
    setMouseCursor (juce::MouseCursor::NormalCursor);
    if (e.mods.isPopupMenu())
        return;
    switch (finished)
    {
        case Gesture::autoMove:
        case Gesture::autoBend:
            autoPointNode = {};
            readoutText = {};
            break;
        case Gesture::move:
            if (movedEnough)
                view.commitMove (dragged, deltaBeats, deltaTracks, copying);
            break;
        case Gesture::resizeRight:
            if (movedEnough)
                view.commitResizeRight (stretchClip, stretchValue);
            break;
        case Gesture::resizeLeft:
            if (movedEnough)
                view.commitResizeLeft (stretchClip, stretchValue);
            break;
        case Gesture::selectTime:
            if (! movedEnough)
            {
                view.clearSelection();
                view.placeCursor (view.snapBeat (view.xToBeat (e.x), e.mods.isCommandDown()));
            }
            else if (! view.timeSel.isEmpty())
                view.placeCursor (view.timeSel.start);
            break;
        case Gesture::none: break;
    }
    dragged.clear();
    repaint();
}

void ArrangementLanes::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;
    if (view.automationMode())
    {
        const auto ah = autoHitAt (e.getPosition());
        if (ah.point >= 0)
        {
            const auto track = view.model.findTrackOrMaster (view.layoutRows[(size_t) ah.row].trackId);
            const auto lane = automation::findLane (track, view.automationTarget (track.id()));
            if (lane.isValid() && ah.point < lane.getNumChildren())
            {
                view.model.undo.beginNewTransaction (Strings::undoAutoDelete);
                automation::removePoint (view.model, lane.getChild (ah.point));
            }
            gesture = Gesture::none;
            autoPointNode = {};
            repaint();
            return;
        }
        if (ah.onLine)
            return;
    }
    const auto hit = clipHit (e.getPosition());
    if (hit.clip.isValid())
    {
        if (view.onOpenClip)
            view.onOpenClip (arrangement::trackOf (view.model, hit.clip).id(), hit.clip.id());
        return;
    }
    if (hit.row >= 0)
        view.drawClipAt (view.layoutRows[(size_t) hit.row].trackId, view.xToBeat (e.x));
}

void ArrangementLanes::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown())
    {
        view.zoomBy (w.deltaY > 0 ? 1.15 : 1.0 / 1.15, (double) e.x);
        return;
    }
    if (e.mods.isAltDown())
    {
        const int row = view.rowAtY (e.y);
        if (row >= 0)
        {
            const auto& lane = view.layoutRows[(size_t) row];
            view.setTrackHeight (lane.trackId, lane.height + (w.deltaY > 0 ? 8 : -8), true);
        }
        return;
    }
    const bool horizontal = e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY);
    const float d = horizontal ? (std::abs (w.deltaX) > 0.0f ? w.deltaX : w.deltaY) : w.deltaY;
    if (horizontal)
        view.setScrollBeats (view.v.scrollBeats - (double) d * 400.0 / view.v.pxPerBeat);
    else
        view.setScrollY (view.v.scrollY - juce::roundToInt (d * 300.0f));
}

void ArrangementLanes::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    view.zoomBy ((double) scaleFactor, (double) e.x);
}

juce::String ArrangementLanes::helpAt (juce::Point<int> p) const
{
    const auto hit = clipHit (p);
    if (view.automationMode() && (autoHitAt (p).onLine || autoHitAt (p).point >= 0))
        return Strings::help::arrAutomationLanes;
    if (hit.clip.isValid())
        return hit.zone == Zone::body ? Strings::help::arrClip : (hit.zone == Zone::leftEdge ? Strings::help::arrClipLeftEdge : Strings::help::arrClipRightEdge);
    return Strings::help::arrLanes;
}

//==============================================================================
void ArrangementLanes::beginRenameClip (int64_t aclipId)
{
    const auto clip = arrangement::findClip (view.model, aclipId);
    const auto bounds = clipBounds (aclipId);
    if (! clip.isValid() || ! clip.clip().isValid() || bounds.isEmpty())
        return;
    renameEditor = std::make_unique<InlineEditor>();
    renameEditor->setText (clip.clip().name(), false);
    renameEditor->onCommit = [this, aclipId] (const juce::String& text) { finishRename (text, aclipId); };
    renameEditor->onCancel = [this, aclipId] { finishRename ({}, aclipId); };
    addAndMakeVisible (*renameEditor);
    renameEditor->setBounds (bounds.withHeight (juce::jmin (22, bounds.getHeight())).withWidth (juce::jmax (120, bounds.getWidth())));
    renameEditor->grabKeyboardFocus();
    renameEditor->selectAll();
}

void ArrangementLanes::finishRename (const juce::String& text, int64_t aclipId)
{
    auto* dying = renameEditor.release();
    removeChildComponent (dying);
    juce::MessageManager::callAsync ([dying] { delete dying; });
    if (text.isNotEmpty())
        if (const auto clip = arrangement::findClip (view.model, aclipId); clip.isValid() && clip.clip().isValid())
            view.actions.renameClipById (clip.clip().id(), text);
    view.takeFocus();
    repaint();
}

void ArrangementLanes::showClipMenu (juce::Point<int> screenPosition)
{
    namespace A = Strings::arrangement;
    juce::PopupMenu menu;
    const auto add = [&menu] (const juce::String& text, bool enabled, const juce::String& shortcut, std::function<void()> action) {
        juce::PopupMenu::Item item (text);
        item.setEnabled (enabled);
        item.shortcutKeyDescription = shortcut;
        item.action = std::move (action);
        menu.addItem (std::move (item));
    };
    const auto selected = view.selectedClipRefs();
    const bool one = selected.size() == 1;
    const juce::Component::SafePointer<ArrangementLanes> safe (this);
    add (A::menuOpen, one, {}, [safe] {
        if (safe == nullptr || safe->view.selectedAClips.size() != 1)
            return;
        const auto c = arrangement::findClip (safe->view.model, safe->view.selectedAClips.front());
        if (c.isValid() && safe->view.onOpenClip)
            safe->view.onOpenClip (arrangement::trackOf (safe->view.model, c).id(), c.id());
    });
    add (A::menuRename, one, shortcutText ('r'), [safe] {
        if (safe != nullptr)
            safe->view.renameSelection();
    });
    const int current = one && selected.front().clip().isValid() ? selected.front().clip().colour() : -1;
    menu.addSubMenu (Strings::menu::colour, makePaletteMenu (current, [safe] (int index) {
        if (safe == nullptr)
            return;
        safe->view.model.undo.beginNewTransaction (Strings::undoClipColour);
        for (const auto& c : safe->view.selectedClipRefs())
            if (c.clip().isValid())
                c.clip().setColour (juce::jlimit (0, 15, index), &safe->view.model.undo);
    }));
    if (one)
        if (const auto takes = arrangement::takesOf (selected.front()); takes.size() > 1)
        {
            // Clips recorded in a loop keep every pass: "Takes ▸ Take 1 / 2 / 3"
            juce::PopupMenu takeMenu;
            const auto clipId = selected.front().id();
            for (const auto& take : takes)
                takeMenu.addItem (Strings::arrRec::take (take.number), true, take.active, [safe, clipId, number = take.number] {
                    if (safe != nullptr)
                        safe->view.switchTake (clipId, number);
                });
            menu.addSubMenu (Strings::arrRec::menuTakes, takeMenu);
        }
    menu.addSeparator();
    add (A::menuDuplicate, true, shortcutText ('d'), [safe] {
        if (safe != nullptr)
            safe->view.duplicateSelection();
    });
    add (A::menuSplit, true, shortcutText ('e'), [safe] {
        if (safe != nullptr)
            safe->view.splitAtCursor();
    });
    add (A::menuJoin, selected.size() >= 2, shortcutText ('j'), [safe] {
        if (safe != nullptr)
            safe->view.joinSelection();
    });
    add (A::menuLoopSelection, true, shortcutText ('l'), [safe] {
        if (safe != nullptr)
            safe->view.loopSelection();
    });
    menu.addSeparator();
    add (A::menuSendToSession, true, {}, [safe] {
        if (safe != nullptr)
            safe->view.sendSelectionToSession();
    });
    add (Strings::menu::remove, true, juce::KeyPress (juce::KeyPress::deleteKey).getTextDescriptionWithIcons(), [safe] {
        if (safe != nullptr)
            safe->view.deleteSelection();
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (screenPosition.x, screenPosition.y, 1, 1)));
}

void ArrangementLanes::showEmptyMenu (juce::Point<int> screenPosition, double beat)
{
    namespace A = Strings::arrangement;
    juce::PopupMenu menu;
    const juce::Component::SafePointer<ArrangementLanes> safe (this);
    const int row = view.rowAtY (getLocalPoint (nullptr, screenPosition).y);
    auto trackId = row >= 0 ? view.layoutRows[(size_t) row].trackId : -1;
    if (trackId == ProjectModel::kMasterTrackId)
        trackId = -1; // the master row holds automation only
    menu.addItem (A::menuNewClip, trackId >= 0, false, [safe, trackId, beat] {
        if (safe != nullptr)
            safe->view.drawClipAt (trackId, beat);
    });
    juce::PopupMenu::Item paste (A::menuPaste);
    paste.setEnabled (view.hasClipboard());
    paste.shortcutKeyDescription = shortcutText ('v');
    paste.action = [safe, beat] {
        if (safe == nullptr)
            return;
        safe->view.placeCursor (beat);
        safe->view.pasteAtCursor();
    };
    menu.addItem (std::move (paste));
    menu.addItem (A::menuSelectAll, true, false, [safe] {
        if (safe != nullptr)
            safe->view.selectAllClips();
    });
    menu.addSeparator();
    // New tracks go below the one that was clicked (at the end on the empty space under the tracks).
    const int insertAt = trackId >= 0 ? view.model.indexOfTrack (trackId) + 1 : -1;
    menu.addItem (A::menuAddMidiTrack, [safe, insertAt] {
        if (safe != nullptr)
            safe->view.selectionState.selectTrack (safe->view.actions.addMidiTrack (insertAt).id());
    });
    menu.addItem (A::menuAddAudioTrack, [safe, insertAt] {
        if (safe != nullptr)
            safe->view.selectionState.selectTrack (safe->view.actions.addAudioTrack (insertAt).id());
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (screenPosition.x, screenPosition.y, 1, 1)));
}
} // namespace ui
