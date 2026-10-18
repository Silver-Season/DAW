#pragma once
#include "Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ui::glass
{
// Background surfaces never distort musical data or labels. Small control lenses refract their own track.
void
surface (juce::Graphics&, juce::Rectangle<float>, juce::Colour, float radius);
void
lens (juce::Graphics&, juce::Rectangle<float>, juce::Colour track, bool vertical = false);
void
knob (juce::Graphics&, juce::Rectangle<float>, float position, juce::Colour, bool bipolar = false);
void
accent (juce::Graphics&, juce::Rectangle<float>, juce::Colour, float radius);
}
