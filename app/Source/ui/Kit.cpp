#include "Kit.h"

#include "GlassMaterial.h"

namespace ui::kit
{
namespace
{
// The theme's ink for a fill, or black or white when that is not readable enough on it (any theme).
juce::Colour inkFor (juce::Colour fill)
{
    const auto& t = theme();
    auto ink = t.inkOn (fill);
    if (Theme::contrast (ink, fill) < 4.5f)
        ink = Theme::contrast (juce::Colours::white, fill) >= Theme::contrast (juce::Colours::black, fill) ? juce::Colours::white : juce::Colours::black;
    return ink;
}
} // namespace

float controlRadius (juce::Rectangle<float> r)
{
    const auto& m = theme().metrics;
    return m.pill ? juce::jmin (r.getWidth(), r.getHeight()) * 0.5f : juce::jmin (m.controlRadius, r.getHeight() * 0.5f);
}

juce::Colour button (juce::Graphics& g, juce::Rectangle<float> r, Kind kind, State s, std::optional<juce::Colour> colour)
{
    const auto& t = theme();
    const float radius = controlRadius (r);
    const bool lit = s.on || kind == Kind::primary;
    juce::Colour litColour = colour.value_or (kind == Kind::danger ? t.record : (t.glass ? t.accentFill : t.accent));
    juce::Colour ink = t.text;

    if (kind == Kind::play)
    {
        const auto play = colour.value_or (t.play);
        const float d = juce::jmin (r.getWidth(), r.getHeight());
        const auto circle = r.withSizeKeepingCentre (d, d);
        if (t.glass)
        {
            // clear glass; while playing the light of the accent gathers in it and the rim turns into a spectrum
            juce::ignoreUnused (play);
            const auto light = colour.value_or (t.accentFill);
            glass::litBead (g, circle, d * 0.5f, true, light, s.on ? 1.0f : 0.0f);
            if (! s.on && (s.hover || s.down))
                glass::spectrumRim (g, circle, d * 0.5f, s.down ? 0.45f : 0.3f);
            return s.on ? (t.isLight() ? t.accent : t.accent.interpolatedWith (t.highlight(), 0.3f)) : t.text;
        }
        // classic: a framed square that lights up while playing
        if (s.on && t.glow > 0.0f)
            juce::DropShadow (play.withAlpha (0.6f), juce::roundToInt (14.0f * t.glow), {}).drawForRectangle (g, r.toNearestInt().reduced (3));
        g.setColour (s.on ? play : (s.hover ? t.panel2 : t.panel));
        g.fillRoundedRectangle (r, t.radius);
        g.setColour (s.on ? play : t.line);
        g.drawRoundedRectangle (r.reduced (0.5f), t.radius, 1.0f);
        return s.on ? inkFor (play) : t.text;
    }

    if (! t.glass)
    {
        // the classic skins: a filled cell with a one-pixel border, the accent when lit
        auto fill = lit ? litColour : (s.down ? t.panel2.brighter (0.1f) : (s.hover ? t.panel2 : t.panel));
        auto border = lit ? litColour : t.line;
        if (kind == Kind::ghost && ! lit && ! s.hover)
            fill = t.panel;
        if (! s.enabled)
        {
            fill = fill.withMultipliedAlpha (0.5f);
            border = border.withMultipliedAlpha (0.6f);
        }
        if (lit && t.glow > 0.0f && kind != Kind::plain)
            juce::DropShadow (litColour.withAlpha (0.45f), juce::roundToInt (10.0f * t.glow), {}).drawForRectangle (g, r.toNearestInt().reduced (3));
        g.setColour (fill);
        g.fillRoundedRectangle (r, radius);
        g.setColour (border);
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
        ink = lit ? inkFor (litColour) : (s.enabled ? t.text : t.dimText.withAlpha (0.6f));
    }
    else if (kind == Kind::ghost && ! (s.on && colour.has_value()))
    {
        // in the transport capsule: only the icon; lit is a soft tint with the icon in the accent
        if (s.on)
        {
            g.setColour (t.accentFill.withAlpha (t.isLight() ? 0.22f : 0.3f));
            g.fillRoundedRectangle (r, radius);
            ink = t.accent;
        }
        else if ((s.hover || s.down) && s.enabled)
        {
            g.setColour (t.panel2.withAlpha (s.down ? 1.0f : 0.8f));
            g.fillRoundedRectangle (r, radius);
        }
        if (! s.enabled)
            ink = t.dimText.withAlpha (0.6f);
    }
    else if (lit && kind != Kind::primary && s.enabled)
    {
        // a toggle that is on, record armed: the glass lit from inside by its colour, the label in that colour
        glass::litBead (g, r, radius, false, litColour, s.down ? 0.8f : 1.0f);
        // the middle of the lit glass is about the body with the glow over it; the label keeps 3:1 against it
        const auto middle = (t.isLight() ? t.highlight() : t.panel2).interpolatedWith (litColour, t.isLight() ? 0.42f : 0.55f);
        ink = t.text;
        for (const auto candidate : { t.isLight() ? litColour.interpolatedWith (t.text, 0.35f) : litColour.interpolatedWith (t.highlight(), 0.45f),
                                      t.text, t.background })
            if (Theme::contrast (candidate, middle) >= 3.0f)
            {
                ink = candidate;
                break;
            }
    }
    else if (lit)
    {
        glass::accent (g, r, s.enabled ? litColour : t.panel2, radius);
        if (s.hover || s.down)
        {
            g.setColour (t.highlight().withAlpha (s.down ? 0.18f : 0.1f));
            g.fillRoundedRectangle (r, radius);
        }
        ink = s.enabled ? inkFor (litColour) : t.dimText;
    }
    else
    {
        if (s.enabled)
        {
            glass::bead (g, r, radius);
            if (s.hover || s.down)
            {
                g.setColour ((s.down ? t.shadow() : t.highlight()).withAlpha (s.down ? 0.06f : (t.isLight() ? 0.25f : 0.06f)));
                g.fillRoundedRectangle (r, radius);
                glass::spectrumRim (g, r, radius, s.down ? 0.45f : 0.3f); // touched glass splits the light at its edge
            }
        }
        else
        {
            g.setColour (t.panel2.withAlpha (0.5f));
            g.fillRoundedRectangle (r, radius);
        }
        ink = s.enabled ? t.text : t.dimText.withAlpha (0.6f);
    }
    if (s.focus)
        focusRing (g, r, radius);
    return ink;
}

void label (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, juce::Colour ink, const Icon& icon, LabelStyle st)
{
    const auto& t = theme();
    const auto font = t.uiFont (st.size > 0.0f ? st.size : t.metrics.text, st.weight);
    const float textWidth = text.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth (font, text);
    const float iconSize = icon ? st.iconSize : 0.0f;
    const float gap = icon && text.isNotEmpty() ? 8.0f : 0.0f;
    const float chevronWidth = st.chevron ? 14.0f : 0.0f;
    const float total = iconSize + gap + textWidth + chevronWidth;
    float x = st.leftAligned ? r.getX() + 12.0f : r.getCentreX() - total * 0.5f;
    if (icon)
    {
        g.setColour (st.iconColour.value_or (ink));
        g.fillPath (icon (juce::Rectangle<float> (iconSize, iconSize).withCentre ({ x + iconSize * 0.5f, r.getCentreY() })));
        x += iconSize + gap;
    }
    if (text.isNotEmpty())
    {
        g.setColour (ink);
        g.setFont (font);
        g.drawText (text, juce::Rectangle<float> (x, r.getY(), textWidth + 2.0f, r.getHeight()), juce::Justification::centredLeft, false);
        x += textWidth;
    }
    if (st.chevron)
    {
        juce::Path v;
        const juce::Point<float> c (x + 9.0f, r.getCentreY() + 1.0f);
        v.startNewSubPath (c.x - 3.5f, c.y - 2.0f);
        v.lineTo (c.x, c.y + 1.8f);
        v.lineTo (c.x + 3.5f, c.y - 2.0f);
        g.setColour (ink.withMultipliedAlpha (0.8f));
        g.strokePath (v, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void switchControl (juce::Graphics& g, juce::Rectangle<float> area, State s)
{
    const auto& t = theme();
    const float h = juce::jmin (area.getHeight(), t.glass ? 18.0f : 16.0f), w = h * 1.75f;
    const auto track = juce::Rectangle<float> (w, h).withCentre ({ area.getRight() - w * 0.5f - 1.0f, area.getCentreY() });
    const auto knob = juce::Rectangle<float> (h + 2.0f, h + 2.0f).withCentre ({ s.on ? track.getRight() - h * 0.5f : track.getX() + h * 0.5f, track.getCentreY() });
    if (t.glass)
    {
        // the knob is a clear lens over the track
        glass::Liquid liquid (g, track.expanded (3.0f).getSmallestIntegerContainer());
        auto& b = liquid.backdrop();
        if (s.on)
            glass::accent (b, track, s.enabled ? t.accentFill : t.panel2, h * 0.5f);
        else
        {
            b.setColour (t.line.withMultipliedAlpha (s.enabled ? 1.0f : 0.5f));
            b.fillRoundedRectangle (track, h * 0.5f);
        }
        liquid.lens (knob.reduced (1.0f), knob.getWidth() * 0.5f, 0.5f, 0.92f);
    }
    else
    {
        g.setColour (s.on ? (s.enabled ? t.accent : t.panel2) : t.line.withMultipliedAlpha (s.enabled ? 1.0f : 0.5f));
        g.fillRoundedRectangle (track, h * 0.5f);
        g.setColour (s.on ? t.inkOn (t.accent) : t.panel);
        g.fillEllipse (knob.reduced (2.0f));
    }
    if (s.focus)
        focusRing (g, track.expanded (2.0f), h * 0.5f + 2.0f);
}

int segmentAt (juce::Rectangle<float> r, int count, float x)
{
    return juce::jlimit (0, juce::jmax (0, count - 1), (int) ((x - r.getX()) / juce::jmax (1.0f, r.getWidth() / (float) juce::jmax (1, count))));
}

void segmented (juce::Graphics& g, juce::Rectangle<float> r, const juce::StringArray& labels, int selected, int hover, int drop)
{
    const auto& t = theme();
    const int n = juce::jmax (1, labels.size());
    const float w = r.getWidth() / (float) n;
    const auto seg = [&] (int i) { return juce::Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight()); };
    const auto labelFont = [&] (int i) { return t.uiFont (t.glass ? t.metrics.text + 1.0f : 12.0f, i == selected && t.glass ? 600 : 500); };
    if (t.glass)
    {
        // A sunken track with all the labels, the selected one in the accent colour. The selected segment is a clear
        // lens a little taller than the track: the rim bends the labels and the edges of the track and splits them into
        // a spectrum, as a prism does; the middle only magnifies a little, so the label under it stays readable.
        const auto track = r.reduced (0.0f, 3.0f);
        const float pill = track.getHeight() * 0.5f;
        {
            glass::Liquid liquid (g, r.expanded (2.0f, 2.0f).getSmallestIntegerContainer());
            auto& b = liquid.backdrop();
            b.setColour (t.panel2.interpolatedWith (t.line, t.isLight() ? 0.35f : 0.2f));
            b.fillRoundedRectangle (track, pill);
            b.setColour (t.shadow().withAlpha (t.isLight() ? 0.08f : 0.3f));
            b.drawRoundedRectangle (track.reduced (0.5f), pill, 1.0f);
            if (hover >= 0 && hover != selected)
            {
                b.setColour (t.panel.withAlpha (0.45f));
                b.fillRoundedRectangle (seg (hover).withY (track.getY()).withHeight (track.getHeight()).reduced (3.0f), pill - 3.0f);
            }
            if (drop >= 0)
            {
                b.setColour (t.accent.withAlpha (0.22f));
                b.fillRoundedRectangle (seg (drop).withY (track.getY()).withHeight (track.getHeight()), pill);
            }
            for (int i = 0; i < labels.size(); ++i)
            {
                b.setColour (i == selected ? t.accent : t.dimText);
                b.setFont (labelFont (i));
                b.drawText (labels[i], seg (i), juce::Justification::centred, false);
            }
            if (selected >= 0)
                liquid.lens (seg (selected).reduced (1.0f, 0.0f), r.getHeight() * 0.5f, 0.0f, t.isLight() ? 0.12f : 0.0f, 2.2f);
        }
        return;
    }
    g.setColour (t.panel);
    g.fillRoundedRectangle (r, t.radius);
    if (selected >= 0)
    {
        auto a = seg (selected);
        g.setColour (t.panel2);
        g.fillRoundedRectangle (a, t.radius);
        g.setColour (t.accent);
        g.fillRect (a.removeFromBottom (2.0f).reduced (1.0f, 0.0f));
    }
    g.setColour (t.line);
    g.drawRoundedRectangle (r.reduced (0.5f), t.radius, 1.0f);
    for (int i = 0; i < labels.size(); ++i)
    {
        g.setColour (i == selected ? t.text : t.dimText);
        g.setFont (labelFont (i));
        g.drawText (labels[i], seg (i), juce::Justification::centred, false);
    }
    if (drop >= 0)
    {
        g.setColour (t.accent.withAlpha (0.22f));
        g.fillRoundedRectangle (seg (drop), t.radius);
    }
}

void field (juce::Graphics& g, juce::Rectangle<float> r, State s)
{
    const auto& t = theme();
    const float radius = t.glass ? juce::jmin (t.metrics.controlRadius, r.getHeight() * 0.5f) : t.radius;
    if (t.glass)
    {
        g.setColour (t.panel2.withAlpha (t.isLight() ? 0.75f : 0.6f));
        g.fillRoundedRectangle (r, radius);
        g.setColour (s.focus ? t.accent : t.line.withAlpha (0.7f));
        g.drawRoundedRectangle (r.reduced (s.focus ? 0.75f : 0.5f), radius, s.focus ? 1.5f : 1.0f);
        return;
    }
    g.setColour (t.panel);
    g.fillRoundedRectangle (r, radius);
    g.setColour (s.focus ? t.accent : t.line);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

void card (juce::Graphics& g, juce::Rectangle<float> r, bool selected)
{
    const auto& t = theme();
    const float radius = t.metrics.panelRadius;
    if (t.glass)
    {
        juce::DropShadow (t.shadow().withAlpha (t.isLight() ? 0.06f : 0.3f), 10, { 0, 2 }).drawForRectangle (g, r.toNearestInt().reduced (3));
        glass::surface (g, r, t.panel, radius);
    }
    else
    {
        g.setColour (t.panel);
        g.fillRoundedRectangle (r, radius);
    }
    g.setColour (selected ? t.accent : t.line.withAlpha (t.glass ? 0.7f : 1.0f));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, selected ? 1.5f : 1.0f);
}

void row (juce::Graphics& g, juce::Rectangle<float> r, State s)
{
    const auto& t = theme();
    if (! s.on && ! s.hover)
        return;
    if (t.glass)
    {
        g.setColour (s.on ? t.accentFill.withAlpha (t.isLight() ? 0.18f : 0.24f) : t.panel2.withAlpha (0.7f));
        g.fillRoundedRectangle (r, t.metrics.controlRadius);
        return;
    }
    g.setColour (s.on ? t.panel2 : t.panel);
    g.fillRoundedRectangle (r, t.radius * 0.8f);
    if (s.on)
    {
        g.setColour (t.accent);
        g.fillRect (r.getX(), r.getY() + 3.0f, 2.0f, r.getHeight() - 6.0f);
    }
}

void focusRing (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    g.setColour (theme().accent);
    g.drawRoundedRectangle (r.expanded (1.5f), radius + 1.5f, 2.0f);
}
} // namespace ui::kit
