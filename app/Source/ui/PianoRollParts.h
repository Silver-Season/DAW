#pragma once

// The parts of the PianoRoll (private to the editor): declared here so that each can be defined in its own file.
#include "PianoRoll.h"

#include "../Strings.h"

namespace ui
{
// A pill-shaped button or toggle, painted from the theme. Used for the switches of the properties panel.
class Chip final : public juce::Component
{
public:
    Chip (const juce::String& label, const juce::String& help, bool toggle) : text (label), isToggle (toggle) { setHelpText (help); }

    void setText (const juce::String& t)
    {
        if (t != text)
        {
            text = t;
            repaint();
        }
    }
    void setOn (bool v)
    {
        if (v != on)
        {
            on = v;
            repaint();
        }
    }
    bool isOn() const { return on; }
    std::function<void()> onClick;

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool lit = isToggle && on;
        auto fill = lit ? t.accent : (isMouseOver() && isEnabled() ? t.panel2.brighter (0.08f) : t.panel2);
        if (! isEnabled())
            fill = fill.withMultipliedAlpha (0.5f);
        fillCell (g, r, fill, 0.8f);
        strokeCell (g, r, lit ? t.accent : t.line, 1.0f, 0.8f);
        g.setColour (lit ? t.clipInk : (isEnabled() ? t.text : t.dimText));
        g.setFont (t.uiFont (11.5f, lit ? 600 : 500));
        g.drawText (text, getLocalBounds().reduced (3, 0), juce::Justification::centred, true);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (isEnabled() && getLocalBounds().contains (e.getPosition()) && onClick)
            onClick();
    }

private:
    juce::String text;
    bool isToggle, on = false;
};

//==============================================================================
// Left column: name, loop, grid, scale, quantise, transpose.
class PianoRoll::Props final : public juce::Component
{
public:
    explicit Props (PianoRoll& owner);
    ~Props() override;

    // Copies the state of the clip and the editor into the widgets (not while one is being dragged).
    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;

    static constexpr int kContentHeight = 398;

private:
    class NameField;
    juce::Viewport viewport;
    class Content;
    std::unique_ptr<Content> content;
};

//==============================================================================
// Ruler: bar numbers, the loop brace, the start point triangle, the playing position.
class PianoRoll::Ruler final : public juce::Component, public HelpProvider
{
public:
    explicit Ruler (PianoRoll& o) : owner (o) { setHelpText (Strings::help::editorRuler); }

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::String helpAt (juce::Point<int>) const override { return {}; }

private:
    enum class Part
    {
        none,
        braceStart,
        braceEnd,
        braceBody,
        startPoint
    };
    Part partAt (juce::Point<int>) const;
    void updateCursor (Part);
    juce::Rectangle<int> braceArea() const { return { 0, getHeight() - 11, getWidth(), 11 }; }

    PianoRoll& owner;
    Part dragPart = Part::none;
    double origStart = 0.0, origEnd = 0.0, origOffset = 0.0, anchorBeat = 0.0;
    bool changed = false;
};

//==============================================================================
// Piano keys on the left of the grid. A press plays the note and selects every note of that pitch.
class PianoRoll::Keys final : public juce::Component
{
public:
    explicit Keys (PianoRoll& o) : owner (o) { setHelpText (Strings::help::editorKeys); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    int pitchAt (int y) const;
    PianoRoll& owner;
    int heldPitch = -1;
};

//==============================================================================
// The note grid.
class PianoRoll::NoteGrid final : public juce::Component
{
public:
    explicit NoteGrid (PianoRoll& o);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;
    void focusGained (FocusChangeType) override { repaint(); }

    // The note under a point (the topmost), or an invalid tree.
    juce::ValueTree noteAt (juce::Point<int>) const;
    bool onRightEdge (const juce::ValueTree& n, juce::Point<int> p) const;
    // The last x drawn for the playing cursor, so that tick() can repaint just that strip.
    void setCursorBeat (double beat);
    juce::Point<int> mousePositionOrCentre() const;

private:
    enum class Gesture
    {
        none,
        rubber,
        move,
        resize,
        pencil
    };

    struct Origin
    {
        juce::ValueTree node;
        daw::engine::Note note;
    };

    void beginGesture (const juce::String& transactionName);
    void startMove (const juce::ValueTree& hit, const juce::MouseEvent&);
    void applyMove (const juce::MouseEvent&);
    void applyResize (const juce::MouseEvent&);
    void applyPencil (const juce::MouseEvent&);
    void finishGesture();
    int pitchForRow (int row, bool constrain) const;
    juce::Rectangle<int> rubberRect() const;
    void updateCursor (juce::Point<int>);
    juce::ValueTree addNoteAt (juce::Point<int>, bool floorToGrid);

    PianoRoll& owner;
    Gesture gesture = Gesture::none;
    bool transactionOpen = false, moved = false, copied = false, wasSelectedOnDown = false;
    juce::ValueTree pressed;
    std::vector<Origin> origins;
    juce::Point<int> downPosition, dragPosition;
    clipedit::Nodes selectionBeforeRubber;
    double anchorStart = 0.0, anchorEnd = 0.0;
    int anchorPitch = 0, lastAuditioned = -1;
    double cursorBeat = -1.0;
    int cursorPixel = -100;
    juce::ValueTree drawnNote;
};

//==============================================================================
class PianoRoll::VelocityLane final : public juce::Component
{
public:
    explicit VelocityLane (PianoRoll& o) : owner (o) { setHelpText (Strings::help::editorVelocity); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::ValueTree noteNear (int x) const;
    int velocityAt (int y) const;
    int barX (const juce::ValueTree&) const;
    void applyRamp (const juce::MouseEvent&);

    PianoRoll& owner;
    bool transactionOpen = false, ramp = false;
    juce::ValueTree grabbed;
    std::vector<std::pair<juce::ValueTree, int>> originals; // the selected notes and their velocities when the drag began
    int grabbedOriginal = 0;
    juce::Point<int> downPosition;
};

//==============================================================================
// The collapsible controller strip: modwheel, pitch bend or sustain of the clip as a step line over the same time axis
// as the note grid. A mouse stroke draws values; the whole stroke is one undo transaction.
class PianoRoll::ControllerStrip final : public juce::Component, public HelpProvider
{
public:
    explicit ControllerStrip (PianoRoll& o) : owner (o) { setHelpText (Strings::help::editorControllers); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::String helpAt (juce::Point<int> p) const override
    {
        return owner.v.controllersOpen && p.y >= PianoRoll::kControllerHeader && p.x >= PianoRoll::kKeysWidth ? Strings::help::controllerLane : juce::String();
    }

private:
    juce::Rectangle<int> tabArea (int i) const;
    juce::Rectangle<int> bodyArea() const; // the lane itself, aligned with the note grid
    clipedit::ControllerLane lane() const { return (clipedit::ControllerLane) juce::jlimit (0, 2, owner.v.controller); }
    void addStrokePoint (juce::Point<int>);

    PianoRoll& owner;
    bool drawing = false, transactionOpen = false;
    std::vector<daw::engine::ControllerEvent> original;
    std::vector<std::pair<double, double>> stroke;
};
} // namespace ui
