#pragma once
#include "Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ui::glass
{
// Background surfaces never distort musical data or labels. Small control lenses refract their own track.
void
surface (juce::Graphics&, juce::Rectangle<float>, juce::Colour, float radius);
// Real glass: what lies behind a glass panel, painted from `source` (a sibling layer, e.g. the clip grid under the
// mixer) for `area` in the source's coordinates, at `scale` and blurred. Painting the source never repaints the glass,
// and a glass panel painted during a capture skips its own capture (no recursion).
juce::Image backdrop (juce::Component& source, juce::Rectangle<int> area, float scale = 0.25f, int blurRadius = 3);
bool capturing ();
// Draws a glass pane over `r` from a backdrop image (scaled to fill it): the blurred content, magnified a little toward
// the centre so the rim bends it, a tint of the panel colour, the gloss and the rim. Falls back to `surface` when the
// material is off (reduce transparency) or the image is empty.
void frosted (juce::Graphics&, juce::Rectangle<float> r, float radius, const juce::Image& behind, juce::Colour tint);

// A liquid-glass body for a raised control (the selected segment, framed buttons, thumbs, knobs): a soft shadow, a
// near-white body, a thick bright rim whose lower edge bends the light into a cyan fringe, a gloss on top and a caustic
// glow along the bottom. `round` draws a circle in r.
void bead (juce::Graphics&, juce::Rectangle<float> r, float radius, bool round = false, juce::Colour fringe = juce::Colour());

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

namespace ui::glass
{
// Liquid glass, the optics of the design study (media/design/glass-optics): whatever is painted into the backdrop is
// shown through clear lenses that bend it at the rim, magnify it a little inside, part its colours at the edge into a
// spectrum, light the rim and cast a soft shadow. Paint the content under the glass into backdrop(), add the lenses,
// and the destructor puts the result into the target. Draw labels and icons afterwards, straight into the target, so
// they stay sharp. Classic themes (and Reduce transparency) get the backdrop as painted plus a plain glass body.
// The light of the glass follows the mouse, as in the optics study: a component that paints lenses opens a Lit scope
// in its paint, the lenses then take their light from the direction of the pointer (smoothed, a little behind it), and
// the component is repainted while the pointer moves near it.
class Lit
{
public:
    explicit Lit (juce::Component&);
    ~Lit ();
    JUCE_DECLARE_NON_COPYABLE (Lit)
};

class Liquid
{
public:
    // `area` in the target's coordinates; `behind`: a colour to fill it with first, or transparent to keep what the
    // target has there (the lens then bends only what is painted into the backdrop and lights its own rim).
    Liquid (juce::Graphics& target, juce::Rectangle<int> area, juce::Colour behind = juce::Colours::transparentBlack);
    ~Liquid ();
    juce::Graphics& backdrop () { return *graphics; }
    // A lens: a rounded rectangle (radius 0..half the short side; half makes a capsule or, for a square, a circle).
    // `shadow` scales the soft shadow it casts (0 where the shadow would be cut by the edge of the component).
    // `body` paints a pale fill under the lens first (0 = perfectly clear): small controls need a body to read as solid.
    void lens (juce::Rectangle<float> box, float radius, float shadow = 1.0f, float body = 0.0f);

private:
    struct Box
    {
        juce::Rectangle<float> r;
        float radius, shadow;
    };
    juce::Graphics& target;
    juce::Rectangle<int> area;
    float scale = 1.0f;
    juce::Image image;
    std::unique_ptr<juce::Graphics> graphics;
    std::vector<Box> boxes;
    void compose ();
    JUCE_DECLARE_NON_COPYABLE (Liquid)
};
} // namespace ui::glass
