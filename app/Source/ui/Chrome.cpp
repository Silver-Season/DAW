#include "Chrome.h"

#include "../Strings.h"

namespace ui
{
InfoBar::InfoBar (juce::Component& w) : window (w)
{
    setInterceptsMouseClicks (false, false);
    setHelpText (Strings::help::infoBar);
    juce::Desktop::getInstance().addGlobalMouseListener (this);
}

InfoBar::~InfoBar()
{
    juce::Desktop::getInstance().removeGlobalMouseListener (this);
}

juce::String InfoBar::helpFor (juce::Component* component, juce::Point<int> position)
{
    for (auto* c = component; c != nullptr; c = c->getParentComponent())
    {
        const auto local = c->getLocalPoint (component, position);
        if (auto* provider = dynamic_cast<HelpProvider*> (c))
            if (const auto text = provider->helpAt (local); text.isNotEmpty())
                return text;
        if (const auto text = c->getHelpText(); text.isNotEmpty())
            return text;
    }
    return {};
}

void InfoBar::mouseMove (const juce::MouseEvent& e)
{
    auto* c = e.eventComponent;
    if (c == nullptr || (c != &window && ! window.isParentOf (c)))
        return;
    const auto text = helpFor (c, e.getPosition());
    if (text != helpText)
    {
        helpText = text;
        repaint();
    }
}

void InfoBar::showMessage (const juce::String& text, int ms)
{
    message = text;
    startTimer (ms);
    repaint();
}

void InfoBar::timerCallback()
{
    stopTimer();
    message = {};
    repaint();
}

void InfoBar::paint (juce::Graphics& g)
{
    const auto& t = theme();
    g.fillAll (t.glass ? t.background : t.chrome);
    if (! t.glass)
    {
        g.setColour (t.line);
        g.fillRect (0, 0, getWidth(), 1);
    }

    auto area = getLocalBounds().reduced (12, 0);
    if (message.isNotEmpty())
    {
        g.setColour (t.accent);
        g.setFont (t.uiFont (12.0f, 500));
        g.drawText (message, area, juce::Justification::centredLeft, true);
        return;
    }
    if (helpText.isEmpty())
        return;

    const auto title = helpTitle (helpText);
    const auto body = helpBody (helpText);
    const auto titleFont = t.uiFont (12.0f, 600);
    const auto bodyFont = t.uiFont (12.0f);
    const int titleWidth = (int) std::ceil (juce::GlyphArrangement::getStringWidth (titleFont, title)) + 1;
    g.setColour (t.text);
    g.setFont (titleFont);
    g.drawText (title, area.removeFromLeft (titleWidth), juce::Justification::centredLeft, false);
    if (body.isNotEmpty())
    {
        area.removeFromLeft (8);
        g.setColour (t.dimText);
        g.setFont (bodyFont);
        g.drawText (juce::String::fromUTF8 ("\xe2\x80\x94  ") + body, area, juce::Justification::centredLeft, true);
    }
}

//==============================================================================
Splitter::Splitter (bool verticalBar) : vertical (verticalBar)
{
    setMouseCursor (vertical ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::UpDownResizeCursor);
}

void Splitter::paint (juce::Graphics& g)
{
    const auto& t = theme();
    g.setColour (hover || dragging ? t.accent : t.line);
    const auto r = getLocalBounds();
    if (vertical)
        g.fillRect (r.getCentreX(), 0, 1, getHeight());
    else
        g.fillRect (0, r.getCentreY(), getWidth(), 1);
}

void Splitter::mouseDown (const juce::MouseEvent&)
{
    dragging = true;
    if (onDragStart)
        onDragStart();
    repaint();
}

void Splitter::mouseDrag (const juce::MouseEvent& e)
{
    if (onDrag)
        onDrag (vertical ? e.getDistanceFromDragStartX() : e.getDistanceFromDragStartY());
}

void Splitter::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    repaint();
}

//==============================================================================
void EdgeTab::paint (juce::Graphics& g)
{
    const auto& t = theme();
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (hover ? t.panel2 : t.chrome);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (t.line);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);

    const int dir = open ? direction : (direction + 2) % 4;
    const float s = 5.0f;
    const auto icon = juce::Rectangle<float> (s, s).withCentre (r.getCentre());
    g.setColour (hover ? t.accent : t.dimText);
    g.fillPath (icons::triangle (dir % 2 == 0 ? icon.withTrimmedLeft (-1).withTrimmedRight (-1) : icon.withTrimmedTop (-1).withTrimmedBottom (-1), dir));
}

//==============================================================================
void CardFrames::setCards (std::vector<juce::Rectangle<int>> areas)
{
    if (areas != cards)
    {
        cards = std::move (areas);
        repaint();
    }
}

void CardFrames::paint (juce::Graphics& g)
{
    const auto& t = theme();
    if (! t.glass)
        return;
    for (const auto& card : cards)
    {
        if (card.isEmpty())
            continue;
        const auto r = card.toFloat();
        juce::Path rounded;
        rounded.addRoundedRectangle (r, kRadius);
        // corners: the window background outside the rounded shape
        juce::Path corners;
        corners.addRectangle (r);
        corners.setUsingNonZeroWinding (false);
        corners.addPath (rounded);
        g.setColour (t.background);
        g.fillPath (corners);
        // a soft shadow below the card, kept out of the card itself
        {
            juce::Graphics::ScopedSaveState save (g);
            for (const auto& other : cards)
                g.excludeClipRegion (other.reduced (2));
            juce::DropShadow (t.shadow().withAlpha (t.isLight() ? 0.07f : 0.30f), 12, { 0, 3 }).drawForPath (g, rounded);
        }
        // the rim: bright along the top, a quiet line all round
        g.setColour (t.line.withAlpha (t.isLight() ? 0.55f : 0.7f));
        g.strokePath (rounded, juce::PathStrokeType (1.0f));
        g.setGradientFill (juce::ColourGradient (t.highlight().withAlpha (t.isLight() ? 0.9f : 0.16f), 0.0f, r.getY(),
                                                 t.highlight().withAlpha (0.0f), 0.0f, r.getY() + 40.0f, false));
        juce::Path inner;
        inner.addRoundedRectangle (r.reduced (1.0f), kRadius - 1.0f);
        g.strokePath (inner, juce::PathStrokeType (1.0f));
    }
}

//==============================================================================
void ScanlineOverlay::paint (juce::Graphics& g)
{
    const int generation = ThemeManager::get().generation();
    if (tile.isNull() || tileGeneration != generation)
    {
        tile = juce::Image (juce::Image::ARGB, 1, 3, true);
        tile.setPixelAt (0, 1, juce::Colours::black.withAlpha (0.16f));
        tileGeneration = generation;
    }
    g.setTiledImageFill (tile, 0, 0, 1.0f);
    g.fillAll();
}

//==============================================================================
void drawLogo (juce::Graphics& g, juce::Rectangle<float> area)
{
    // The same geometry as media/brand/*.svg: a glass prism, the incoming beam and six spectrum bars like tracks.
    // Drawn in a 100-unit square whose content spans x 3..98, y 22..78; it is fitted into the area by that content.
    const auto& t = theme();
    const juce::Rectangle<float> content (3.0f, 20.0f, 96.0f, 60.0f);
    const float s = juce::jmin (area.getWidth() / content.getWidth(), area.getHeight() / content.getHeight());
    const auto origin = area.getCentre() - content.getCentre() * s;
    auto pt = [&] (float x, float y) { return juce::Point<float> (origin.x + x * s, origin.y + y * s); };
    auto px = [&] (float units) { return juce::jmax (1.0f, units * s); };

    const juce::Point<float> a (40, 22), b (68, 78), c (12, 78), p (46, 64);
    auto rightEdge = [&] (float y) { return a.x + (b.x - a.x) * (y - a.y) / (b.y - a.y); };
    const float beamY = 50.0f, beamX = a.x + (c.x - a.x) * (beamY - a.y) / (c.y - a.y);
    auto path = [&] (std::initializer_list<juce::Point<float>> pts, bool closed) {
        juce::Path r;
        bool first = true;
        for (auto q : pts)
        {
            const auto w = pt (q.x, q.y);
            if (first) r.startNewSubPath (w); else r.lineTo (w);
            first = false;
        }
        if (closed) r.closeSubPath();
        return r;
    };

    // Spectrum bars first: the glass in front lets their roots show through.
    static constexpr float barY[6] { 42, 48, 54, 60, 66, 72 }, barEnd[6] { 96, 98.5f, 95, 98.5f, 94, 90 };
    const int barHues[6] { 0, 2, 4, 7, 9, 12 };
    const float barH = juce::jmax (1.2f, 3.4f * s);
    for (int k = 0; k < 6; ++k)
    {
        const auto colour = juce::Colour::fromHSL (t.hues[(size_t) barHues[k]] / 360.0f, 0.95f, 0.58f, 1.0f);
        const auto from = pt (rightEdge (barY[k]) - 1.5f, barY[k]);
        const auto bar = juce::Rectangle<float> (from.x, from.y - barH * 0.5f, pt (barEnd[k], 0).x - from.x, barH);
        if (t.glow > 0.0f)
            for (float spread : { 2.2f, 1.2f })
            {
                g.setColour (colour.withAlpha (0.16f * t.glow));
                g.fillRoundedRectangle (bar.expanded (barH * spread * 0.5f), barH * (0.5f + spread * 0.5f));
            }
        g.setColour (colour);
        g.fillRoundedRectangle (bar, barH * 0.5f);
    }

    const auto ink = t.text;
    g.setColour (ink);
    g.drawLine (juce::Line<float> (pt (3, beamY), pt (beamX, beamY)), px (2.2f));

    g.setColour (ink.withAlpha (0.30f));
    g.fillPath (path ({ a, c, p }, true));
    g.setColour (ink.withAlpha (0.16f));
    g.fillPath (path ({ a, p, b }, true));
    g.setColour (ink.withAlpha (0.08f));
    g.fillPath (path ({ c, b, p }, true));

    // The inner edges and the refracted beam only read at icon sizes; in the top bar they turn into noise.
    if (s >= 0.6f)
    {
        g.setColour (ink.withAlpha (0.75f));
        g.drawLine (juce::Line<float> (pt (beamX, beamY), pt (rightEdge (57), 57)), px (1.4f));
        const juce::PathStrokeType thin (px (0.9f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour (ink.withAlpha (0.6f));
        g.strokePath (path ({ a, p, b }, false), thin);
        g.strokePath (path ({ p, c }, false), thin);
    }

    const auto outline = path ({ a, b, c }, true);
    if (t.glow > 0.0f)
    {
        g.setColour (ink.withAlpha (0.18f * t.glow));
        g.strokePath (outline, juce::PathStrokeType (px (4.0f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (ink);
    g.strokePath (outline, juce::PathStrokeType (px (1.6f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
} // namespace ui
