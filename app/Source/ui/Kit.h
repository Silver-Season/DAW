#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

// The painting of every control kind in every state (docs/design/UI-KIT.md). Components keep their behaviour and call
// these to draw, so a change of style is made here once. Sizes come from theme().metrics, colours from theme().
namespace ui::kit
{
struct State
{
    bool hover = false, down = false, on = false, focus = false, enabled = true;
};

enum class Kind
{
    plain,   // a framed button
    ghost,   // only its label until hovered; lit: a soft tint (the switches of the transport capsule)
    toggle,  // lit: filled with its colour
    primary, // the main action of a dialog: always filled with the accent
    danger,  // lit: filled with the record colour
    play     // the round transport play button
};

// The corner radius of a control of this size (fully round in themes with pill controls).
float controlRadius (juce::Rectangle<float>);
// Paints a button's background and returns the colour for its label and icon. `colour` overrides the lit colour.
juce::Colour button (juce::Graphics&, juce::Rectangle<float>, Kind, State, std::optional<juce::Colour> colour = std::nullopt);
// A label centred in a button (or from the left edge), with an optional icon before it.
using Icon = std::function<juce::Path (juce::Rectangle<float>)>;
struct LabelStyle
{
    float size = 0.0f; // 0: theme().metrics.text
    int weight = 500;
    float iconSize = 11.0f;
    std::optional<juce::Colour> iconColour; // the icon in its own colour (the red dot of Capture)
    bool leftAligned = false, chevron = false;
};
void label (juce::Graphics&, juce::Rectangle<float>, const juce::String&, juce::Colour ink, const Icon& icon = {}, LabelStyle = {});
// A switch: its track and thumb inside `area` (right-aligned), label not included.
void switchControl (juce::Graphics&, juce::Rectangle<float> area, State);
// A segmented control (view switch, editor modes). `drop`: a segment highlighted while something is dragged over it.
// Glass: `position` puts the lens between segments while it slides (a fractional index; negative = on `selected`), and
// `flare` (0..1) stretches it and brightens its spectrum while it moves.
void segmented (juce::Graphics&, juce::Rectangle<float>, const juce::StringArray& labels, int selected, int hover = -1, int drop = -1,
                float position = -1.0f, float flare = 0.0f);
int segmentAt (juce::Rectangle<float>, int count, float x);
// The background of a text field or a combo box.
void field (juce::Graphics&, juce::Rectangle<float>, State);
// A card or panel; `selected` outlines it in the accent.
void card (juce::Graphics&, juce::Rectangle<float>, bool selected = false);
// A row of a list (browser, settings tabs, scenes): selected and hovered highlights.
void row (juce::Graphics&, juce::Rectangle<float>, State);
// The keyboard focus ring around a control.
void focusRing (juce::Graphics&, juce::Rectangle<float>, float radius);
} // namespace ui::kit
