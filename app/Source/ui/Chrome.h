#pragma once

#include "UiCommon.h"

namespace ui
{
// Bottom line of the window: title and description of the element under the mouse, like Info View in Ableton.
// Elements publish their text through Component::setHelpText (title, newline, description) or HelpProvider.
class InfoBar final : public juce::Component, private juce::Timer
{
public:
    explicit InfoBar (juce::Component& window);
    ~InfoBar() override;

    // A message that replaces the hint for a while ("saved card not found, system default selected").
    void showMessage (const juce::String& text, int milliseconds = 5000);
    // The help text of the element at `positionInComponent` of `component` (or of its nearest parent with one).
    static juce::String helpFor (juce::Component* component, juce::Point<int> positionInComponent);
    juce::String currentText() const { return message.isNotEmpty() ? message : helpText; }

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override; // also receives the global mouse events

private:
    void timerCallback() override;

    juce::Component& window;
    juce::String helpText, message;
};

// Draggable border between two panels. The owner turns the drag into a new panel size.
class Splitter final : public juce::Component
{
public:
    static constexpr int kThickness = 5;

    // `verticalBar`: a vertical bar that changes a width (browser); otherwise a horizontal bar that changes a height.
    explicit Splitter (bool verticalBar);

    std::function<void()> onDragStart;
    std::function<void (int deltaFromStart)> onDrag; // pixels along the axis the bar moves
    std::function<void()> onDoubleClick;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (onDoubleClick)
            onDoubleClick();
    }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }

private:
    bool vertical, hover = false, dragging = false;
};

// Small triangle tab at the edge of a panel that hides and shows the panel.
class EdgeTab final : public juce::Component, public juce::TooltipClient
{
public:
    // direction: which way the triangle points while the panel is open (0 up, 1 right, 2 down, 3 left)
    explicit EdgeTab (int openDirection) : direction (openDirection) {}

    // The tooltip says what a click does: hide the panel while it is open, show it while it is hidden.
    void setTooltips (const juce::String& whenOpen, const juce::String& whenClosed)
    {
        openTip = whenOpen;
        closedTip = whenClosed;
    }
    juce::String getTooltip() override { return open ? openTip : closedTip; }

    void setOpen (bool o)
    {
        open = o;
        repaint();
    }
    std::function<void()> onClick;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains (e.getPosition()) && onClick)
            onClick();
    }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }

private:
    int direction;
    bool open = true, hover = false;
    juce::String openTip, closedTip;
};

// Dark horizontal lines over the whole window for the Neon skin. One 3-pixel tile, drawn once.
class ScanlineOverlay final : public juce::Component
{
public:
    ScanlineOverlay() { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics&) override;

private:
    juce::Image tile;
    int tileGeneration = -1;
};

// The triangle and six spectrum rays of the concept, drawn in code.
void drawLogo (juce::Graphics&, juce::Rectangle<float> area);
} // namespace ui
