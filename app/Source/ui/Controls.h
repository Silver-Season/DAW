#pragma once

#include "UiCommon.h"

#include <functional>

namespace ui
{
// Rotary knob in the style of the concept: a ring that fills with the value, a disc in the middle and a
// caption underneath. While the mouse is over it or it is dragged, the caption shows the value instead.
// Drag up/down (Shift: x0.1), wheel, double-click resets to the default. The owner writes the value to the
// model or to a plugin parameter from the callbacks: one gesture = one undo transaction.
class Knob final : public juce::Component
{
public:
    Knob();

    void setCaption (const juce::String& text);
    void setRingColour (juce::Colour colour);
    void useDefaultRingColour() { ringColour.reset(); repaint(); }
    void setBipolar (bool b) { bipolar = b; repaint(); }

    // Normalised value 0..1. Callbacks are not called.
    void setValue (double normalised);
    double getValue() const { return value; }
    void setDefaultValue (double normalised) { defaultValue = normalised; }
    bool isDragging() const { return gestureOpen; }
    // Off: the scroll wheel passes through to the view behind (scrolling over a pan knob must not move it).
    void setWheelChangesValue (bool v) { wheelChangesValue = v; }

    std::function<void()> onGestureStart, onGestureEnd;
    std::function<void (double)> onValueChange;
    std::function<juce::String (double)> valueText;
    std::function<void (const juce::MouseEvent&)> onContextMenu;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }

private:
    void change (double newValue);
    void beginGesture();
    bool wheelChangesValue = true;
    void endGesture();

    juce::String caption;
    std::optional<juce::Colour> ringColour;
    double value = 0.0, defaultValue = 0.0;
    bool bipolar = false, hover = false, gestureOpen = false;
    juce::Point<int> lastDragPosition;
};

// Level meter with fast attack and slow decay, fed with peaks from the engine's atomics.
class Meter final : public juce::Component
{
public:
    void push (float peakL, float peakR);
    void paint (juce::Graphics&) override;

private:
    float levelL = 0.0f, levelR = 0.0f;
};

// Vertical volume fader, -60..+6 dB. Same gestures as the knob; double-click resets to 0 dB.
class Fader final : public juce::Component
{
public:
    static constexpr double kMinDb = -60.0, kMaxDb = 6.0;

    Fader();
    void setValueDb (double db);
    double getValueDb() const { return valueDb; }
    bool isDragging() const { return gestureOpen; }

    std::function<void()> onGestureStart, onGestureEnd;
    std::function<void (double)> onValueChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    double dbToNorm (double db) const { return range.convertTo0to1 (db); }
    void change (double db);
    void beginGesture();
    void endGesture();
    juce::Rectangle<float> thumbArea() const;

    juce::NormalisableRange<double> range { kMinDb, kMaxDb, 0.1 };
    double valueDb = 0.0;
    bool gestureOpen = false;
    juce::Point<int> lastDragPosition;
};

// A number in a box: drag vertically (Shift: x0.1), wheel, double-click types a value in place
// (Enter confirms, Esc cancels). Used for the tempo and the dB readouts.
class NumberField final : public juce::Component
{
public:
    NumberField();

    void setRange (double minimum, double maximum, double interval);
    void setValue (double v);
    double getValue() const { return value; }
    void setSuffix (const juce::String& s) { suffix = s; repaint(); }
    void setDecimals (int d) { decimals = d; repaint(); }
    void setDragSensitivity (double unitsPerPixel) { sensitivity = unitsPerPixel; }
    void setWheelStep (double step) { wheelStep = step; } // 0: the wheel passes through to the view behind
    void setBoxed (bool b) { boxed = b; repaint(); }
    void setFontHeight (float h) { fontHeight = h; repaint(); }
    void setJustification (juce::Justification j) { justification = j; repaint(); }
    bool isEditing() const { return editor != nullptr; }
    juce::String getDisplayText() const;

    std::function<void()> onGestureStart, onGestureEnd;
    std::function<void (double)> onValueChange;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    void beginEditing();

private:
    double snap (double v) const;
    void change (double v);
    void beginGesture();
    void endGesture();
    void finishEditing (bool commit, const juce::String& text);

    double value = 0.0, minimum = 0.0, maximum = 1.0, interval = 0.01, sensitivity = 0.1, wheelStep = 1.0;
    int decimals = 2;
    juce::String suffix;
    bool boxed = true, gestureOpen = false;
    float fontHeight = 13.0f;
    juce::Justification justification = juce::Justification::centred;
    juce::Point<int> lastDragPosition;
    double dragAccumulator = 0.0;
    std::unique_ptr<InlineEditor> editor;
};

// Parses what the user typed into a number field: accepts a comma as the decimal separator, a minus
// sign (also U+2212), and ignores units such as "dB" or "BPM". Returns false if there is no number.
bool parseNumber (const juce::String& text, double& result);
} // namespace ui
