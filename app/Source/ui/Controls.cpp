#include "Controls.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace ui
{
namespace
{
constexpr float kPi = juce::MathConstants<float>::pi;
constexpr float kKnobStart = -0.75f * kPi, kKnobEnd = 0.75f * kPi; // 270 degrees, gap at the bottom
constexpr double kDragRange = 180.0; // pixels for a full turn
} // namespace

bool parseNumber (const juce::String& text, double& result)
{
    juce::String s = text.replaceCharacter (',', '.').replace (juce::String::fromUTF8 ("\xe2\x88\x92"), "-").trim();
    juce::String number;
    for (int i = 0; i < s.length(); ++i)
    {
        const auto c = s[i];
        if (juce::CharacterFunctions::isDigit (c) || c == '.' || (c == '-' && number.isEmpty()) || (c == '+' && number.isEmpty()))
            number << juce::String::charToString (c);
        else if (number.isNotEmpty() && c != ' ')
            break; // unit suffix
    }
    if (number.isEmpty() || ! number.containsAnyOf ("0123456789"))
        return false;
    result = number.getDoubleValue();
    return true;
}

//==============================================================================
Knob::Knob()
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void Knob::setCaption (const juce::String& text)
{
    caption = text;
    repaint();
}

void Knob::setRingColour (juce::Colour colour)
{
    ringColour = colour;
    repaint();
}

void Knob::setValue (double normalised)
{
    normalised = juce::jlimit (0.0, 1.0, normalised);
    if (std::abs (normalised - value) > 1.0e-9)
    {
        value = normalised;
        repaint();
    }
}

void Knob::paint (juce::Graphics& g)
{
    const auto& t = theme();
    auto area = getLocalBounds().toFloat();
    const float captionHeight = caption.isNotEmpty() ? 13.0f : 0.0f;
    auto captionArea = area.removeFromBottom (captionHeight);
    const float d = juce::jmin (area.getWidth(), area.getHeight());
    const auto knob = juce::Rectangle<float> (d, d).withCentre (area.getCentre());
    if (t.glass)
    {
        glass::knob (g,
                     knob.reduced (1.0f),
                     (float)value,
                     isEnabled () ? ringColour.value_or (t.accentFill) : t.dimText.withAlpha (0.5f),
                     bipolar);
        if (caption.isNotEmpty ())
        {
            const bool showValue = (hover || gestureOpen) && valueText && isEnabled ();
            g.setColour (showValue ? t.text : t.dimText);
            g.setFont (showValue ? t.numFont (10.0f) : t.uiFont (10.5f));
            g.drawText (showValue ? valueText (value) : caption,
                        captionArea.toNearestInt ().expanded (6, 0),
                        juce::Justification::centred,
                        false);
        }
        return;
    }
    const float thickness = juce::jmax (3.0f, d * 0.16f);
    const float radius = d * 0.5f - thickness * 0.5f;
    const float alpha = isEnabled() ? 1.0f : 0.4f;

    auto arc = [&] (float from, float to, juce::Colour colour) {
        juce::Path p;
        p.addCentredArc (knob.getCentreX(), knob.getCentreY(), radius, radius, 0.0f, from, to, true);
        g.setColour (colour.withMultipliedAlpha (alpha));
        g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    };
    arc (kKnobStart, kKnobEnd, t.panel2);
    const auto ring = ringColour.value_or (t.accent);
    const float valueAngle = kKnobStart + (float) value * (kKnobEnd - kKnobStart);
    if (bipolar)
    {
        const float mid = 0.0f;
        if (std::abs (valueAngle - mid) > 0.02f)
            arc (juce::jmin (mid, valueAngle), juce::jmax (mid, valueAngle), ring);
    }
    else if (value > 0.001)
        arc (kKnobStart, valueAngle, ring);

    const auto inner = knob.reduced (thickness);
    g.setColour (t.panel.withMultipliedAlpha (alpha));
    g.fillEllipse (inner);
    g.setColour (t.line.withMultipliedAlpha (alpha));
    g.drawEllipse (inner, 1.0f);

    if (caption.isNotEmpty())
    {
        const bool showValue = (hover || gestureOpen) && valueText && isEnabled();
        g.setColour (showValue ? t.text : t.dimText);
        g.setFont (showValue ? t.numFont (10.0f) : t.uiFont (10.5f));
        g.drawText (showValue ? valueText (value) : caption, captionArea.toNearestInt().expanded (6, 0), juce::Justification::centred, false);
    }
}

void Knob::beginGesture()
{
    if (gestureOpen)
        return;
    gestureOpen = true;
    if (onGestureStart)
        onGestureStart();
}

void Knob::endGesture()
{
    if (! gestureOpen)
        return;
    gestureOpen = false;
    if (onGestureEnd)
        onGestureEnd();
    repaint();
}

void Knob::change (double newValue)
{
    newValue = juce::jlimit (0.0, 1.0, newValue);
    if (std::abs (newValue - value) < 1.0e-9)
        return;
    value = newValue;
    if (onValueChange)
        onValueChange (value);
    repaint();
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onContextMenu)
            onContextMenu (e);
        return;
    }
    lastDragPosition = e.getPosition();
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (! isEnabled() || e.mods.isPopupMenu())
        return;
    if (! gestureOpen && e.getDistanceFromDragStart() < 3)
        return;
    beginGesture();
    const int dy = lastDragPosition.y - e.y;
    lastDragPosition = e.getPosition();
    change (value + (double) dy / kDragRange * (e.mods.isShiftDown() ? 0.1 : 1.0));
}

void Knob::mouseUp (const juce::MouseEvent&)
{
    endGesture();
}

void Knob::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! isEnabled() || e.mods.isPopupMenu())
        return;
    beginGesture();
    change (defaultValue);
    endGesture();
}

void Knob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! wheelChangesValue)
    {
        juce::Component::mouseWheelMove (e, w); // up to the parent
        return;
    }
    if (! isEnabled())
        return;
    const double delta = (double) (w.isReversed ? -w.deltaY : w.deltaY) * (e.mods.isShiftDown() ? 0.05 : 0.5);
    beginGesture();
    change (value + delta);
    endGesture();
}

//==============================================================================
void Meter::push (float peakL, float peakR)
{
    const float newL = juce::jmax (peakL, levelL * 0.86f);
    const float newR = juce::jmax (peakR, levelR * 0.86f);
    // Repaint only when the bars moved visibly.
    const bool changed = std::abs (newL - levelL) > 0.002f || std::abs (newR - levelR) > 0.002f;
    levelL = newL;
    levelR = newR;
    if (changed)
        repaint();
}

void Meter::paint (juce::Graphics& g)
{
    const auto& t = theme();
    auto r = getLocalBounds().toFloat();
    g.setColour (t.panel2);
    g.fillRoundedRectangle (r, 2.0f);
    r = r.reduced (1.5f);
    const float barWidth = (r.getWidth() - 1.0f) * 0.5f;
    auto drawBar = [&] (juce::Rectangle<float> bar, float level) {
        const float db = juce::Decibels::gainToDecibels (level, -60.0f);
        const float norm = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
        auto filled = bar.removeFromBottom (bar.getHeight() * norm);
        juce::ColourGradient grad (t.play, 0.0f, r.getBottom(), t.record, 0.0f, r.getY(), false);
        grad.addColour (0.7, t.play.interpolatedWith (t.record, 0.5f));
        g.setGradientFill (grad);
        g.fillRect (filled);
    };
    drawBar (r.withWidth (barWidth), levelL);
    drawBar (r.withTrimmedLeft (barWidth + 1.0f), levelR);
}

//==============================================================================
Fader::Fader()
{
    range.setSkewForCentre (-12.0);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void Fader::setValueDb (double db)
{
    db = juce::jlimit (kMinDb, kMaxDb, db);
    if (std::abs (db - valueDb) > 1.0e-6)
    {
        valueDb = db;
        repaint();
    }
}

juce::Rectangle<float> Fader::thumbArea() const
{
    // The thumb travels between its half-height from the top and bottom.
    const float h = 10.0f;
    const auto bounds = getLocalBounds().toFloat();
    const float travel = bounds.getHeight() - h;
    const float y = bounds.getBottom() - h - (float) dbToNorm (valueDb) * travel;
    return { bounds.getX(), y, bounds.getWidth(), h };
}

void Fader::paint (juce::Graphics& g)
{
    const auto& t = theme();
    const auto bounds = getLocalBounds().toFloat();
    const float cx = bounds.getCentreX();
    const float travel = bounds.getHeight() - 10.0f;

    g.setColour (t.panel2);
    g.fillRoundedRectangle (juce::Rectangle<float> (4.0f, bounds.getHeight() - 4.0f).withCentre (bounds.getCentre()), 2.0f);

    // 0 dB tick
    const float zeroY = bounds.getBottom() - 5.0f - (float) dbToNorm (0.0) * travel;
    g.setColour (t.dimText.withAlpha (0.6f));
    g.fillRect (cx - 7.0f, zeroY, 14.0f, 1.0f);

    const auto thumb = thumbArea();
    if (t.glass)
    {
        const auto lens =
          juce::Rectangle<float> (juce::jmin (bounds.getWidth (), 24.0f), 16.0f).withCentre (thumb.getCentre ());
        g.setColour (t.accentFill.withAlpha (0.65f));
        g.fillRoundedRectangle (
          juce::Rectangle<float> (3.0f, juce::jmax (0.0f, bounds.getBottom () - thumb.getCentreY ()))
            .withPosition (cx - 1.5f, thumb.getCentreY ()),
          1.5f);
        glass::lens (g, lens, t.accentFill, true);
        return;
    }
    g.setColour (t.text);
    g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (bounds.getWidth(), 22.0f), thumb.getHeight()).withCentre (thumb.getCentre()), 3.0f);
    g.setColour (t.chrome.withAlpha (0.55f));
    g.fillRect (juce::Rectangle<float> (juce::jmin (bounds.getWidth(), 22.0f) - 6.0f, 1.0f).withCentre (thumb.getCentre()));
}

void Fader::beginGesture()
{
    if (gestureOpen)
        return;
    gestureOpen = true;
    if (onGestureStart)
        onGestureStart();
}

void Fader::endGesture()
{
    if (! gestureOpen)
        return;
    gestureOpen = false;
    if (onGestureEnd)
        onGestureEnd();
}

void Fader::change (double db)
{
    db = juce::jlimit (kMinDb, kMaxDb, range.snapToLegalValue (db));
    if (std::abs (db - valueDb) < 1.0e-6)
        return;
    valueDb = db;
    if (onValueChange)
        onValueChange (valueDb);
    repaint();
}

void Fader::mouseDown (const juce::MouseEvent& e)
{
    lastDragPosition = e.getPosition();
}

void Fader::mouseDrag (const juce::MouseEvent& e)
{
    if (! gestureOpen && e.getDistanceFromDragStart() < 2)
        return;
    beginGesture();
    const float travel = (float) getHeight() - 10.0f;
    const int dy = lastDragPosition.y - e.y;
    lastDragPosition = e.getPosition();
    if (travel <= 1.0f)
        return;
    const double norm = dbToNorm (valueDb) + (double) dy / (double) travel * (e.mods.isShiftDown() ? 0.1 : 1.0);
    change (range.convertFrom0to1 (juce::jlimit (0.0, 1.0, norm)));
}

void Fader::mouseUp (const juce::MouseEvent&)
{
    endGesture();
}

void Fader::mouseDoubleClick (const juce::MouseEvent&)
{
    beginGesture();
    change (0.0);
    endGesture();
}

void Fader::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const double delta = (double) (w.isReversed ? -w.deltaY : w.deltaY) * (e.mods.isShiftDown() ? 1.0 : 10.0);
    beginGesture();
    change (valueDb + delta);
    endGesture();
}

//==============================================================================
NumberField::NumberField()
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void NumberField::setRange (double lo, double hi, double step)
{
    minimum = lo;
    maximum = hi;
    interval = step;
}

double NumberField::snap (double v) const
{
    v = juce::jlimit (minimum, maximum, v);
    if (interval > 0.0)
        v = minimum + std::round ((v - minimum) / interval) * interval;
    return juce::jlimit (minimum, maximum, v);
}

void NumberField::setValue (double v)
{
    v = snap (v);
    if (std::abs (v - value) > 1.0e-9)
    {
        value = v;
        repaint();
    }
}

juce::String NumberField::getDisplayText() const
{
    return juce::String (value, decimals).replace ("-", juce::String::fromUTF8 ("\xe2\x88\x92")) + suffix;
}

void NumberField::paint (juce::Graphics& g)
{
    const auto& t = theme();
    auto r = getLocalBounds().toFloat();
    if (boxed && (boxedInGlass || ! t.glass))
    {
        fillCell (g, r, t.panel);
        strokeCell (g, r, t.line);
    }
    if (editor != nullptr)
        return;
    g.setColour (t.text);
    g.setFont (t.numFont (fontHeight));
    g.drawText (getDisplayText(), r.toNearestInt().reduced (4, 0), justification, false);
}

void NumberField::resized()
{
    if (editor != nullptr)
        editor->setBounds (getLocalBounds().reduced (1));
}

void NumberField::beginGesture()
{
    if (gestureOpen)
        return;
    gestureOpen = true;
    if (onGestureStart)
        onGestureStart();
}

void NumberField::endGesture()
{
    if (! gestureOpen)
        return;
    gestureOpen = false;
    if (onGestureEnd)
        onGestureEnd();
}

void NumberField::change (double v)
{
    v = snap (v);
    if (std::abs (v - value) < 1.0e-9)
        return;
    value = v;
    if (onValueChange)
        onValueChange (value);
    repaint();
}

void NumberField::mouseDown (const juce::MouseEvent& e)
{
    lastDragPosition = e.getPosition();
    dragAccumulator = 0.0;
}

void NumberField::mouseDrag (const juce::MouseEvent& e)
{
    if (editor != nullptr)
        return;
    if (! gestureOpen && e.getDistanceFromDragStart() < 3)
        return;
    beginGesture();
    const int dy = lastDragPosition.y - e.y;
    lastDragPosition = e.getPosition();
    dragAccumulator += (double) dy * sensitivity * (e.mods.isShiftDown() ? 0.1 : 1.0);
    // Accumulate sub-interval movement so that Shift-drag still moves the value, slowly.
    if (std::abs (dragAccumulator) >= interval * 0.5)
    {
        const double next = snap (value + dragAccumulator);
        dragAccumulator = 0.0;
        change (next);
    }
}

void NumberField::mouseUp (const juce::MouseEvent&)
{
    endGesture();
}

void NumberField::mouseDoubleClick (const juce::MouseEvent&)
{
    beginEditing();
}

void NumberField::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (wheelStep <= 0.0)
    {
        juce::Component::mouseWheelMove (e, w); // up to the parent
        return;
    }
    if (editor != nullptr)
        return;
    const double delta = (double) (w.isReversed ? -w.deltaY : w.deltaY) * 10.0 * wheelStep * (e.mods.isShiftDown() ? 0.1 : 1.0);
    beginGesture();
    change (value + (std::abs (delta) < interval ? (delta < 0 ? -interval : interval) : delta));
    endGesture();
}

void NumberField::beginEditing()
{
    if (editor != nullptr)
        return;
    editor = std::make_unique<InlineEditor>();
    editor->setText (juce::String (value, decimals), false);
    editor->setJustification (justification);
    editor->onCommit = [this] (const juce::String& text) { finishEditing (true, text); };
    editor->onCancel = [this] { finishEditing (false, {}); };
    addAndMakeVisible (*editor);
    resized();
    editor->grabKeyboardFocus();
    editor->selectAll();
}

void NumberField::finishEditing (bool commit, const juce::String& text)
{
    // Called from the editor's own key or focus handler: delete it later.
    auto* dying = editor.release();
    removeChildComponent (dying);
    juce::MessageManager::callAsync ([dying] { delete dying; });
    if (commit)
    {
        double parsed = 0.0;
        if (parseNumber (text, parsed))
        {
            beginGesture();
            change (parsed);
            endGesture();
        }
    }
    repaint();
}
} // namespace ui
