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
// The ground under the glass (glass themes): the background colour with a few large, soft glows in the accent colours
// laid out over the whole window, so the see-through plates have light to let through. Paints `c`'s bounds with the
// part of the window it covers; the window, the top bar, the info bar and the card corners call it.
void windowBackdrop (juce::Graphics&, juce::Component& c);
// The colour of a plate (the first layer of glass, the most see-through): the theme's chrome, a little transparent over
// the window backdrop. Opaque in the classic look or with Reduce transparency.
juce::Colour plateColour ();

// A liquid-glass body for a raised control (the selected segment, framed buttons, thumbs, knobs): a soft shadow, a
// near-white body, a thick bright rim whose lower edge bends the light into a cyan fringe, a gloss on top and a caustic
// glow along the bottom. `round` draws a circle in r.
void bead (juce::Graphics&, juce::Rectangle<float> r, float radius, bool round = false, juce::Colour fringe = juce::Colour());
// The edge of a glass plate or card: a faint hairline and the rim catching the light (rimLight). `strength` 0..1
// scales the light (cards less than plates).
void edge (juce::Graphics&, juce::Rectangle<float> r, float radius, float strength = 1.0f);
// A translucent pane of glass under a button (the transport row): a frosted body you can see the panel through, a
// hairline and a lit top bevel; `presence` 0..1 fades the whole pane in, `lit` 0..1 adds a quiet edge and light in
// `colour` (the button is on).
void pane (juce::Graphics&, juce::Rectangle<float> r, float radius, juce::Colour colour, float lit, float presence);
// The rim of a pane of glass, where its thickness shows: two edges a few pixels apart (the outer one catching the light
// where it faces it, the inner one where the light leaves the glass) and a lighter band between them. This, not a
// single drawn outline, is what makes a pane read as glass. `strength` 0..1.
void rimLight (juce::Graphics&, juce::Rectangle<float> r, float radius, float strength = 1.0f);
// A thin line of spectrum round a rounded rectangle (the hue turns with the direction of the rim): the prism edge of
// glass that is lit or being touched. `alpha` 0 draws nothing.
void spectrumRim (juce::Graphics&, juce::Rectangle<float> r, float radius, float alpha, float width = 1.2f);
// A bead lit from inside by `colour` (play while playing, record while armed, a toggle that is on): a little light
// scattered in the glass and its edge in the colour. `strength` 0..1 (0 = the plain bead).
void litBead (juce::Graphics&, juce::Rectangle<float> r, float radius, bool round, juce::Colour colour, float strength);

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
// Parallax: how far a layer `depth` pixels deep leans toward the smoothed pointer while it is near `c` (nothing when it
// is far, so glass out of its reach rests). Layers under glass move against the glass, nearer layers more than farther
// ones, so the glass reads as floating over its content. Zero until the mouse has been seen.
juce::Point<float> parallax (juce::Component&, float depth);

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
    // `prism` scales the bend at the rim and the spectrum it splits there (1 = the study; the view switch uses more).
    void lens (juce::Rectangle<float> box, float radius, float shadow = 1.0f, float body = 0.0f, float prism = 1.0f);

private:
    struct Box
    {
        juce::Rectangle<float> r;
        float radius, shadow, prism;
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
