#pragma once

#include "UiCommon.h"

namespace ui
{
// Bottom line of the window: title and description of the element under the mouse.
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
    // Glass themes: the device fields shown on the left ("48 kHz", "256 samples", the interface), refreshed by the window.
    void setStatus (const juce::StringArray& fields)
    {
        if (fields != status)
        {
            status = fields;
            repaint();
        }
    }

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override; // also receives the global mouse events

private:
    void timerCallback() override;

    juce::Component& window;
    juce::String helpText, message;
    juce::StringArray status;
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

// Glass themes: the main areas (browser, Session/Arrangement, bottom panel) float as rounded cards on the window
// background. The views keep painting their own rectangles; this overlay, on top of everything and transparent to the
// mouse, rounds their corners with the background colour, draws a soft shadow into the gaps and a light rim.
class CardFrames final : public juce::Component
{
public:
    CardFrames() { setInterceptsMouseClicks (false, false); }
    void setCards (std::vector<juce::Rectangle<int>> areas);
    const std::vector<juce::Rectangle<int>>& getCards() const { return cards; }
    void paint (juce::Graphics&) override;
    static constexpr float kRadius = 12.0f;

private:
    std::vector<juce::Rectangle<int>> cards;
};

// A view that is designed but not built yet: a faint picture of what it will hold, and a glass card in the middle
// with a "Coming soon" badge, a title, a sentence and one button that leads to what exists today.
class ComingSoonView final : public juce::Component
{
public:
    struct Column
    {
        juce::String name;
        juce::Colour colour;
    };
    ComingSoonView (const juce::String& title, const juce::String& text, const juce::String& action);
    std::function<std::vector<Column>()> columns; // the ghost picture: one mixer strip per column
    std::function<void (juce::Graphics&, juce::Rectangle<float>)> ghost; // or a picture of its own
    std::function<void()> onAction;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String title, text;
    juce::TextButton button;
    juce::Rectangle<int> card;
};

// The app icon (the glass tile with the prism), as the glass themes show it beside the name in the top bar.
void drawAppIcon (juce::Graphics&, juce::Rectangle<float> area);

// The triangle and six spectrum rays of the concept, drawn in code.
void drawLogo (juce::Graphics&, juce::Rectangle<float> area);
} // namespace ui
