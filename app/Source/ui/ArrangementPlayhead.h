#pragma once

#include "ArrangementView.h"

namespace ui
{
// The playhead as a light beam over the lanes (docs/spec/03-arrangement.md, "Курсор воспроизведения"): a line in the
// text colour, a glow in the accent colour when the theme glows, and six thin spectrum stripes to the right of it on
// every clip it crosses. It is its own component so that 60 repaints a second touch a strip a few pixels wide, never
// the cached lanes below it. It lets the mouse through.
class ArrangementPlayhead final : public juce::Component, private juce::Timer
{
public:
    explicit ArrangementPlayhead (ArrangementView&);
    ~ArrangementPlayhead() override;

    // x of the beam in this component, or a negative number far outside when it is not shown.
    float beamX() const;
    // The strip around x that the beam, its glow and the stripes cover.
    static juce::Rectangle<int> stripAround (float x, int height);

    void paint (juce::Graphics&) override;
    void visibilityChanged() override;

private:
    void timerCallback() override;
    void paintRecording (juce::Graphics&) const;

    ArrangementView& view;
    float lastX = -1.0e6f;
    int lastRows = -1;
    int recordingTick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangementPlayhead)
};
} // namespace ui
