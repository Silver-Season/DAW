#pragma once
#include "Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ui::glass
{
// Background surfaces never distort musical data or labels. Small control lenses refract their own track.
void
surface (juce::Graphics&, juce::Rectangle<float>, juce::Colour, float radius);
// A flat cell (clip slot, field, meter): a soft top light only, no rim, so a grid of them stays calm.
void
cell (juce::Graphics&, juce::Rectangle<float>, juce::Colour, float radius);
// A capsule that holds a row of controls, like the transport.
void
capsule (juce::Graphics&, juce::Rectangle<float>);
// The round white knob of a switch: frosted, with a soft shadow and a bright upper rim.
void
thumb (juce::Graphics&, juce::Rectangle<float>);
void
lens (juce::Graphics&, juce::Rectangle<float>, juce::Colour track, bool vertical = false);
void
knob (juce::Graphics&, juce::Rectangle<float>, float position, juce::Colour, bool bipolar = false);
void
accent (juce::Graphics&, juce::Rectangle<float>, juce::Colour, float radius);
}
