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
class Spectrum final : public juce::Component, public HelpProvider
{
public:
    explicit Spectrum (AudioEngine&);

    void tick(); // 30 Hz from the top bar
    juce::String helpAt (juce::Point<int>) const override;
    void paint (juce::Graphics&) override;

    static constexpr int kBands = 56;

private:
    AudioEngine& engine;
    std::vector<float> samples, window;
    std::array<float, kBands> level {}, peak {};
    bool silent = true;
};
} // namespace ui
