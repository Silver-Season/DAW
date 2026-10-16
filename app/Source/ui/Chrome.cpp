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
    g.fillAll (t.chrome);
    g.setColour (t.line);
    g.fillRect (0, 0, getWidth(), 1);

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
    const auto& t = theme();
    const float s = juce::jmin (area.getWidth(), area.getHeight()) / 44.0f;
    const auto origin = area.getTopLeft();
    auto pt = [&] (float x, float y) { return juce::Point<float> (origin.x + x * s, origin.y + y * s); };

    juce::Path tri;
    tri.startNewSubPath (pt (22, 5));
    tri.lineTo (pt (39, 37));
    tri.lineTo (pt (5, 37));
    tri.closeSubPath();
    g.setColour (t.text.withAlpha (0.9f));
    g.fillPath (tri);

    const int rayHues[6] { 0, 2, 4, 7, 9, 12 };
    for (int k = 0; k < 6; ++k)
    {
        g.setColour (juce::Colour::fromHSL (t.hues[(size_t) rayHues[k]] / 360.0f, 0.9f, 0.62f, 1.0f));
        const auto a = pt (30.0f, 21.0f + (float) k * 2.4f);
        g.fillRect (juce::Rectangle<float> (a.x, a.y, 14.0f * s, 2.2f * s));
    }
}
} // namespace ui
