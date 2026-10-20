#include "GlassMaterial.h"
#include <cmath>
#include <map>
#include <tuple>
#include <vector>
#include <optional>
#include <algorithm>

namespace ui::glass
{
namespace
{
int captureDepth = 0; // message thread only

// Three passes of a box blur in each direction approximate a gaussian; the image is small (a quarter scale or less).
void boxBlur (juce::Image& image, int radius)
{
    if (radius <= 0 || ! image.isValid())
        return;
    const int w = image.getWidth(), h = image.getHeight();
    juce::Image::BitmapData data (image, juce::Image::BitmapData::readWrite);
    std::vector<juce::PixelARGB> line ((size_t) juce::jmax (w, h));
    const auto pass = [&] (bool horizontal)
    {
        const int length = horizontal ? w : h, lines = horizontal ? h : w;
        for (int l = 0; l < lines; ++l)
        {
            for (int i = 0; i < length; ++i)
            {
                const auto c = horizontal ? data.getPixelColour (i, l) : data.getPixelColour (l, i);
                line[(size_t) i] = c.getPixelARGB();
            }
            int sum[4] { 0, 0, 0, 0 };
            const auto add = [&] (int i, int sign) {
                const auto& p = line[(size_t) juce::jlimit (0, length - 1, i)];
                sum[0] += sign * p.getAlpha(); sum[1] += sign * p.getRed(); sum[2] += sign * p.getGreen(); sum[3] += sign * p.getBlue();
            };
            for (int i = -radius; i <= radius; ++i)
                add (i, 1);
            const int n = 2 * radius + 1;
            for (int i = 0; i < length; ++i)
            {
                const auto c = juce::Colour ((juce::uint8) (sum[1] / n), (juce::uint8) (sum[2] / n), (juce::uint8) (sum[3] / n), (juce::uint8) (sum[0] / n));
                horizontal ? data.setPixelColour (i, l, c) : data.setPixelColour (l, i, c);
                add (i + radius + 1, 1);
                add (i - radius, -1);
            }
        }
    };
    for (int k = 0; k < 3; ++k)
    {
        pass (true);
        pass (false);
    }
}
} // namespace

bool capturing ()
{
    return captureDepth > 0;
}

juce::Image backdrop (juce::Component& source, juce::Rectangle<int> area, float scale, int blurRadius)
{
    area = area.getIntersection (source.getLocalBounds ());
    if (area.isEmpty () || captureDepth > 0)
        return {};
    const int w = juce::jmax (1, juce::roundToInt ((float) area.getWidth () * scale));
    const int h = juce::jmax (1, juce::roundToInt ((float) area.getHeight () * scale));
    juce::Image image (juce::Image::ARGB, w, h, true, juce::SoftwareImageType ());
    {
        ++captureDepth;
        juce::Graphics g (image);
        g.fillAll (theme ().chrome); // opaque: a blur over transparent pixels would darken the edges
        g.addTransform (juce::AffineTransform::translation ((float) -area.getX (), (float) -area.getY ()).scaled (scale));
        source.paintEntireComponent (g, true);
        --captureDepth;
    }
    boxBlur (image, blurRadius);
    return image;
}

void windowBackdrop (juce::Graphics& g, juce::Component& c)
{
    const auto& t = theme ();
    g.fillAll (t.background);
    if (!t.glass || t.material <= 0.0f)
        return;
    auto* top = c.getTopLevelComponent ();
    const auto window = (top != nullptr ? top->getLocalBounds () : c.getLocalBounds ()).toFloat ();
    const auto origin = top != nullptr && top != &c ? top->getLocalPoint (&c, juce::Point<int> ()).toFloat () : juce::Point<float> ();
    const float w = window.getWidth (), h = window.getHeight (), reach = juce::jmax (w, h);
    const float strength = t.material * (t.isLight () ? 0.55f : 0.12f);
    // where each glow sits (fractions of the window), its radius (fraction of the long side) and its colour
    const std::tuple<float, float, float, juce::Colour> glows[] = {
        { 0.08f, 0.10f, 0.60f, t.glassTint },
        { 0.94f, 0.25f, 0.55f, t.glassTint2 },
        { 0.55f, 1.00f, 0.60f, t.glassTint2 },
    };
    for (const auto& [x, y, radius, colour] : glows)
    {
        const juce::Point<float> centre (x * w - origin.x, y * h - origin.y);
        juce::ColourGradient glow (colour.withAlpha (strength), centre, colour.withAlpha (0.0f), centre.translated (radius * reach, 0.0f), true);
        glow.addColour (0.5, colour.withAlpha (strength * 0.35f));
        g.setGradientFill (glow);
        g.fillRect (c.getLocalBounds ());
    }
}

juce::Colour plateColour ()
{
    const auto& t = theme ();
    if (!t.glass || t.material <= 0.0f)
        return t.chrome;
    return t.chrome.withAlpha ((t.isLight () ? 0.52f : 0.82f) + 0.18f * (1.0f - t.material));
}

void frosted (juce::Graphics& g, juce::Rectangle<float> r, float radius, const juce::Image& behind, juce::Colour tint)
{
    const auto& t = theme ();
    if (!t.glass || t.material <= 0.0f || !behind.isValid ())
    {
        surface (g, r, tint, radius);
        return;
    }
    const float strength = t.material;
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path shape;
        shape.addRoundedRectangle (r, radius);
        g.reduceClipRegion (shape);
        g.fillAll (t.background);
        // the content, blurred, magnified a little about the centre: the rim seems to bend it
        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
        g.setOpacity (1.0f);
        g.drawImage (behind, r.expanded (r.getWidth () * 0.02f, r.getHeight () * 0.04f), juce::RectanglePlacement::stretchToFit);
        // the frost: the panel colour over it, lighter in light themes
        g.setColour (tint.withAlpha ((t.isLight () ? 0.74f : 0.78f) - 0.2f * strength));
        g.fillRect (r);
        // the gloss band and a soft vignette at the rim, where real glass is thicker
        const auto gloss = r.reduced (1.5f).withHeight (r.getHeight () * 0.42f);
        g.setGradientFill (juce::ColourGradient (t.highlight ().withAlpha ((t.isLight () ? 0.55f : 0.10f) * strength), 0.0f, gloss.getY (),
                                                 t.highlight ().withAlpha (0.0f), 0.0f, gloss.getBottom (), false));
        g.fillRect (gloss);
    }
    g.setGradientFill (juce::ColourGradient (t.highlight ().withAlpha ((t.isLight () ? 0.95f : 0.25f) * strength), 0.0f, r.getY (),
                                             t.highlight ().withAlpha ((t.isLight () ? 0.3f : 0.04f) * strength), 0.0f, r.getBottom (), false));
    g.drawRoundedRectangle (r.reduced (1.0f), juce::jmax (0.0f, radius - 1.0f), 1.2f);
    g.setColour (t.line.withAlpha (t.isLight () ? 0.6f : 0.7f));
    g.drawRoundedRectangle (r.reduced (0.25f), radius, 0.8f);
}

void
accent (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour, float radius)
{
    const auto& t = theme ();
    if (t.gradients)
    {
        const float angle = juce::degreesToRadians (t.gradientAngle);
        const auto delta =
          juce::Point<float> (std::cos (angle) * r.getWidth () * 0.5f, std::sin (angle) * r.getHeight () * 0.5f);
        auto second = colour == t.accentFill || colour == t.accent ? t.accentFill2
                                                                   : colour.interpolatedWith (t.highlight (), 0.20f);
        const auto ink = t.inkOn (colour);
        const auto requested = second;
        for (int i = 0; i <= 100 && Theme::contrast (ink, second) < 4.5f; ++i)
            second = requested.interpolatedWith (colour, (float)i / 100.0f);
        g.setGradientFill (
          juce::ColourGradient (colour, r.getCentre () - delta, second, r.getCentre () + delta, false));
    }
    else
        g.setColour (colour);
    g.fillRoundedRectangle (r, radius);
}

void
surface (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour base, float radius)
{
    // An even satin body lets the ambient ground participate. A single shared rim supplies its depth, so cards do
    // not accumulate an opaque gloss band and several nested outlines.
    const auto& t = theme ();
    if (!t.glass || t.material <= 0.0f)
    {
        g.setColour (base);
        g.fillRoundedRectangle (r, radius);
        return;
    }
    const float strength = t.material;
    const float opacity = (t.isLight () ? 0.62f : 0.92f) + (t.isLight () ? 0.25f : 0.08f) * (1.0f - strength);
    const auto top = base.interpolatedWith (t.highlight (), (t.isLight () ? 0.16f : 0.035f) * strength).withAlpha (opacity);
    const auto bottom = base.interpolatedWith (t.panel2, 0.22f * strength).withAlpha (opacity);
    g.setGradientFill (juce::ColourGradient (top, 0.0f, r.getY (), bottom, 0.0f, r.getBottom (), false));
    g.fillRoundedRectangle (r, radius);
    if (r.getHeight () >= 14.0f)
    {
        const auto gloss = r.reduced (1.5f).withHeight (r.getHeight () * 0.30f);
        g.setGradientFill (juce::ColourGradient (t.highlight ().withAlpha ((t.isLight () ? 0.16f : 0.025f) * strength), 0.0f, gloss.getY (),
                                                 t.highlight ().withAlpha (0.0f), 0.0f, gloss.getBottom (), false));
        g.fillRoundedRectangle (gloss, juce::jmax (0.0f, juce::jmin (radius - 1.5f, gloss.getHeight ())));
    }
    edge (g, r, radius, 0.75f);
}

void
bead (juce::Graphics& g, juce::Rectangle<float> r, float radius, bool round, juce::Colour fringe)
{
    // Clear glass over the ground, as in the references: a body only a little lighter than what is under it, a thin
    // rim that catches the light at the top and, more faintly, at the bottom where it leaves the glass, and only a
    // breath of shadow (a heavy shadow under small buttons reads as dirt, above all on dark ground).
    juce::ignoreUnused (fringe);
    const auto& t = theme ();
    if (round)
        radius = juce::jmin (r.getWidth (), r.getHeight ()) * 0.5f;
    radius = juce::jmin (radius, juce::jmin (r.getWidth (), r.getHeight ()) * 0.5f);
    const float k = t.material > 0.0f ? t.material : 0.0f;
    const bool light = t.isLight ();
    const auto shape = [&] (juce::Rectangle<float> b, float rad) {
        juce::Path p;
        if (round)
            p.addEllipse (b);
        else
            p.addRoundedRectangle (b, rad);
        return p;
    };
    // No shadow outside the shape: controls fill their components, and anything painted past r would be cut square at
    // the component's edge. Depth comes from the rim instead.
    const auto top = light ? t.panel.interpolatedWith (t.highlight (), 0.25f) : t.panel2.interpolatedWith (t.highlight (), 0.035f);
    const auto bottom = light ? t.panel.interpolatedWith (t.panel2, 0.40f) : t.panel2.interpolatedWith (t.panel, 0.40f);
    g.setGradientFill (juce::ColourGradient (top, 0.0f, r.getY (), bottom, 0.0f, r.getBottom (), false));
    g.fillPath (shape (r, radius));
    if (k > 0.0f && ! round)
        rimLight (g, r, radius, 0.55f);
    else if (k > 0.0f)
    {
        juce::ColourGradient rim (t.highlight ().withAlpha ((light ? 0.70f : 0.26f) * k), 0.0f, r.getY (),
                                  t.highlight ().withAlpha ((light ? 0.22f : 0.06f) * k), 0.0f, r.getBottom (), false);
        rim.addColour (0.5, t.highlight ().withAlpha ((light ? 0.08f : 0.02f) * k));
        g.setGradientFill (rim);
        g.strokePath (shape (r.reduced (0.9f), juce::jmax (0.0f, radius - 0.9f)), juce::PathStrokeType (1.1f));
    }
    // a hairline outside, so the bead holds its shape on light ground and stays apart from dark ground
    g.setColour (light ? t.line.withAlpha (0.24f) : t.shadow ().withAlpha (0.22f));
    g.strokePath (shape (r.reduced (0.25f), radius), juce::PathStrokeType (0.75f));
}

void
edge (juce::Graphics& g, juce::Rectangle<float> r, float radius, float strength)
{
    const auto& t = theme ();
    if (!t.glass || r.isEmpty ())
        return;
    const bool light = t.isLight ();
    const float k = (t.material > 0.0f ? t.material : 0.0f) * strength;
    radius = juce::jmin (radius, juce::jmin (r.getWidth (), r.getHeight ()) * 0.5f);
    // a quiet hairline, then the rim catching the light as on the lenses (no drawn bevel line)
    g.setColour (light ? t.line.withAlpha (0.22f) : t.highlight ().withAlpha (0.035f));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    if (k > 0.0f)
        rimLight (g, r, radius, 0.7f * strength);
}

void
pane (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour colour, float lit, float presence)
{
    const auto& t = theme ();
    presence = juce::jlimit (0.0f, 1.0f, juce::jmax (presence, lit));
    lit = juce::jlimit (0.0f, 1.0f, lit);
    if (presence <= 0.0f || r.isEmpty ())
        return;
    const bool light = t.isLight ();
    radius = juce::jmin (radius, juce::jmin (r.getWidth (), r.getHeight ()) * 0.5f);
    juce::Path shape;
    shape.addRoundedRectangle (r, radius);
    if (t.material <= 0.0f)
    {
        g.setColour (t.panel);
        g.fillPath (shape);
        g.setColour (lit > 0.0f ? colour : t.line);
        g.strokePath (shape, juce::PathStrokeType (1.0f));
        return;
    }
    // the frosted body: the panel shows through it
    // a control is a layer above its card, so it is more matte than the card: less of what is under it shows through
    const auto body = light ? t.panel : t.highlight ();
    g.setGradientFill (juce::ColourGradient (body.withAlpha ((light ? 0.56f : 0.065f) * presence), 0.0f, r.getY (),
                                             body.withAlpha ((light ? 0.40f : 0.025f) * presence), 0.0f, r.getBottom (), false));
    g.fillPath (shape);
    if (lit > 0.0f)
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (shape);
        juce::ColourGradient glow (colour.withAlpha ((light ? 0.18f : 0.24f) * lit), r.getCentreX (), r.getBottom () - r.getHeight () * 0.15f,
                                   colour.withAlpha (0.0f), r.getCentreX (), r.getY () - r.getHeight () * 0.3f, true);
        g.setGradientFill (glow);
        g.fillRect (r);
    }
    // the hairline round it and the bevel lit along the top
    g.setColour ((light ? t.line.withAlpha (0.28f) : t.highlight ().withAlpha (0.065f)).withMultipliedAlpha (presence));
    g.strokePath (shape, juce::PathStrokeType (1.0f));
    rimLight (g, r, radius, presence * 0.55f);
    if (lit > 0.0f)
    {
        g.setColour (colour.withAlpha ((light ? 0.8f : 0.7f) * lit));
        g.strokePath (shape, juce::PathStrokeType (1.2f));
    }
}

void
rimLight (juce::Graphics& g, juce::Rectangle<float> r, float radius, float strength)
{
    const auto& t = theme ();
    const float k = (t.material > 0.0f ? t.material : 0.0f) * strength;
    if (k <= 0.0f || r.getWidth () < 4.0f || r.getHeight () < 4.0f)
        return;
    const bool light = t.isLight ();
    radius = juce::jmin (radius, juce::jmin (r.getWidth (), r.getHeight ()) * 0.5f);
    const juce::Point<float> from { -0.45f, -0.89f }; // the resting light, as on the lenses
    const auto c = r.getCentre ();
    const float hx = r.getWidth () * 0.5f - radius, hy = r.getHeight () * 0.5f - radius;
    const auto rimAt = [&] (float inset, float width, float base, float main, float back)
    {
        juce::Path p;
        p.addRoundedRectangle (r.reduced (inset), juce::jmax (0.0f, radius - inset));
        juce::PathFlatteningIterator it (p, {}, 0.4f);
        while (it.next ())
        {
            // the normal of the rim here: from the nearest point of the inner box to this point
            const float mx = (it.x1 + it.x2) * 0.5f - c.x, my = (it.y1 + it.y2) * 0.5f - c.y;
            const float nx0 = mx - juce::jlimit (-hx, hx, mx), ny0 = my - juce::jlimit (-hy, hy, my);
            const float nl = std::sqrt (nx0 * nx0 + ny0 * ny0);
            const float facing = nl < 0.001f ? 0.0f : (nx0 * from.x + ny0 * from.y) / nl;
            const float a = base + std::pow (juce::jmax (facing, 0.0f), 2.2f) * main + std::pow (juce::jmax (-facing, 0.0f), 3.0f) * back;
            if (a < 0.01f)
                continue;
            g.setColour (t.highlight ().withAlpha (juce::jmin (1.0f, a * k)));
            g.drawLine (it.x1, it.y1, it.x2, it.y2, width);
        }
    };
    // the thickness of the glass: a band between the outer edge and an inner one a few pixels in, lighter at the top
    const float thick = juce::jlimit (1.2f, 2.6f, juce::jmin (r.getWidth (), r.getHeight ()) * 0.045f);
    {
        juce::Path band;
        band.addRoundedRectangle (r.reduced (0.5f), radius);
        band.addRoundedRectangle (r.reduced (thick), juce::jmax (0.0f, radius - thick));
        band.setUsingNonZeroWinding (false);
        g.setGradientFill (juce::ColourGradient (t.highlight ().withAlpha ((light ? 0.15f : 0.025f) * k), 0.0f, r.getY (),
                                                 t.highlight ().withAlpha ((light ? 0.025f : 0.005f) * k), 0.0f, r.getBottom (), false));
        g.fillPath (band);
    }
    // the outer edge catches the light where it faces it; the inner edge, where the light leaves the glass
    rimAt (0.65f, 0.9f, light ? 0.10f : 0.025f, light ? 0.72f : 0.25f, light ? 0.12f : 0.045f);
    rimAt (thick, 0.65f, light ? 0.025f : 0.008f, light ? 0.12f : 0.035f, light ? 0.25f : 0.08f);
}

void
spectrumRim (juce::Graphics& g, juce::Rectangle<float> r, float radius, float alpha, float width)
{
    if (alpha <= 0.0f || r.isEmpty ())
        return;
    const auto& t = theme ();
    radius = juce::jmin (radius, juce::jmin (r.getWidth (), r.getHeight ()) * 0.5f);
    juce::Path p;
    p.addRoundedRectangle (r.reduced (width * 0.5f), juce::jmax (0.0f, radius - width * 0.5f));
    const auto c = r.getCentre ();
    juce::PathFlatteningIterator it (p, {}, 0.5f);
    while (it.next ())
    {
        const float mx = (it.x1 + it.x2) * 0.5f - c.x, my = (it.y1 + it.y2) * 0.5f - c.y;
        // the hue turns with the direction of the rim, the same order as the prism lens of the view switch
        const float hue = std::fmod (std::atan2 (my, mx) / juce::MathConstants<float>::twoPi + 1.25f, 1.0f);
        g.setColour (juce::Colour::fromHSV (hue, t.isLight () ? 0.85f : 0.75f, 1.0f, alpha));
        g.drawLine (it.x1, it.y1, it.x2, it.y2, width);
    }
}

void
litBead (juce::Graphics& g, juce::Rectangle<float> r, float radius, bool round, juce::Colour colour, float strength)
{
    // Glass lit from inside, quietly: the clear bead with a little of the light of `colour` scattered in it and its edge
    // drawn in that colour.
    const auto& t = theme ();
    if (round)
        radius = juce::jmin (r.getWidth (), r.getHeight ()) * 0.5f;
    radius = juce::jmin (radius, juce::jmin (r.getWidth (), r.getHeight ()) * 0.5f);
    strength = juce::jlimit (0.0f, 1.0f, strength);
    juce::Path shape;
    if (round)
        shape.addEllipse (r);
    else
        shape.addRoundedRectangle (r, radius);
    // the light stays inside the glass (a glow outside would be cut square at the edge of the component)
    bead (g, r, radius, round);
    if (strength <= 0.0f)
        return;
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (shape);
        // a soft light scattered in the glass, strongest low in it, and gathered along the rim inside
        juce::ColourGradient glow (colour.withAlpha ((t.isLight () ? 0.20f : 0.26f) * strength), r.getCentreX (), r.getBottom () - r.getHeight () * 0.2f,
                                   colour.withAlpha (0.0f), r.getCentreX (), r.getY () - r.getHeight () * 0.2f, true);
        g.setGradientFill (glow);
        g.fillRect (r);
        g.setColour (colour.withAlpha (0.16f * strength));
        g.strokePath (shape, juce::PathStrokeType (juce::jlimit (2.0f, 4.0f, juce::jmin (r.getWidth (), r.getHeight ()) * 0.12f)));
    }
    // the edge in the colour: the clearest sign that it is on, and still quiet
    g.setColour (colour.withAlpha ((t.isLight () ? 0.75f : 0.65f) * strength));
    g.strokePath (shape, juce::PathStrokeType (1.2f));
}

namespace
{
// A small tile of fine grain, the frost of matte glass (made once; luminance noise around mid grey).
const juce::Image& frostTile ()
{
    static const juce::Image tile = [] {
        juce::Image image (juce::Image::ARGB, 96, 96, true, juce::SoftwareImageType ());
        juce::Random random (0x9a55);
        for (int y = 0; y < image.getHeight (); ++y)
            for (int x = 0; x < image.getWidth (); ++x)
            {
                const bool lighter = random.nextBool ();
                const auto level = (juce::uint8) (lighter ? 255 : 0);
                image.setPixelAt (x, y, juce::Colour (level, level, level, (juce::uint8) random.nextInt (22)));
            }
        return image;
    } ();
    return tile;
}
} // namespace

void
cell (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour base, float radius)
{
    // Matte glass (clip slots, clips, fields): the colour seen through frosted glass, a fine grain, light along the top
    // edge and a little shade along the bottom one, so each cell reads as a flat pane rather than a painted box.
    const auto& t = theme ();
    if (!t.glass || t.material <= 0.0f || base.getAlpha () < 255)
    {
        g.setColour (base);
        g.fillRoundedRectangle (r, radius);
        return;
    }
    const bool light = t.isLight ();
    const float k = t.material;
    const auto top = base.interpolatedWith (t.highlight (), (light ? 0.22f : 0.06f) * k);
    const auto bottom = base.interpolatedWith (t.shadow (), (light ? 0.03f : 0.10f) * k);
    g.setGradientFill (juce::ColourGradient (top, 0.0f, r.getY (), bottom, 0.0f, r.getBottom (), false));
    g.fillRoundedRectangle (r, radius);
    if (r.getWidth () < 6.0f || r.getHeight () < 6.0f)
        return;
    g.setTiledImageFill (frostTile (), 0, 0, (light ? 0.3f : 0.32f) * k);
    g.fillRoundedRectangle (r, radius);
    juce::ColourGradient edge (t.highlight ().withAlpha ((light ? 0.55f : 0.12f) * k), 0.0f, r.getY (),
                               t.shadow ().withAlpha ((light ? 0.05f : 0.25f) * k), 0.0f, r.getBottom (), false);
    edge.addColour (0.35, t.highlight ().withAlpha (0.0f));
    edge.addColour (0.75, t.shadow ().withAlpha (0.0f));
    g.setGradientFill (edge);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

void
capsule (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& t = theme ();
    const float radius = juce::jmin (r.getHeight () * 0.5f, t.metrics.panelRadius + 2.0f); // a rounded bar, like the cards
    if (!t.glass || t.material <= 0.0f)
    {
        g.setColour (t.panel);
        g.fillRoundedRectangle (r, radius);
        g.setColour (t.line);
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
        return;
    }
    // Keep the shadow outside the translucent body, so it cannot muddy the material from underneath.
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path outside, shape;
        shape.addRoundedRectangle (r, radius);
        outside.addRectangle (r.expanded (12.0f));
        outside.addPath (shape);
        outside.setUsingNonZeroWinding (false);
        g.reduceClipRegion (outside);
        juce::DropShadow (t.shadow ().withAlpha (t.isLight () ? 0.045f : 0.18f), 6, { 0, 2 }).drawForPath (g, shape);
    }
    surface (g, r, t.chrome, radius);
}

void
thumb (juce::Graphics& g, juce::Rectangle<float> r)
{
    // a small glass bead
    bead (g, r, juce::jmin (r.getWidth (), r.getHeight ()) * 0.5f, std::abs (r.getWidth () - r.getHeight ()) < 0.5f);
}

void
lens (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour track, bool vertical)
{
    const auto& t = theme ();
    if (!t.glass || t.material <= 0.0f)
    {
        g.setColour (t.text);
        g.fillRoundedRectangle (r, r.getHeight () * 0.5f);
        return;
    }
    // Compact CPU analogue of the verified WebGL model. Refract an analytic copy of the control's track,
    // never capture a parent component during paint. Cache is bounded and belongs to the UI thread only.
    using Key = std::tuple<int, int, int, juce::uint32, bool>;
    static std::map<Key, juce::Image> cache;
    const int w = juce::jlimit (2, 160, juce::roundToInt (r.getWidth () * 2.0f));
    const int h = juce::jlimit (2, 160, juce::roundToInt (r.getHeight () * 2.0f));
    const Key key{ ThemeManager::get ().generation (), w, h, track.getARGB (), vertical };
    auto it = cache.find (key);
    if (it == cache.end ())
    {
        if (cache.size () >= 96)
            cache.clear ();
        juce::Image image (juce::Image::ARGB, w, h, true);
        juce::Image::BitmapData pixels (image, juce::Image::BitmapData::writeOnly);
        const float hx = (float)w * 0.5f - 1.0f, hy = (float)h * 0.5f - 1.0f;
        const float radius = juce::jmin (hx, hy);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const float px = (float)x + 0.5f - (float)w * 0.5f;
                const float py = (float)y + 0.5f - (float)h * 0.5f;
                const float qx = std::abs (px) - hx + radius, qy = std::abs (py) - hy + radius;
                const float ox = juce::jmax (qx, 0.0f), oy = juce::jmax (qy, 0.0f);
                const float distance = std::sqrt (ox * ox + oy * oy) + juce::jmin (juce::jmax (qx, qy), 0.0f) - radius;
                if (distance >= 0.5f)
                    continue;
                const float nx = px - juce::jlimit (-hx + radius, hx - radius, px);
                const float ny = py - juce::jlimit (-hy + radius, hy - radius, py);
                const float length = juce::jmax (0.01f, std::sqrt (nx * nx + ny * ny));
                const float inset = juce::jmax (-distance, 0.0f);
                const float edge = std::pow (juce::jmax (0.0f, 1.0f - inset / juce::jmax (radius, 1.0f)), 1.7f);
                const float sx = px - nx / length * edge * 6.0f * t.material;
                const float sy = py - ny / length * edge * 6.0f * t.material;
                const float stripeDistance = vertical ? std::abs (sx) : std::abs (sy);
                const bool filled = vertical ? sy >= 0.0f : sx <= 0.0f;
                const float stripe = juce::jlimit (0.0f, 1.0f, 3.0f - stripeDistance);
                auto colour = t.panel.interpolatedWith (filled ? track : t.line, stripe * 0.85f);
                const float light = juce::jlimit (0.0f, 1.0f, (-nx * 0.6f - ny * 0.8f) / length);
                const float rim = std::exp (-inset / 1.5f);
                colour = colour.interpolatedWith (t.highlight (),
                                                  (rim * 0.24f + std::pow (light, 5.0f) * edge * 0.46f) * t.material);
                pixels.setPixelColour (x, y, colour.withAlpha (juce::jlimit (0.0f, 1.0f, 0.5f - distance)));
            }
        it = cache.emplace (key, image).first;
    }
    g.setColour (t.shadow ().withAlpha (t.isLight () ? 0.10f : 0.30f));
    g.fillRoundedRectangle (r.translated (0.0f, 1.5f), r.getHeight () * 0.5f);
    g.setOpacity (1.0f); // drawImage takes the context's opacity, which the shadow colour just set
    g.drawImage (it->second, r);
}

// The direction the light comes from for a point in the component being painted (toward the pointer while a Lit
// scope is open and the mouse has been seen, otherwise from above and a little to the right). Defined below.
juce::Point<float> lightFrom (juce::Point<float> local);

void
knob (juce::Graphics& g, juce::Rectangle<float> r, float value, juce::Colour colour, bool bipolar)
{
    // A glass dome with the value in the track's colour as a thin ring round its edge; on top of it sit the small lights
    // that make glass read as glass: a bright point where the light enters and a thin crescent along the lit rim. The
    // colour stays in the ring (the glass is not tinted by it).
    const auto& t = theme ();
    const bool light = t.isLight ();
    const float start = -juce::MathConstants<float>::pi * 0.75f, end = -start;
    const float angle = start + value * (end - start);
    const float outer = juce::jmax (1.0f, juce::jmin (r.getWidth (), r.getHeight ()) * 0.5f - 1.5f);
    const float ringW = juce::jlimit (1.8f, 3.4f, outer * 0.13f);
    const float ringR = outer - ringW * 0.5f;
    const auto c = r.getCentre ();
    // the sphere fills the knob; the ring is laid round its edge, as on a dial
    const float sphereR = juce::jmax (2.0f, outer - 0.5f);
    const auto sphere = juce::Rectangle<float> (sphereR * 2.0f, sphereR * 2.0f).withCentre (c);
    {
        Liquid liquid (g, r.expanded (2.0f).getSmallestIntegerContainer ());
        auto& b = liquid.backdrop ();
        // the sphere's body: brighter at the top, darker toward the bottom
        b.setGradientFill (juce::ColourGradient (light ? t.panel.interpolatedWith (t.highlight (), 0.6f) : t.panel2.interpolatedWith (t.highlight (), 0.2f), c.x, sphere.getY (),
                                                 (light ? t.panel.interpolatedWith (t.line, 0.7f) : t.panel.interpolatedWith (t.shadow (), 0.3f)),
                                                 c.x, sphere.getBottom (), false));
        b.fillEllipse (sphere);
        liquid.lens (sphere, sphereR, 0.0f);
    }
    auto arc = [&] (float a, float e, juce::Colour col, float width)
    {
        juce::Path path;
        path.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, a, e, true);
        g.setColour (col);
        g.strokePath (path, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    // the groove along the rim, then the value in the ring's colour
    arc (start, end, light ? t.line.withAlpha (0.55f) : t.shadow ().withAlpha (0.45f), ringW);
    if (std::abs (angle - (bipolar ? 0.0f : start)) > 0.01f)
        arc (juce::jmin (bipolar ? 0.0f : start, angle), juce::jmax (bipolar ? 0.0f : start, angle), colour, ringW);
    // a fine seam inside the ring, so the dome rises from it
    g.setColour (t.shadow ().withAlpha (light ? 0.10f : 0.35f));
    g.drawEllipse (sphere.reduced (ringW + 0.4f), 0.7f);
    const float domeR = sphereR - ringW - 0.5f;
    const auto dome = sphere.reduced (ringW + 0.5f);

    const auto dir = lightFrom (c);
    const float k = t.material > 0.0f ? t.material : 0.0f;
    if (k > 0.0f)
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addEllipse (dome);
        g.reduceClipRegion (clip);
        // the crescent along the lit rim
        juce::Path crescent;
        const float towards = std::atan2 (dir.x, -dir.y);
        crescent.addCentredArc (c.x, c.y, domeR - 1.0f, domeR - 1.0f, 0.0f, towards - 1.05f, towards + 1.05f, true);
        g.setColour (t.highlight ().withAlpha ((light ? 0.95f : 0.55f) * k));
        g.strokePath (crescent, juce::PathStrokeType (juce::jmax (1.0f, domeR * 0.09f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // the pointer
    g.setColour (t.text.withAlpha (0.9f));
    g.drawLine (c.x + std::sin (angle) * domeR * 0.25f, c.y - std::cos (angle) * domeR * 0.25f,
                c.x + std::sin (angle) * domeR * 0.78f, c.y - std::cos (angle) * domeR * 0.78f, juce::jmax (1.4f, domeR * 0.13f));
    // the bright point where the light enters
    if (k > 0.0f)
    {
        const auto spot = c + dir * domeR * 0.5f;
        const float sr = juce::jmax (1.2f, domeR * 0.2f);
        g.setGradientFill (juce::ColourGradient (t.highlight ().withAlpha ((light ? 0.95f : 0.8f) * k), spot, t.highlight ().withAlpha (0.0f), spot.translated (sr, 0.0f), true));
        g.fillEllipse (juce::Rectangle<float> (sr * 2.0f, sr * 2.0f).withCentre (spot));
    }
}
}

//==============================================================================
namespace ui::glass
{
namespace
{
float roundedBoxDistance (float px, float py, float hx, float hy, float r)
{
    const float qx = std::abs (px) - hx + r, qy = std::abs (py) - hy + r;
    return std::sqrt (juce::square (juce::jmax (qx, 0.0f)) + juce::square (juce::jmax (qy, 0.0f))) + juce::jmin (juce::jmax (qx, qy), 0.0f) - r;
}

struct Rgba // premultiplied, 0..1
{
    float r = 0, g = 0, b = 0, a = 0;
};

// Bilinear, clamped to the image, premultiplied (transparent parts of the backdrop stay transparent through the lens).
Rgba fetch (const juce::Image::BitmapData& d, float x, float y)
{
    x = juce::jlimit (0.0f, (float) d.width - 1.001f, x);
    y = juce::jlimit (0.0f, (float) d.height - 1.001f, y);
    const int x0 = (int) x, y0 = (int) y;
    const float fx = x - (float) x0, fy = y - (float) y0;
    const auto px = [&] (int xx, int yy) { return *reinterpret_cast<const juce::PixelARGB*> (d.getPixelPointer (xx, yy)); };
    const auto a = px (x0, y0), b = px (x0 + 1, y0), c = px (x0, y0 + 1), e = px (x0 + 1, y0 + 1);
    const auto mix = [&] (float pa, float pb, float pc, float pe) { return ((pa * (1 - fx) + pb * fx) * (1 - fy) + (pc * (1 - fx) + pe * fx) * fy) / 255.0f; };
    return { mix (a.getRed(), b.getRed(), c.getRed(), e.getRed()), mix (a.getGreen(), b.getGreen(), c.getGreen(), e.getGreen()),
             mix (a.getBlue(), b.getBlue(), c.getBlue(), e.getBlue()), mix (a.getAlpha(), b.getAlpha(), c.getAlpha(), e.getAlpha()) };
}

// `top` (premultiplied) over `under`
Rgba over (Rgba top, Rgba under)
{
    const float k = 1.0f - top.a;
    return { top.r + under.r * k, top.g + under.g * k, top.b + under.b * k, top.a + under.a * k };
}
} // namespace

//==============================================================================
namespace
{
std::vector<juce::Component*> litStack; // the components painting lenses right now (message thread)

// How far the pointer's light reaches: full within kNear of a lens, fading out smoothly to nothing at kFar, so glass
// further away rests in its own light instead of keeping a direction from the last time it was repainted.
constexpr float kNear = 160.0f, kFar = 460.0f;

float pointerWeight (float distance)
{
    const float x = juce::jlimit (0.0f, 1.0f, (distance - kNear) / (kFar - kNear));
    return 1.0f - x * x * (3.0f - 2.0f * x);
}

// The direction of the light for a lens: toward the pointer when it is near, the resting direction when it is far.
juce::Point<float> blendLight (juce::Point<float> toPointer, juce::Point<float> rest)
{
    const float len = toPointer.getDistanceFromOrigin();
    if (len < 0.01f)
        return rest;
    const float w = pointerWeight (len);
    auto d = rest * (1.0f - w) + toPointer / len * w;
    const float l = d.getDistanceFromOrigin();
    return l > 0.001f ? d / l : rest;
}

class LightTracker final : private juce::MouseListener, private juce::Timer
{
public:
    static LightTracker& get()
    {
        static LightTracker tracker;
        return tracker;
    }

    void enrol (juce::Component& c)
    {
        if (! installed)
        {
            juce::Desktop::getInstance().addGlobalMouseListener (this);
            installed = true;
        }
        for (auto& w : lit)
            if (w.component.getComponent() == &c)
                return;
        lit.erase (std::remove_if (lit.begin(), lit.end(), [] (const auto& w) { return w.component.getComponent() == nullptr; }), lit.end());
        lit.push_back ({ &c, 0.0f, 0.0f, 0.0f });
    }

    // The smoothed pointer in screen coordinates, if the mouse has been seen.
    std::optional<juce::Point<float>> pointer() const { return seen ? std::optional<juce::Point<float>> (smoothed) : std::nullopt; }

    // How much the pointer touches the glass of `c`, 0..1: rises as the mouse comes near, full while it is held on it,
    // and falls back smoothly when the mouse leaves (slowly at first, so a quick flick away still fades out).
    float touchOf (juce::Component& c) const
    {
        for (const auto& w : lit)
            if (w.component.getComponent() == &c)
                return w.touch;
        return 0.0f;
    }

private:
    struct Entry
    {
        juce::Component::SafePointer<juce::Component> component;
        float touch = 0.0f;
        float angle = 0.0f, weight = 0.0f; // the light it was last painted with
    };

    LightTracker() = default;
    void mouseMove (const juce::MouseEvent& e) override { follow (e.getScreenPosition().toFloat()); }
    void mouseDrag (const juce::MouseEvent& e) override { follow (e.getScreenPosition().toFloat()); }
    void mouseDown (const juce::MouseEvent& e) override { follow (e.getScreenPosition().toFloat()); }
    void mouseUp (const juce::MouseEvent& e) override { follow (e.getScreenPosition().toFloat()); }
    void follow (juce::Point<float> p)
    {
        target = p;
        if (! seen)
        {
            smoothed = p;
            seen = true;
        }
        if (! isTimerRunning())
        {
            last = juce::Time::getMillisecondCounterHiRes();
            startTimerHz (60);
        }
    }
    void timerCallback() override
    {
        static constexpr float kReach = 140.0f; // the glass starts to answer within this distance of the pointer
        const double now = juce::Time::getMillisecondCounterHiRes();
        const float dt = (float) juce::jlimit (0.0, 0.05, (now - last) / 1000.0);
        last = now;
        const auto previous = smoothed;
        smoothed += (target - smoothed) * (1.0f - std::exp (-dt * 13.0f)); // the study's damping
        const bool moving = smoothed.getDistanceFrom (previous) >= 0.25f;
        bool settling = false;
        for (auto& w : lit)
        {
            auto* c = w.component.getComponent();
            if (c == nullptr)
                continue;
            float goal = 0.0f;
            bool repaintNow = false;
            if (c->isShowing())
            {
                const auto bounds = c->getScreenBounds().toFloat();
                const float dx = juce::jmax (0.0f, juce::jmax (bounds.getX() - target.x, target.x - bounds.getRight()));
                const float dy = juce::jmax (0.0f, juce::jmax (bounds.getY() - target.y, target.y - bounds.getBottom()));
                const float near = juce::jlimit (0.0f, 1.0f, 1.0f - std::sqrt (dx * dx + dy * dy) / kReach);
                goal = c->isMouseButtonDown (true) && c->isMouseOver (true) ? 1.0f : 0.6f * near * near;
                // repaint only when the light it shows has visibly changed: its direction or how much the pointer
                // reaches it (far glass settles into its resting light once and is then left alone)
                const auto toPointer = smoothed - bounds.getCentre();
                const float weight = pointerWeight (toPointer.getDistanceFromOrigin());
                const float angle = std::atan2 (toPointer.y, toPointer.x);
                float turn = std::abs (angle - w.angle);
                turn = juce::jmin (turn, juce::MathConstants<float>::twoPi - turn);
                if (std::abs (weight - w.weight) > 0.015f || (weight > 0.01f && turn * weight > 0.03f))
                {
                    w.weight = weight;
                    w.angle = angle;
                    repaintNow = true;
                }
            }
            // quick to answer, slower to let go
            const float rate = goal > w.touch ? 12.0f : 4.0f;
            const float before = w.touch;
            w.touch += (goal - w.touch) * (1.0f - std::exp (-dt * rate));
            if (std::abs (goal - w.touch) < 0.002f)
                w.touch = goal;
            settling = settling || ! juce::exactlyEqual (w.touch, goal);
            if (c->isShowing() && (repaintNow || std::abs (w.touch - before) > 0.001f))
                c->repaint();
        }
        if (! moving && ! settling)
            stopTimer();
    }

    std::vector<Entry> lit;
    juce::Point<float> target, smoothed;
    double last = 0.0;
    bool installed = false, seen = false;
};
} // namespace

juce::Point<float> parallax (juce::Component& c, float depth)
{
    // the layer leans toward the pointer while it is near, and rests where it is when the pointer is far away
    const auto p = LightTracker::get().pointer();
    if (! p || depth == 0.0f || ! c.isShowing())
        return {};
    const auto d = *p - c.getScreenBounds().toFloat().getCentre();
    const float len = d.getDistanceFromOrigin();
    if (len < 0.01f)
        return {};
    return d / len * (juce::jmin (1.0f, len / kNear) * pointerWeight (len) * depth);
}

Lit::Lit (juce::Component& c)
{
    litStack.push_back (&c);
    if (theme().glass && juce::SystemStats::getEnvironmentVariable ("DAW_UI_SNAPSHOTS", {}).isEmpty())
        LightTracker::get().enrol (c);
}

Lit::~Lit ()
{
    litStack.pop_back();
}

juce::Point<float> lightFrom (juce::Point<float> local)
{
    const juce::Point<float> rest { -0.45f, -0.89f };
    if (! litStack.empty())
        if (const auto p = LightTracker::get().pointer())
            return blendLight (litStack.back()->getLocalPoint (nullptr, *p) - local, rest);
    return rest;
}

Liquid::Liquid (juce::Graphics& g, juce::Rectangle<int> a, juce::Colour behind) : target (g), area (a)
{
    scale = juce::jlimit (1.0f, 3.0f, g.getInternalContext ().getPhysicalPixelScaleFactor ());
    image = juce::Image (juce::Image::ARGB, juce::jmax (1, juce::roundToInt ((float) area.getWidth () * scale)),
                         juce::jmax (1, juce::roundToInt ((float) area.getHeight () * scale)), true, juce::SoftwareImageType ());
    graphics = std::make_unique<juce::Graphics> (image);
    if (! behind.isTransparent ())
        graphics->fillAll (behind);
    graphics->addTransform (juce::AffineTransform::translation ((float) -area.getX (), (float) -area.getY ()).scaled (scale));
}

Liquid::~Liquid ()
{
    graphics.reset ();
    compose ();
    target.setOpacity (1.0f);
    target.drawImage (image, area.toFloat ());
    // the body of a lens where the optics are off: a plain bead, so the control still reads as one
    const auto& t = theme ();
    if (!t.glass || t.material <= 0.0f)
        for (const auto& b : boxes)
        {
            target.setColour (t.panel.withAlpha (0.9f));
            target.fillRoundedRectangle (b.r, b.radius);
            target.setColour (t.line);
            target.drawRoundedRectangle (b.r.reduced (0.5f), b.radius, 1.0f);
        }
}

void Liquid::lens (juce::Rectangle<float> box, float radius, float shadow, float body, float prism)
{
    radius = juce::jlimit (0.0f, juce::jmin (box.getWidth (), box.getHeight ()) * 0.5f, radius);
    if (body > 0.0f && graphics != nullptr)
    {
        const auto& t = theme ();
        const auto pale = t.isLight () ? t.highlight () : t.panel2.interpolatedWith (t.highlight (), 0.18f);
        graphics->setGradientFill (juce::ColourGradient (pale.withAlpha (body), 0.0f, box.getY (), pale.withAlpha (body * 0.75f), 0.0f, box.getBottom (), false));
        graphics->fillRoundedRectangle (box.reduced (1.0f), juce::jmax (0.0f, radius - 1.0f));
    }
    boxes.push_back ({ box, radius, shadow, prism });
}

void Liquid::compose ()
{
    const auto& t = theme ();
    if (boxes.empty () || !t.glass || t.material <= 0.0f)
        return;
    const juce::Image source = image.createCopy ();
    const juce::Image::BitmapData src (source, juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData dst (image, juce::Image::BitmapData::readWrite);
    const bool dark = !t.isLight ();
    const float depth = juce::jlimit (0.0f, 1.0f, t.material * 0.9f); // the study's depth slider, from Appearance → Glass depth
    const float highlight = 0.52f;
    // the light: toward the pointer when a component is lit and the mouse has been seen, otherwise from above
    std::optional<juce::Point<float>> pointer;
    if (! litStack.empty())
        if (const auto p = LightTracker::get().pointer())
            pointer = litStack.back()->getLocalPoint (nullptr, *p);
    // touched glass splits the light: a lens of the component under the pointer shows a spectrum at its rim, more while
    // it is held (dragging a fader or a knob); lenses that are prisms already (the view switch) keep their own
    float touch = 0.0f;
    if (! litStack.empty() && litStack.back()->isEnabled())
        touch = LightTracker::get().touchOf (*litStack.back());
    // what lies under a lens stays where it is (moving labels read as a fault); only the light moves with the pointer
    const juce::Point<float> under;
    for (auto b : boxes)
    {
        if (b.prism <= 1.0f && touch > 0.0f)
            b.prism = 1.0f + 0.9f * touch;
        float lx = 0.24f, ly = -0.97f;
        if (pointer)
        {
            const auto d = blendLight (*pointer - b.r.getCentre(), { lx, ly });
            lx = d.x;
            ly = d.y;
        }
        const float hx = b.r.getWidth () * 0.5f, hy = b.r.getHeight () * 0.5f, radius = b.radius;
        const float cx = b.r.getCentreX (), cy = b.r.getCentreY ();
        const float reach = b.shadow > 0.0f ? 26.0f : 2.0f; // the shadow fades out within this distance
        const auto pixels = b.r.expanded (reach, reach).translated ((float) -area.getX (), (float) -area.getY ()) * scale;
        const int x0 = juce::jmax (0, (int) std::floor (pixels.getX ())), x1 = juce::jmin (dst.width, (int) std::ceil (pixels.getRight ()));
        const int y0 = juce::jmax (0, (int) std::floor (pixels.getY ())), y1 = juce::jmin (dst.height, (int) std::ceil (pixels.getBottom ()));
        // small lenses bend less: the study's numbers are for a lens about 50 px tall
        const float size = juce::jlimit (0.2f, 1.0f, juce::jmin (hx, hy) / 30.0f);
        const float shiftMax = (3.0f + depth * 22.0f) * size * (b.prism > 1.0f ? 0.8f : 1.0f), zoomBase = 0.025f + depth * 0.07f;
        // the shadow fades out before the edge of the painted area, so it never ends in a hard line
        const float room = juce::jmax (1.0f, juce::jmin (b.r.getX () - (float) area.getX (), b.r.getY () - (float) area.getY (),
                                                         (float) area.getRight () - b.r.getRight (), (float) area.getBottom () - b.r.getBottom ()));
        // a prism lens keeps a clear middle: the bend and the spectrum stay in a band along the rim
        const float edgeWidth = b.prism > 1.0f ? juce::jmax (4.0f, juce::jmin (hx, hy) * 0.42f) : juce::jmin (radius * 0.9f, 34.0f, juce::jmin (hx, hy));
        for (int yy = y0; yy < y1; ++yy)
            for (int xx = x0; xx < x1; ++xx)
            {
                // component coordinates of this pixel, then relative to the lens centre
                const float px = ((float) xx + 0.5f) / scale + (float) area.getX (), py = ((float) yy + 0.5f) / scale + (float) area.getY ();
                const float lxp = px - cx, lyp = py - cy;
                const float dist = roundedBoxDistance (lxp, lyp, hx, hy, radius);
                auto* out = reinterpret_cast<juce::PixelARGB*> (dst.getPixelPointer (xx, yy));
                Rgba col { out->getRed () / 255.0f, out->getGreen () / 255.0f, out->getBlue () / 255.0f, out->getAlpha () / 255.0f };
                // soft shadow under the lens: black, fading with the distance below and around it
                const float shadowDist = roundedBoxDistance (lxp, lyp - 5.0f, hx, hy, radius);
                const float shade = std::exp (-juce::jmax (shadowDist, 0.0f) / 9.0f) * (dark ? 0.2f : 0.11f) * b.shadow
                                  * juce::square (juce::jlimit (0.0f, 1.0f, 1.0f - juce::jmax (0.0f, roundedBoxDistance (lxp, lyp, hx, hy, radius)) / room))
                                  * (1.0f - juce::jlimit (0.0f, 1.0f, (-dist + 0.2f) / 1.2f));
                col = over ({ 0, 0, 0, shade }, col);
                if (dist < 1.0f)
                {
                    const float coverage = 1.0f - juce::jlimit (0.0f, 1.0f, (dist + 0.65f) / 1.3f);
                    const float gx = roundedBoxDistance (lxp + 0.4f, lyp, hx, hy, radius) - roundedBoxDistance (lxp - 0.4f, lyp, hx, hy, radius);
                    const float gy = roundedBoxDistance (lxp, lyp + 0.4f, hx, hy, radius) - roundedBoxDistance (lxp, lyp - 0.4f, hx, hy, radius);
                    const float gl = juce::jmax (0.0001f, std::sqrt (gx * gx + gy * gy));
                    const float nx = gx / gl, ny = gy / gl;
                    const float inset = juce::jmax (-dist, 0.0f);
                    const float edge = 1.0f - juce::jlimit (0.0f, 1.0f, inset / juce::jmax (1.0f, edgeWidth));
                    const float curve = std::pow (edge, 1.65f);
                    const float sx = px - nx * shiftMax * curve - lxp * zoomBase * (1.0f - edge) + under.x * (1.0f - edge * 0.5f);
                    const float sy = py - ny * shiftMax * curve - lyp * zoomBase * (1.0f - edge) + under.y * (1.0f - edge * 0.5f);
                    // the spectrum at the rim: five samples spread along the normal, red outward, violet inward
                    const float spread = (0.6f + depth * 2.6f) * curve * size * b.prism * 1.5f;
                    const auto at = [&] (float k) { return fetch (src, (sx - (float) area.getX () + nx * spread * k) * scale, (sy - (float) area.getY () + ny * spread * k) * scale); };
                    const Rgba s0 = at (-1.0f), s1 = at (-0.5f), s2 = at (0.0f), s3 = at (0.5f), s4 = at (1.0f);
                    Rgba glassCol { s0.r * 0.45f + s1.r * 0.35f + s2.r * 0.2f, s1.g * 0.25f + s2.g * 0.5f + s3.g * 0.25f,
                                    s2.b * 0.2f + s3.b * 0.35f + s4.b * 0.45f, (s0.a + s1.a + s2.a + s3.a + s4.a) * 0.2f };
                    const float tint = dark ? 0.025f : 0.035f, tintValue = dark ? 0.32f : 1.0f;
                    glassCol = over ({ tintValue * tint, tintValue * tint, tintValue * tint, tint }, glassCol);
                    const float rim = std::exp (-inset / 1.25f);
                    const float facing = nx * lx + ny * ly;
                    const float caustic = std::exp (-juce::square ((inset - 3.0f) / 2.5f)) * juce::jmax (-facing, 0.0f) * 0.12f;
                    const float spec = std::pow (juce::jmax (facing, 0.0f), 7.0f) * rim;
                    const float inner = std::pow (juce::jmax (nx * -0.6f + ny * -0.8f, 0.0f), 3.0f) * std::exp (-inset / 6.0f);
                    const float power = 0.18f + highlight * 0.75f;
                    const float add = juce::jlimit (0.0f, 1.0f, rim * 0.11f + spec * power + inner * 0.07f + caustic);
                    glassCol = over ({ add, add, add, add }, glassCol); // light: white over the glass
                    if (b.prism > 1.0f)
                    {
                        // a prism rim: a thin line of spectrum round the edge, its hue turning with the direction of
                        // the rim, brightest where the light falls on it and opposite (the light leaving the glass)
                        const float hue = std::fmod (std::atan2 (ny, nx) / juce::MathConstants<float>::twoPi + 1.25f, 1.0f);
                        const auto c = juce::Colour::fromHSV (hue, dark ? 0.75f : 0.85f, 1.0f, 1.0f);
                        const float band = std::exp (-juce::square ((inset - 1.2f) / 1.1f)) * (0.25f + 0.55f * std::abs (facing)) * (dark ? 0.55f : 0.45f)
                                         * juce::jlimit (1.0f, 1.8f, b.prism / 2.2f);
                        glassCol = over ({ c.getFloatRed () * band, c.getFloatGreen () * band, c.getFloatBlue () * band, band }, glassCol);
                    }
                    const float dim = 1.0f - juce::jmax (-facing, 0.0f) * rim * 0.18f;
                    glassCol = over ({ 0, 0, 0, 1.0f - dim }, glassCol);
                    col = { col.r + (glassCol.r - col.r) * coverage, col.g + (glassCol.g - col.g) * coverage, col.b + (glassCol.b - col.b) * coverage,
                            col.a + (glassCol.a - col.a) * coverage };
                }
                const auto byte = [] (float v) { return (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (v * 255.0f)); };
                const float a = juce::jlimit (0.0f, 1.0f, col.a);
                out->setARGB (byte (a), byte (juce::jmin (col.r, a)), byte (juce::jmin (col.g, a)), byte (juce::jmin (col.b, a)));
            }
    }
}
} // namespace ui::glass
