#pragma once

#include "ArrangementView.h"

namespace ui
{
class MiniButton;

// The track headers on the left of the lanes: colour, name, on / S / record, volume, pan. They are the tracks of the
// session (the same model): a change here shows in the Session View and the other way round. The bottom edge of a
// header is dragged to change the height of the lane.
class ArrangementHeaders final : public juce::Component
{
public:
    explicit ArrangementHeaders (ArrangementView&);
    ~ArrangementHeaders() override;

    // Creates and places one header per lane (after the tracks, the heights or the vertical scroll changed).
    void refreshRows();
    // The numbers and lamps from the model (30 Hz).
    void refreshValues();
    void beginRename (int64_t trackId);
    int numRows() const;
    // The bounds of a header in this component's coordinates (empty if the track has none).
    juce::Rectangle<int> rowBounds (int64_t trackId) const;

    void paint (juce::Graphics&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    class Row;

private:
    ArrangementView& view;
    std::vector<std::unique_ptr<Row>> rowComponents;
    std::unique_ptr<MiniButton> addMidi, addAudio; // under the last track

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangementHeaders)
};

// The corner above the headers: "+ Marker", previous and next marker, the list of markers, the grid and the lane height mode.
class ArrangementCorner final : public juce::Component, public HelpProvider
{
public:
    explicit ArrangementCorner (ArrangementView&);
    ~ArrangementCorner() override;

    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::String helpAt (juce::Point<int>) const override;

private:
    ArrangementView& view;
    std::unique_ptr<MiniButton> addMarker, previous, next, list, heightChip;
    juce::String shownGrid;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangementCorner)
};
} // namespace ui
