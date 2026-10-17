#pragma once

#include "ArrangementView.h"

namespace ui
{
// The overview of the whole song at the bottom: every clip drawn small, bar numbers, the markers and the frame of
// what the lanes show. Dragging the frame scrolls; dragging its edges changes the zoom; a click elsewhere centres
// the view there.
class ArrangementMinimap final : public juce::Component, public HelpProvider
{
public:
    explicit ArrangementMinimap (ArrangementView&);

    // The horizontal mapping of the song onto this component.
    double beatToX (double beat) const;
    double xToBeat (double x) const;
    // The frame of the visible part, in this component's coordinates.
    juce::Rectangle<float> frameBounds() const;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    juce::String helpAt (juce::Point<int>) const override;

private:
    enum class Gesture
    {
        none,
        scroll,
        zoomLeft,
        zoomRight
    };
    static constexpr int kPad = 10, kTop = 12, kBottom = 10; // the tab that hides the detail panel sits in the bottom padding

    ArrangementView& view;
    Gesture gesture = Gesture::none;
    double grabOffset = 0.0, fixedBeat = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangementMinimap)
};
} // namespace ui
