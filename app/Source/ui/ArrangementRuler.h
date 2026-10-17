#pragma once

#include "ArrangementView.h"

namespace ui
{
// Above the lanes: the marker lane, the loop brace and the bar ruler (all on the time axis of the view).
// A click on the bars sets the play start and jumps there, a drag selects time on every track. The brace sets the
// loop of the arrangement (click toggles it, its ends and the whole brace are dragged). Markers are dragged, renamed
// by double-click, and a click on one jumps to it.
class ArrangementRuler final : public juce::Component, public HelpProvider
{
public:
    explicit ArrangementRuler (ArrangementView&);

    enum class Zone
    {
        markers,
        brace,
        bars
    };
    static Zone zoneAt (int y)
    {
        if (y < ArrangementView::kMarkerLaneHeight)
            return Zone::markers;
        return y < ArrangementView::kMarkerLaneHeight + ArrangementView::kBraceHeight ? Zone::brace : Zone::bars;
    }

    // The label rectangle of a marker, in this component's coordinates (empty if it is not a marker).
    juce::Rectangle<int> markerBounds (int64_t markerId) const;
    int64_t markerAt (juce::Point<int>) const;
    void beginRenameMarker (int64_t markerId);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;
    juce::String helpAt (juce::Point<int>) const override;

private:
    enum class Gesture
    {
        none,
        marker,
        braceStart,
        braceEnd,
        braceMove,
        seek
    };

    void paintBars (juce::Graphics&, juce::Rectangle<int> area) const;
    void paintBrace (juce::Graphics&) const;
    void paintMarkers (juce::Graphics&) const;
    void showMarkerMenu (juce::Point<int> screenPosition, int64_t markerId);
    void finishRename (const juce::String& text, int64_t markerId);
    int braceEdgeAt (int x) const; // 0 none, 1 start, 2 end, 3 inside

    ArrangementView& view;
    Gesture gesture = Gesture::none;
    int64_t pressedMarker = -1;
    double pressBeat = 0.0, anchorBeat = 0.0, markerStartBeat = 0.0;
    double braceStartAtPress = 0.0, braceEndAtPress = 0.0;
    bool moved = false, transactionOpen = false;
    std::unique_ptr<InlineEditor> renameEditor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangementRuler)
};
} // namespace ui
