#pragma once

#include "UiCommon.h"

#include <functional>

namespace ui
{
// "C minor" or "Scale: off" for a root (0..11) and a scale key ("" = off).
juce::String scaleLabel (int root, const juce::String& mode);

// The chooser of tonic and scale: twelve tonic cells and the list of scales. Every click is reported at once
// (the callout stays open), so the caller makes one undo transaction per click.
class ScalePicker final : public juce::Component, public HelpProvider
{
public:
    using Callback = std::function<void (int root, const juce::String& mode)>;

    ScalePicker (int root, const juce::String& mode, Callback onChange);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    juce::String helpAt (juce::Point<int>) const override { return {}; }

    static constexpr int kWidth = 300;

private:
    static constexpr int kPad = 10, kCell = 38, kRowHeight = 24, kGap = 4;
    juce::Rectangle<int> rootCell (int i) const;
    juce::Rectangle<int> modeCell (int i) const; // 0 = off, 1.. = scales
    int rootAt (juce::Point<int>) const;
    int modeAt (juce::Point<int>) const;
    int numModeCells() const;
    int rootsBottom() const { return kPad + 16 + 2 * (kCell / 2 + kGap) + 6; }

    int root;
    juce::String mode;
    Callback callback;
    int hoverRoot = -1, hoverMode = -1;
};

// Opens the picker in a callout under `target`.
void showScalePicker (juce::Component& target, int root, const juce::String& mode, ScalePicker::Callback onChange);
} // namespace ui
