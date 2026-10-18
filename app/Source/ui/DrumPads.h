#pragma once

#include "Controls.h"

#include <juce_gui_basics/juce_gui_basics.h>

class AudioEngine;
class DrumKitProcessor;

namespace ui
{
// The body of the drum machine in the device chain: a 4 x 4 grid of pads (C1 bottom left, like a pad controller) that
// flash when they play, and the knobs of the selected pad. A click plays the pad through the track (it is the selected
// track, so it takes live notes); a sound file dropped on a pad from the browser or Finder replaces its sample.
class DrumPads final : public juce::Component, public juce::DragAndDropTarget, public juce::FileDragAndDropTarget, public HelpProvider
{
public:
    DrumPads (AudioEngine&, DrumKitProcessor&, juce::Colour trackColour);

    static constexpr int kPreferredWidth = 520;

    void tick();
    juce::String helpAt (juce::Point<int>) const override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;

private:
    juce::Rectangle<int> padBounds (int pad) const;
    int padAt (juce::Point<int>) const;
    void selectPad (int pad);
    void dropFile (int pad, const juce::File&);
    void updateKnobs();

    AudioEngine& engine;
    DrumKitProcessor& kit;
    juce::Colour colour;
    int selected = 0, pressed = -1, hoverDrop = -1;
    std::array<float, 16> shown {};
    uint32_t seenVersion = 0;

    struct PadKnob
    {
        int param;     // daw::drums::PadParam
        Knob knob;
    };
    std::vector<std::unique_ptr<PadKnob>> knobs;
};
} // namespace ui
