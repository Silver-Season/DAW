#pragma once

#include "UiCommon.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <vector>

class AudioEngine;

namespace ui
{
// The spectrum of the master in the top bar: what the prism of the logo does to light, done to the sound. A smooth
// curve over a log frequency axis (30 Hz .. 18 kHz), filled with the colours of the theme's palette from bass to air,
// rising fast and falling slowly, with a thin line of the recent peaks. Repaints only while something sounds.
// Glass themes put a lens over it: the spectrum is drawn into an image and the lens refracts that image, magnified in
// the middle and bent at the rim, with a little colour dispersion at the edge. The lens can be dragged along.
class Spectrum final : public juce::Component, public HelpProvider
{
public:
    explicit Spectrum (AudioEngine&);

    void tick(); // 30 Hz from the top bar
    juce::String helpAt (juce::Point<int>) const override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    // Where the lens is, in component coordinates (empty outside the glass themes).
    juce::Rectangle<float> lensBounds() const;

    static constexpr int kBands = 56;

private:
    AudioEngine& engine;
    std::vector<float> samples, window;
    std::array<float, kBands> level {}, peak {};
    bool silent = true;

    static constexpr float kLensHome = 0.62f;
    float lensPosition = kLensHome; // centre of the lens, 0..1 across the width
    float dragOffset = 0.0f;
    void paintContent (juce::Graphics&);
};
} // namespace ui
