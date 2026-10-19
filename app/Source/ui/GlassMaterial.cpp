#include "GlassMaterial.h"
#include <cmath>
#include <map>
#include <tuple>
#include <vector>

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
    // Frosted glass: the body a little lighter at the top, a bright glossy band on the upper half, a white inner rim
    // and a faint dark line at the bottom where the light does not reach.
    const auto& t = theme ();
    if (!t.glass || t.material <= 0.0f)
    {
        g.setColour (base);
        g.fillRoundedRectangle (r, radius);
        return;
    }
    const float strength = t.material;
    const auto top = base.interpolatedWith (t.highlight (), (t.isLight () ? 0.55f : 0.10f) * strength);
    const auto bottom = base.interpolatedWith (t.isLight () ? t.line : t.shadow (), (t.isLight () ? 0.12f : 0.15f) * strength);
    g.setGradientFill (juce::ColourGradient (top, 0.0f, r.getY (), bottom, 0.0f, r.getBottom (), false));
    g.fillRoundedRectangle (r, radius);
    if (r.getHeight () >= 14.0f)
    {
        const auto gloss = r.reduced (1.5f).withHeight (r.getHeight () * 0.46f);
        g.setGradientFill (juce::ColourGradient (t.highlight ().withAlpha ((t.isLight () ? 0.75f : 0.10f) * strength), 0.0f, gloss.getY (),
                                                 t.highlight ().withAlpha (0.0f), 0.0f, gloss.getBottom (), false));
        g.fillRoundedRectangle (gloss, juce::jmax (0.0f, juce::jmin (radius - 1.5f, gloss.getHeight ())));
    }
    g.setGradientFill (juce::ColourGradient (t.highlight ().withAlpha ((t.isLight () ? 0.95f : 0.22f) * strength), 0.0f, r.getY (),
                                             t.highlight ().withAlpha ((t.isLight () ? 0.25f : 0.03f) * strength), 0.0f, r.getBottom (), false));
    g.drawRoundedRectangle (r.reduced (1.0f), juce::jmax (0.0f, radius - 1.0f), 1.0f);
}

void
cell (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour base, float radius)
{
    const auto& t = theme ();
    if (!t.glass || t.material <= 0.0f || base.getAlpha () < 255)
    {
        g.setColour (base);
        g.fillRoundedRectangle (r, radius);
        return;
    }
    const auto top = base.interpolatedWith (t.highlight (), (t.isLight () ? 0.30f : 0.05f) * t.material);
    g.setGradientFill (juce::ColourGradient (top, 0.0f, r.getY (), base, 0.0f, r.getBottom (), false));
    g.fillRoundedRectangle (r, radius);
}

void
capsule (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& t = theme ();
    const float radius = r.getHeight () * 0.5f;
    if (!t.glass || t.material <= 0.0f)
    {
        g.setColour (t.panel);
        g.fillRoundedRectangle (r, radius);
        g.setColour (t.line);
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
        return;
    }
    // A soft drop shadow, a frosted body lighter at the top, a bright upper rim and a darker lower one.
    juce::DropShadow (t.shadow ().withAlpha (t.isLight () ? 0.10f : 0.35f), 14, { 0, 4 })
      .drawForPath (g, [&] { juce::Path p; p.addRoundedRectangle (r, radius); return p; } ());
    const auto body = t.panel;
    g.setGradientFill (juce::ColourGradient (body.interpolatedWith (t.highlight (), t.isLight () ? 0.55f : 0.08f),
                                             0.0f, r.getY (),
                                             body.interpolatedWith (t.background, 0.35f), 0.0f, r.getBottom (), false));
    g.fillRoundedRectangle (r, radius);
    g.setGradientFill (juce::ColourGradient (t.highlight ().withAlpha (t.isLight () ? 0.95f : 0.30f), 0.0f, r.getY (),
                                             t.highlight ().withAlpha (0.0f), 0.0f, r.getCentreY (), false));
    g.drawRoundedRectangle (r.reduced (0.75f), radius - 0.75f, 1.5f);
    g.setColour (t.line.withAlpha (t.isLight () ? 0.75f : 0.6f));
    g.drawRoundedRectangle (r, radius, 1.0f);
}

void
thumb (juce::Graphics& g, juce::Rectangle<float> r)
{
    // A glossy capsule: soft shadow, a body lit from above, a bright upper half and a rim.
    const auto& t = theme ();
    const float radius = juce::jmin (r.getWidth (), r.getHeight ()) * 0.5f;
    juce::DropShadow (t.shadow ().withAlpha (t.isLight () ? 0.26f : 0.55f), 5, { 0, 2 })
      .drawForPath (g, [&] { juce::Path p; p.addRoundedRectangle (r, radius); return p; } ());
    const auto body = t.isLight () ? t.highlight () : t.panel2.interpolatedWith (t.highlight (), 0.7f);
    g.setGradientFill (juce::ColourGradient (body, 0.0f, r.getY (), body.interpolatedWith (t.line, 0.45f), 0.0f, r.getBottom (), false));
    g.fillRoundedRectangle (r, radius);
    const auto gloss = r.reduced (1.5f).withHeight (r.getHeight () * 0.48f);
    g.setGradientFill (juce::ColourGradient (t.highlight ().withAlpha (0.95f), 0.0f, gloss.getY (), t.highlight ().withAlpha (0.15f), 0.0f, gloss.getBottom (), false));
    g.fillRoundedRectangle (gloss, juce::jmin (radius, gloss.getHeight () * 0.5f));
    g.setColour (t.isLight () ? t.line.withAlpha (0.85f) : t.shadow ().withAlpha (0.5f));
    g.drawRoundedRectangle (r.reduced (0.5f), radius - 0.5f, 1.0f);
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

void
knob (juce::Graphics& g, juce::Rectangle<float> r, float value, juce::Colour colour, bool bipolar)
{
    // A glass sphere inside a coloured value ring: a soft shadow under it, a body lit from above, a bright
    // specular spot at the upper left, a thin rim, and the pointer from the centre.
    const auto& t = theme ();
    const float start = -juce::MathConstants<float>::pi * 0.75f, end = -start;
    const float angle = start + value * (end - start);
    const float outer = juce::jmax (1.0f, juce::jmin (r.getWidth (), r.getHeight ()) * 0.5f - 1.5f);
    const float ringW = juce::jlimit (2.0f, 3.5f, outer * 0.16f);
    const float ringR = outer - ringW * 0.5f;
    auto arc = [&] (float a, float b, juce::Colour c)
    {
        juce::Path path;
        path.addCentredArc (r.getCentreX (), r.getCentreY (), ringR, ringR, 0.0f, a, b, true);
        g.setColour (c);
        g.strokePath (path, juce::PathStrokeType (ringW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    arc (start, end, t.isLight () ? t.line.withAlpha (0.7f) : t.shadow ().withAlpha (0.45f));
    if (std::abs (angle - (bipolar ? 0.0f : start)) > 0.01f)
        arc (juce::jmin (bipolar ? 0.0f : start, angle), juce::jmax (bipolar ? 0.0f : start, angle), colour);

    const float sphereR = juce::jmax (2.0f, outer - ringW - 2.5f);
    const auto c = r.getCentre ();
    const auto sphere = juce::Rectangle<float> (sphereR * 2.0f, sphereR * 2.0f).withCentre (c);
    const float strength = t.material > 0.0f ? t.material : 0.0f;
    juce::DropShadow (t.shadow ().withAlpha ((t.isLight () ? 0.22f : 0.55f) * juce::jmax (0.4f, strength)), juce::roundToInt (sphereR * 0.45f) + 2,
                      { 0, juce::roundToInt (sphereR * 0.18f) + 1 })
      .drawForPath (g, [&] { juce::Path p; p.addEllipse (sphere); return p; } ());
    const auto light = t.isLight () ? t.highlight () : t.panel2.interpolatedWith (t.highlight (), 0.35f);
    const auto dark = t.isLight () ? t.panel2.interpolatedWith (t.line, 0.35f) : t.panel.interpolatedWith (t.shadow (), 0.3f);
    juce::ColourGradient body (light, c.x - sphereR * 0.35f, c.y - sphereR * 0.55f, dark, c.x + sphereR * 0.4f, c.y + sphereR * 1.05f, true);
    g.setGradientFill (body);
    g.fillEllipse (sphere);
    if (strength > 0.0f && sphereR >= 6.0f)
    {
        // the specular spot and a faint bounce of light along the lower rim
        const auto spot = juce::Rectangle<float> (sphereR * 0.9f, sphereR * 0.55f).withCentre ({ c.x - sphereR * 0.22f, c.y - sphereR * 0.48f });
        g.setGradientFill (juce::ColourGradient (t.highlight ().withAlpha ((t.isLight () ? 0.95f : 0.45f) * strength), spot.getCentreX (), spot.getCentreY (),
                                                 t.highlight ().withAlpha (0.0f), spot.getRight (), spot.getBottom (), true));
        g.fillEllipse (spot);
        juce::Path bounce;
        bounce.addCentredArc (c.x, c.y, sphereR - 1.2f, sphereR - 1.2f, 0.0f, juce::MathConstants<float>::pi * 0.65f, juce::MathConstants<float>::pi * 1.35f, true);
        g.setColour (t.highlight ().withAlpha ((t.isLight () ? 0.7f : 0.2f) * strength));
        g.strokePath (bounce, juce::PathStrokeType (1.0f));
    }
    g.setColour (t.isLight () ? t.line.withAlpha (0.8f) : t.shadow ().withAlpha (0.6f));
    g.drawEllipse (sphere.reduced (0.25f), 0.8f);
    g.setColour (t.text);
    g.drawLine (c.x + std::sin (angle) * sphereR * 0.18f, c.y - std::cos (angle) * sphereR * 0.18f,
                c.x + std::sin (angle) * sphereR * 0.78f, c.y - std::cos (angle) * sphereR * 0.78f, juce::jmax (1.4f, sphereR * 0.12f));
}
}
