#include "ThemeLookAndFeel.h"

ThemeLookAndFeel::ThemeLookAndFeel()
{
    refresh();
}

void ThemeLookAndFeel::refresh()
{
    const auto& t = theme();
    setDefaultSansSerifTypeface (t.uiFont (14.0f).getTypefacePtr());

    setColour (juce::ResizableWindow::backgroundColourId, t.chrome);
    setColour (juce::DocumentWindow::textColourId, t.text);

    setColour (juce::PopupMenu::backgroundColourId, t.panel);
    setColour (juce::PopupMenu::textColourId, t.text);
    setColour (juce::PopupMenu::headerTextColourId, t.dimText);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, t.accent.withAlpha (0.22f));
    setColour (juce::PopupMenu::highlightedTextColourId, t.text);

    setColour (juce::TextEditor::backgroundColourId, t.panel);
    setColour (juce::TextEditor::textColourId, t.text);
    setColour (juce::TextEditor::highlightColourId, t.accent.withAlpha (0.35f));
    setColour (juce::TextEditor::highlightedTextColourId, t.text);
    setColour (juce::TextEditor::outlineColourId, t.line);
    setColour (juce::TextEditor::focusedOutlineColourId, t.accent);
    setColour (juce::CaretComponent::caretColourId, t.accent);

    setColour (juce::Label::textColourId, t.text);
    setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);

    setColour (juce::TextButton::buttonColourId, t.panel);
    setColour (juce::TextButton::buttonOnColourId, t.accent);
    setColour (juce::TextButton::textColourOffId, t.text);
    setColour (juce::TextButton::textColourOnId, t.clipInk);
    setColour (juce::ToggleButton::textColourId, t.text);
    setColour (juce::ToggleButton::tickColourId, t.accent);
    setColour (juce::ToggleButton::tickDisabledColourId, t.dimText);

    setColour (juce::ComboBox::backgroundColourId, t.panel);
    setColour (juce::ComboBox::textColourId, t.text);
    setColour (juce::ComboBox::outlineColourId, t.line);
    setColour (juce::ComboBox::arrowColourId, t.accent);

    setColour (juce::ScrollBar::thumbColourId, t.dimText.withAlpha (0.55f));
    setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);

    setColour (juce::ListBox::backgroundColourId, t.chrome);
    setColour (juce::ListBox::textColourId, t.text);
    setColour (juce::ListBox::outlineColourId, t.line);

    setColour (juce::Slider::backgroundColourId, t.panel2);
    setColour (juce::Slider::trackColourId, t.accent);
    setColour (juce::Slider::thumbColourId, t.text);
    setColour (juce::Slider::textBoxTextColourId, t.text);
    setColour (juce::Slider::textBoxBackgroundColourId, t.panel);
    setColour (juce::Slider::textBoxOutlineColourId, t.line);

    setColour (juce::AlertWindow::backgroundColourId, t.panel);
    setColour (juce::AlertWindow::textColourId, t.text);
    setColour (juce::AlertWindow::outlineColourId, t.line);

    setColour (juce::TooltipWindow::backgroundColourId, t.panel2);
    setColour (juce::TooltipWindow::textColourId, t.text);
    setColour (juce::TooltipWindow::outlineColourId, t.line);

    setColour (juce::TableHeaderComponent::backgroundColourId, t.panel);
    setColour (juce::TableHeaderComponent::textColourId, t.dimText);
    setColour (juce::TableHeaderComponent::outlineColourId, t.line);
    setColour (juce::TableHeaderComponent::highlightColourId, t.panel2);
    setColour (juce::HyperlinkButton::textColourId, t.accent);
}

juce::Font ThemeLookAndFeel::getPopupMenuFont()
{
    return theme().uiFont (13.5f);
}

juce::Font ThemeLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return theme().uiFont (juce::jmin (13.0f, (float) buttonHeight * 0.5f + 1.0f), 500);
}

juce::Font ThemeLookAndFeel::getMenuBarFont (juce::MenuBarComponent&, int, const juce::String&)
{
    return theme().uiFont (13.0f, 500);
}

void ThemeLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto& t = theme();
    const auto r = juce::Rectangle<float> ((float) width, (float) height);
    g.setColour (t.panel);
    g.fillRoundedRectangle (r, juce::jmin (t.radius, 6.0f));
    g.setColour (t.line);
    g.drawRoundedRectangle (r.reduced (0.5f), juce::jmin (t.radius, 6.0f), 1.0f);
}

void ThemeLookAndFeel::drawMenuBarBackground (juce::Graphics& g, int width, int height, bool, juce::MenuBarComponent&)
{
    const auto& t = theme();
    g.fillAll (t.chrome);
    g.setColour (t.line);
    g.fillRect (0, height - 1, width, 1);
}

void ThemeLookAndFeel::drawMenuBarItem (juce::Graphics& g, int width, int height, int, const juce::String& text,
                                        bool isMouseOverItem, bool isMenuOpen, bool, juce::MenuBarComponent&)
{
    const auto& t = theme();
    auto area = juce::Rectangle<int> (width, height).reduced (2, 3);
    if (isMenuOpen || isMouseOverItem)
    {
        g.setColour (t.panel2);
        g.fillRoundedRectangle (area.toFloat(), t.radius);
    }
    g.setColour (t.text);
    g.setFont (t.uiFont (13.0f, 500));
    g.drawFittedText (text, area, juce::Justification::centred, 1);
}

void ThemeLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool isVertical,
                                      int thumbStart, int thumbSize, bool isMouseOver, bool isMouseDown)
{
    const auto& t = theme();
    auto thumb = isVertical ? juce::Rectangle<int> (x + 2, y + thumbStart, width - 4, thumbSize)
                            : juce::Rectangle<int> (x + thumbStart, y + 2, thumbSize, height - 4);
    g.setColour (t.dimText.withAlpha (isMouseDown ? 0.9f : (isMouseOver ? 0.7f : 0.45f)));
    g.fillRoundedRectangle (thumb.toFloat(), 3.0f);
}

void ThemeLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& background, bool over, bool down)
{
    const auto& t = theme();
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto fill = background;
    if (b.getToggleState())
        fill = findColour (juce::TextButton::buttonOnColourId);
    else if (down)
        fill = t.panel2.brighter (0.1f);
    else if (over)
        fill = background.interpolatedWith (t.panel2, 0.7f);
    g.setColour (fill.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
    g.fillRoundedRectangle (r, t.radius);
    g.setColour (t.line);
    g.drawRoundedRectangle (r, t.radius, 1.0f);
}
