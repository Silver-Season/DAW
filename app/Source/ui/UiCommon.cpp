#include "UiCommon.h"

namespace ui
{
namespace icons
{
juce::Path play (juce::Rectangle<float> a)
{
    juce::Path p;
    p.addTriangle (a.getX(), a.getY(), a.getX(), a.getBottom(), a.getRight(), a.getCentreY());
    return p;
}

juce::Path stop (juce::Rectangle<float> a)
{
    juce::Path p;
    p.addRectangle (a);
    return p;
}

juce::Path record (juce::Rectangle<float> a)
{
    juce::Path p;
    p.addEllipse (a);
    return p;
}

juce::Path loop (juce::Rectangle<float> a)
{
    // Two arrows chasing each other: an open ring with an arrowhead.
    juce::Path p;
    const auto c = a.getCentre();
    const float r = juce::jmin (a.getWidth(), a.getHeight()) * 0.5f;
    p.addCentredArc (c.x, c.y, r, r, 0.0f, juce::MathConstants<float>::pi * 0.25f, juce::MathConstants<float>::pi * 1.75f, true);
    juce::Path head;
    head.addTriangle (c.x + r * 0.7f - 3.0f, c.y - r * 0.7f - 3.0f, c.x + r * 0.7f + 4.0f, c.y - r * 0.7f - 1.0f, c.x + r * 0.7f + 0.5f, c.y - r * 0.7f + 4.0f);
    juce::PathStrokeType (1.6f).createStrokedPath (p, p);
    p.addPath (head);
    return p;
}

juce::Path metronome (juce::Rectangle<float> a)
{
    // A trapezoid body with a pendulum.
    juce::Path p;
    p.startNewSubPath (a.getX() + a.getWidth() * 0.3f, a.getY());
    p.lineTo (a.getX() + a.getWidth() * 0.7f, a.getY());
    p.lineTo (a.getRight(), a.getBottom());
    p.lineTo (a.getX(), a.getBottom());
    p.closeSubPath();
    juce::Path out;
    juce::PathStrokeType (1.4f).createStrokedPath (out, p);
    juce::Path arm;
    arm.addLineSegment ({ a.getCentreX(), a.getBottom() - 2.0f, a.getX() + a.getWidth() * 0.72f, a.getY() + 2.0f }, 1.3f);
    out.addPath (arm);
    return out;
}

juce::Path search (juce::Rectangle<float> a)
{
    juce::Path p;
    const float r = a.getWidth() * 0.34f;
    p.addEllipse (a.getX(), a.getY(), r * 2.0f, r * 2.0f);
    juce::Path ring;
    juce::PathStrokeType (1.5f).createStrokedPath (ring, p);
    ring.addLineSegment ({ a.getX() + r * 1.7f, a.getY() + r * 1.7f, a.getRight(), a.getBottom() }, 1.6f);
    return ring;
}

juce::Path chevronDown (juce::Rectangle<float> a)
{
    juce::Path p;
    p.startNewSubPath (a.getX(), a.getY());
    p.lineTo (a.getCentreX(), a.getBottom());
    p.lineTo (a.getRight(), a.getY());
    juce::Path out;
    juce::PathStrokeType (1.5f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt).createStrokedPath (out, p);
    return out;
}

juce::Path triangle (juce::Rectangle<float> a, int direction)
{
    juce::Path p;
    switch (direction)
    {
        case 0: p.addTriangle (a.getX(), a.getBottom(), a.getRight(), a.getBottom(), a.getCentreX(), a.getY()); break;
        case 1: p.addTriangle (a.getX(), a.getY(), a.getX(), a.getBottom(), a.getRight(), a.getCentreY()); break;
        case 2: p.addTriangle (a.getX(), a.getY(), a.getRight(), a.getY(), a.getCentreX(), a.getBottom()); break;
        default: p.addTriangle (a.getRight(), a.getY(), a.getRight(), a.getBottom(), a.getX(), a.getCentreY()); break;
    }
    return p;
}

juce::Path cross (juce::Rectangle<float> a)
{
    juce::Path p;
    p.addLineSegment ({ a.getX(), a.getY(), a.getRight(), a.getBottom() }, 1.5f);
    p.addLineSegment ({ a.getRight(), a.getY(), a.getX(), a.getBottom() }, 1.5f);
    return p;
}

juce::Path openExternal (juce::Rectangle<float> a)
{
    juce::Path p;
    p.addLineSegment ({ a.getX(), a.getBottom(), a.getRight(), a.getY() }, 1.5f);
    p.addTriangle (a.getX() + a.getWidth() * 0.35f, a.getY(), a.getRight(), a.getY(), a.getRight(), a.getY() + a.getHeight() * 0.65f);
    return p;
}

juce::Path headphones (juce::Rectangle<float> a)
{
    juce::Path arc;
    arc.addCentredArc (a.getCentreX(), a.getCentreY() + a.getHeight() * 0.08f, a.getWidth() * 0.42f, a.getHeight() * 0.42f, 0.0f,
                       -juce::MathConstants<float>::halfPi * 1.0f - 1.35f, -juce::MathConstants<float>::halfPi + 1.35f + juce::MathConstants<float>::pi * 0.0f, true);
    juce::Path stroked;
    juce::PathStrokeType (1.6f).createStrokedPath (stroked, arc);
    const float w = a.getWidth() * 0.22f, h = a.getHeight() * 0.38f;
    stroked.addRoundedRectangle (a.getX(), a.getBottom() - h - a.getHeight() * 0.05f, w, h, 1.5f);
    stroked.addRoundedRectangle (a.getRight() - w, a.getBottom() - h - a.getHeight() * 0.05f, w, h, 1.5f);
    return stroked;
}
} // namespace icons

//==============================================================================
InlineEditor::InlineEditor()
{
    setMultiLine (false);
    setReturnKeyStartsNewLine (false);
    setScrollbarsShown (false);
    setBorder ({ 0, 4, 0, 4 });
    setSelectAllWhenFocused (true);
    lookAndFeelChanged();
}

void InlineEditor::lookAndFeelChanged()
{
    const auto& t = theme();
    setColour (juce::TextEditor::backgroundColourId, t.panel2);
    setColour (juce::TextEditor::textColourId, t.text);
    setColour (juce::TextEditor::highlightColourId, t.accent.withAlpha (0.35f));
    setColour (juce::TextEditor::highlightedTextColourId, t.text);
    setColour (juce::TextEditor::outlineColourId, t.accent);
    setColour (juce::TextEditor::focusedOutlineColourId, t.accent);
    setColour (juce::CaretComponent::caretColourId, t.accent);
    applyFontToAllText (t.uiFont (12.5f, 500));
}

void InlineEditor::finish (bool commit)
{
    if (finished)
        return;
    finished = true;
    const auto text = getText();
    auto commitFn = onCommit;
    auto cancelFn = onCancel;
    // The owner deletes this editor from the callback, so nothing may touch `this` afterwards.
    if (commit && commitFn)
        commitFn (text);
    else if (cancelFn)
        cancelFn();
}

bool InlineEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::returnKey)
    {
        finish (true);
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        finish (false);
        return true;
    }
    return TextEditor::keyPressed (key);
}

void InlineEditor::focusLost (FocusChangeType type)
{
    TextEditor::focusLost (type);
    finish (true);
}

bool textFieldHasFocus()
{
    return dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr;
}
} // namespace ui
