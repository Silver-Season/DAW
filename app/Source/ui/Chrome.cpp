#include "Chrome.h"

#include "../Strings.h"

#include <BinaryData.h>
#include <map>

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
    if (t.glass)
    {
        // Glass: a status bar. The device on the left, divided by thin lines; the main shortcuts on the right while
        // no hint is shown; the hint or a message in the middle.
        area = area.withTrimmedLeft (6).withTrimmedRight (6);
        g.setFont (t.uiFont (12.0f));
        for (int i = 0; i < status.size(); ++i)
        {
            const int w = (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), status[i])) + 2;
            g.setColour (t.dimText);
            g.drawText (status[i], area.removeFromLeft (w), juce::Justification::centredLeft, false);
            area.removeFromLeft (12);
            g.setColour (t.line);
            g.fillRect (area.getX(), getHeight() / 2 - 6, 1, 12);
            area.removeFromLeft (13);
        }
        if (message.isEmpty() && helpText.isEmpty())
        {
            const std::pair<const char*, const juce::String*> keys[] {
                { "\xe2\x8c\x98" "Z", &Strings::transport::keyUndo }, { "\xe2\x8c\x98\xe2\x87\xa7" "Z", &Strings::transport::keyRedo },
                { "Space", &Strings::transport::keyPlay }, { "F9", &Strings::transport::keyRecord }, { "\xe2\x8c\xa5\xe2\x8c\x98" "B", &Strings::transport::keyBrowser }
            };
            auto right = area;
            for (int i = (int) std::size (keys) - 1; i >= 0; --i)
            {
                const auto key = juce::String::fromUTF8 (keys[i].first);
                const auto label = *keys[i].second;
                const auto labelFont = t.uiFont (12.0f), keyFont = t.uiFont (11.0f, 500);
                const int lw = (int) std::ceil (juce::GlyphArrangement::getStringWidth (labelFont, label)) + 2;
                const int kw = (int) std::ceil (juce::GlyphArrangement::getStringWidth (keyFont, key)) + 10;
                if (right.getWidth() < lw + kw + 30)
                    break;
                g.setColour (t.dimText);
                g.setFont (labelFont);
                g.drawText (label, right.removeFromRight (lw), juce::Justification::centredRight, false);
                right.removeFromRight (6);
                const auto chip = right.removeFromRight (kw).toFloat().withSizeKeepingCentre ((float) kw, 18.0f);
                g.setColour (t.panel2.withAlpha (0.8f));
                g.fillRoundedRectangle (chip, 4.0f);
                g.setColour (t.text);
                g.setFont (keyFont);
                g.drawText (key, chip, juce::Justification::centred, false);
                right.removeFromRight (16);
            }
            return;
        }
    }
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
ComingSoonView::ComingSoonView (const juce::String& t, const juce::String& x, const juce::String& action) : title (t), text (x)
{
    button.setButtonText (action);
    button.onClick = [this] { if (onAction) onAction(); };
    addAndMakeVisible (button);
}

void ComingSoonView::resized()
{
    card = juce::Rectangle<int> (juce::jmin (440, getWidth() - 40), juce::jmin (220, getHeight() - 20)).withCentre (getLocalBounds().getCentre());
    button.setBounds (juce::Rectangle<int> (juce::jmin (260, card.getWidth() - 40), 34).withCentre ({ card.getCentreX(), card.getBottom() - 38 }));
}

void ComingSoonView::paint (juce::Graphics& g)
{
    const auto& t = theme();
    g.fillAll (t.chrome);
    if (ghost)
    {
        juce::Graphics::ScopedSaveState save (g);
        g.setOpacity (t.isLight() ? 0.6f : 0.5f);
        ghost (g, getLocalBounds().toFloat().reduced (12.0f));
    }
    // the ghost: the strips the mixer will have, in the track colours, very faint
    const auto list = columns ? columns() : std::vector<Column>();
    const int count = ghost ? 0 : juce::jmax (6, (int) list.size()); // none when a picture of its own was drawn
    const float stripW = juce::jmin (118.0f, ((float) getWidth() - 24.0f) / (float) count);
    {
        juce::Graphics::ScopedSaveState save (g);
        g.setOpacity (t.isLight() ? 0.55f : 0.45f);
        for (int i = 0; i < count; ++i)
        {
            const auto colour = i < (int) list.size() ? list[(size_t) i].colour : t.line;
            const auto strip = juce::Rectangle<float> (12.0f + stripW * (float) i, 12.0f, stripW - 6.0f, (float) getHeight() - 24.0f);
            g.setColour (t.panel);
            g.fillRoundedRectangle (strip, 10.0f);
            g.setColour (t.line.withAlpha (0.6f));
            g.drawRoundedRectangle (strip, 10.0f, 1.0f);
            g.setColour (colour);
            g.fillRoundedRectangle (strip.getX() + 8.0f, strip.getY() + 10.0f, 4.0f, 18.0f, 2.0f);
            if (i < (int) list.size())
            {
                g.setColour (t.text);
                g.setFont (t.uiFont (12.5f, 600));
                g.drawText (list[(size_t) i].name, strip.withTrimmedLeft (18.0f).withHeight (38.0f).toNearestInt(), juce::Justification::centredLeft, true);
            }
            for (int row = 0; row < 4; ++row) // insert slots
            {
                const auto slot = juce::Rectangle<float> (strip.getX() + 8.0f, strip.getY() + 44.0f + (float) row * 22.0f, strip.getWidth() - 16.0f, 17.0f);
                g.setColour (t.panel2);
                g.fillRoundedRectangle (slot, 5.0f);
                g.setColour (colour.withAlpha (0.8f));
                g.fillRoundedRectangle (slot.getX() + 5.0f, slot.getY() + 4.0f, 3.0f, 9.0f, 1.5f);
            }
            const float knobY = strip.getY() + 150.0f;
            for (int k = 0; k < 2; ++k)
            {
                const auto knob = juce::Rectangle<float> (24.0f, 24.0f).withCentre ({ strip.getX() + strip.getWidth() * (k == 0 ? 0.32f : 0.68f), knobY });
                g.setColour (t.line);
                g.drawEllipse (knob, 2.0f);
                g.setColour (colour);
                juce::Path arc;
                arc.addCentredArc (knob.getCentreX(), knob.getCentreY(), 12.0f, 12.0f, 0.0f, -2.3f, -0.6f + (float) ((i + k) % 3) * 0.7f, true);
                g.strokePath (arc, juce::PathStrokeType (2.0f));
            }
            const float faderTop = knobY + 40.0f, faderBottom = strip.getBottom() - 50.0f;
            if (faderBottom > faderTop + 20.0f)
            {
                const float fx = strip.getCentreX() - 8.0f;
                g.setColour (t.line);
                g.fillRoundedRectangle (fx - 2.0f, faderTop, 4.0f, faderBottom - faderTop, 2.0f);
                const float level = 0.45f + 0.08f * (float) (i % 4);
                g.setColour (colour);
                g.fillRoundedRectangle (fx - 2.0f, faderBottom - (faderBottom - faderTop) * level, 4.0f, (faderBottom - faderTop) * level, 2.0f);
                for (int m = 0; m < 2; ++m)
                {
                    const float mx = strip.getCentreX() + 14.0f + (float) m * 7.0f;
                    g.setColour (t.line.withAlpha (0.7f));
                    g.fillRoundedRectangle (mx, faderTop, 4.0f, faderBottom - faderTop, 2.0f);
                    g.setColour (t.play);
                    const float h = (faderBottom - faderTop) * (0.5f + 0.06f * (float) ((i + m) % 5));
                    g.fillRoundedRectangle (mx, faderBottom - h, 4.0f, h, 2.0f);
                }
            }
        }
    }
    // the card
    const auto c = card.toFloat();
    juce::DropShadow (t.shadow().withAlpha (t.isLight() ? 0.14f : 0.45f), 24, { 0, 6 })
        .drawForPath (g, [&] { juce::Path p; p.addRoundedRectangle (card.toFloat(), 16.0f); return p; }());
    glass::surface (g, c, t.panel, 16.0f);
    g.setColour (t.line.withAlpha (0.7f));
    g.drawRoundedRectangle (c, 16.0f, 1.0f);
    // badge
    const auto badgeText = Strings::transport::comingSoon;
    const auto badgeFont = t.uiFont (11.5f, 600);
    const float bw = juce::GlyphArrangement::getStringWidth (badgeFont, badgeText) + 20.0f;
    const auto badge = juce::Rectangle<float> (bw, 22.0f).withPosition (c.getX() + 24.0f, c.getY() + 22.0f);
    glass::accent (g, badge, t.accentFill, 11.0f);
    g.setColour (t.inkOn (t.accentFill));
    g.setFont (badgeFont);
    g.drawText (badgeText, badge, juce::Justification::centred, false);
    g.setColour (t.text);
    g.setFont (t.uiFont (22.0f, 600));
    g.drawText (title, c.withTrimmedLeft (24.0f).withTrimmedTop (54.0f).withHeight (32.0f).toNearestInt(), juce::Justification::centredLeft, false);
    g.setColour (t.dimText);
    g.setFont (t.uiFont (13.5f));
    g.drawFittedText (text, c.reduced (24.0f, 0.0f).withTrimmedTop (90.0f).withHeight (58.0f).toNearestInt(), juce::Justification::topLeft, 3, 1.0f);
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
        // the edge of the plate: hairline, bevel and the glints where its thickness catches the light
        glass::edge (g, r, kRadius, 1.0f);
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
void drawAppIcon (juce::Graphics& g, juce::Rectangle<float> area)
{
    // Scaled down once per pixel size with good resampling; the 1024 px source would alias if drawn directly.
    static juce::Image source = juce::ImageCache::getFromMemory (BinaryData::AppIcon1024_png, BinaryData::AppIcon1024_pngSize);
    static std::map<int, juce::Image> scaled;
    if (! source.isValid())
        return;
    const float density = juce::jlimit (1.0f, 3.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int px = juce::jlimit (8, 512, juce::roundToInt (juce::jmin (area.getWidth(), area.getHeight()) * density));
    auto& image = scaled[px];
    if (! image.isValid())
    {
        image = source;
        while (image.getWidth() / 2 >= px) // halve step by step, then the last step: smooth edges at any size
            image = image.rescaled (image.getWidth() / 2, image.getHeight() / 2, juce::Graphics::highResamplingQuality);
        image = image.rescaled (px, px, juce::Graphics::highResamplingQuality);
    }
    g.setOpacity (1.0f);
    g.drawImage (image, area.withSizeKeepingCentre (juce::jmin (area.getWidth(), area.getHeight()), juce::jmin (area.getWidth(), area.getHeight())));
}

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
