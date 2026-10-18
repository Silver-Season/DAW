#include "GlassMaterial.h"
#include <cmath>
#include <map>
#include <tuple>

namespace ui::glass
{
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
    const auto& t = theme ();
    if (!t.glass || t.material <= 0.0f)
    {
        g.setColour (base);
        g.fillRoundedRectangle (r, radius);
        return;
    }
    const float strength = t.material;
    const auto top = base.interpolatedWith (t.highlight (), (t.isLight () ? 0.40f : 0.065f) * strength);
    const auto bottom = base.interpolatedWith (t.accentFill, 0.035f * strength);
    g.setGradientFill (juce::ColourGradient (top, r.getTopLeft (), bottom, r.getBottomRight (), false));
    g.fillRoundedRectangle (r, radius);
    g.setColour (t.highlight ().withAlpha ((t.isLight () ? 0.70f : 0.12f) * strength));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    g.setColour (t.shadow ().withAlpha ((t.isLight () ? 0.06f : 0.15f) * strength));
    g.drawRoundedRectangle (r.reduced (1.5f), juce::jmax (0.0f, radius - 1.0f), 0.7f);
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
    g.drawImage (it->second, r);
}

void
knob (juce::Graphics& g, juce::Rectangle<float> r, float value, juce::Colour colour, bool bipolar)
{
    const auto& t = theme ();
    const float start = -juce::MathConstants<float>::pi * 0.75f, end = -start;
    const float angle = start + value * (end - start);
    const float radius = juce::jmax (1.0f, juce::jmin (r.getWidth (), r.getHeight ()) * 0.5f - 2.0f);
    auto arc = [&] (float a, float b, juce::Colour c, float thickness)
    {
        juce::Path path;
        path.addCentredArc (r.getCentreX (), r.getCentreY (), radius, radius, 0.0f, a, b, true);
        g.setColour (c);
        g.strokePath (path,
                      juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    arc (start, end, t.line, 2.0f);
    if (std::abs (angle - (bipolar ? 0.0f : start)) > 0.01f)
        arc (juce::jmin (bipolar ? 0.0f : start, angle), juce::jmax (bipolar ? 0.0f : start, angle), colour, 2.5f);
    surface (g, r.reduced (5.0f), t.panel, radius);
    g.setColour (t.text);
    const auto centre = r.getCentre ();
    g.drawLine (centre.x + std::sin (angle) * radius * 0.30f,
                centre.y - std::cos (angle) * radius * 0.30f,
                centre.x + std::sin (angle) * radius * 0.68f,
                centre.y - std::cos (angle) * radius * 0.68f,
                1.5f);
}
}
