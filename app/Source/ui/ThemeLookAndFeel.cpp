#include "ThemeLookAndFeel.h"
#include "GlassMaterial.h"

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
    if (t.glass)
    {
        setColour (juce::TextButton::buttonOnColourId, t.accentFill);
        setColour (juce::TextButton::textColourOnId, t.inkOn (t.accentFill));
        setColour (juce::Slider::trackColourId, t.accentFill);
    }
}

void ThemeLookAndFeel::refreshComponent (juce::Component& component, const Theme& previous)
{
    const auto& next = theme();
    const std::pair<juce::Colour, juce::Colour> colours[] {
        { previous.text, next.text }, { previous.dimText, next.dimText },
        { previous.panel, next.panel }, { previous.panel2, next.panel2 },
        { previous.chrome, next.chrome }, { previous.line, next.line },
        { previous.accent, next.accent }, { previous.accent2, next.accent2 }
    };
    for (int id : std::initializer_list<int> { juce::Label::textColourId, juce::ComboBox::backgroundColourId, juce::ComboBox::outlineColourId,
                    juce::ComboBox::textColourId, juce::ComboBox::arrowColourId, juce::TextEditor::backgroundColourId,
                    juce::TextEditor::outlineColourId, juce::TextEditor::focusedOutlineColourId })
        if (component.isColourSpecified (id))
            for (const auto& pair : colours)
                if (component.findColour (id) == pair.first)
                {
                    component.setColour (id, pair.second);
                    break;
                }
    if (auto* editor = dynamic_cast<juce::TextEditor*> (&component))
        editor->applyColourToAllText (next.text);
    for (auto* child : component.getChildren())
        refreshComponent (*child, previous);
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
    if (t.glass)
    {
        if (b.getToggleState ())
            ui::glass::accent (g, r, fill, t.radius);
        else
            ui::glass::surface (g, r, fill.withMultipliedAlpha (b.isEnabled () ? 1.0f : 0.5f), t.radius);
    }
    else
    {
        g.setColour (fill.withMultipliedAlpha (b.isEnabled () ? 1.0f : 0.5f));
        g.fillRoundedRectangle (r, t.radius);
    }
    g.setColour (t.line);
    g.drawRoundedRectangle (r, t.radius, 1.0f);
    if (b.hasKeyboardFocus (false))
    {
        g.setColour (t.accent);
        g.drawRoundedRectangle (r.reduced (1.0f), t.radius, 2.0f);
    }
}

void
ThemeLookAndFeel::drawLinearSlider (juce::Graphics& g,
                                    int x,
                                    int y,
                                    int w,
                                    int h,
                                    float pos,
                                    float lo,
                                    float hi,
                                    juce::Slider::SliderStyle style,
                                    juce::Slider& s)
{
    const auto& t = theme ();
    if (!t.glass || (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearVertical))
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, lo, hi, style, s);
        return;
    }
    const bool vertical = style == juce::Slider::LinearVertical;
    const auto bounds = juce::Rectangle<float> ((float)x, (float)y, (float)w, (float)h);
    const auto track = vertical ? bounds.withSizeKeepingCentre (4.0f, bounds.getHeight ())
                                : bounds.withSizeKeepingCentre (bounds.getWidth (), 4.0f);
    g.setColour (t.line);
    g.fillRoundedRectangle (track, 2.0f);
    const auto filled = vertical ? track.withTop (pos) : track.withRight (pos);
    ui::glass::accent (g, filled, s.isEnabled () ? t.accentFill : t.dimText, 2.0f);
    const auto centre =
      vertical ? juce::Point<float> (bounds.getCentreX (), pos) : juce::Point<float> (pos, bounds.getCentreY ());
    ui::glass::lens (g, juce::Rectangle<float> (22.0f, 18.0f).withCentre (centre), t.accentFill, vertical);
}

void
ThemeLookAndFeel::drawRotarySlider (juce::Graphics& g,
                                    int x,
                                    int y,
                                    int w,
                                    int h,
                                    float pos,
                                    float start,
                                    float end,
                                    juce::Slider& s)
{
    if (!theme ().glass)
    {
        LookAndFeel_V4::drawRotarySlider (g, x, y, w, h, pos, start, end, s);
        return;
    }
    const float diameter = juce::jmax (12.0f, (float)juce::jmin (w, h) - 5.0f);
    ui::glass::knob (g,
                     juce::Rectangle<float> (diameter, diameter)
                       .withCentre ({ (float)x + (float)w * 0.5f, (float)y + (float)h * 0.5f }),
                     pos,
                     s.isEnabled () ? theme ().accentFill : theme ().dimText,
                     s.getMinimum () < 0.0 && s.getMaximum () > 0.0);
}

void
ThemeLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool down)
{
    const auto& t = theme ();
    if (!t.glass)
    {
        LookAndFeel_V4::drawToggleButton (g, b, over, down);
        return;
    }
    const auto track = juce::Rectangle<float> (32.0f, 18.0f).withCentre ({ 19.0f, (float)b.getHeight () * 0.5f });
    if (b.getToggleState ())
        ui::glass::accent (g, track, t.accentFill, 9.0f);
    else
    {
        g.setColour (t.line);
        g.fillRoundedRectangle (track, 9.0f);
    }
    ui::glass::thumb (g,
                      juce::Rectangle<float> (16.0f, 16.0f)
                        .withCentre ({ track.getX () + (b.getToggleState () ? 23.0f : 9.0f), track.getCentreY () }));
    g.setColour (b.isEnabled () ? t.text : t.dimText);
    g.setFont (t.uiFont (12.0f));
    g.drawFittedText (
      b.getButtonText (), b.getLocalBounds ().withTrimmedLeft (43), juce::Justification::centredLeft, 2);
    if (b.hasKeyboardFocus (false))
    {
        g.setColour (t.accent);
        g.drawRoundedRectangle (b.getLocalBounds ().toFloat ().reduced (1.0f), t.radius, 2.0f);
    }
}

void
ThemeLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool down, int buttonX, int buttonY,
                                int buttonW, int buttonH, juce::ComboBox& box)
{
    const auto& t = theme ();
    if (!t.glass)
    {
        LookAndFeel_V4::drawComboBox (g, width, height, down, buttonX, buttonY, buttonW, buttonH, box);
        return;
    }
    const auto r = juce::Rectangle<float> ((float)width, (float)height).reduced (0.5f);
    ui::glass::surface (g, r, box.findColour (juce::ComboBox::backgroundColourId), t.radius);
    g.setColour (box.hasKeyboardFocus (true) ? t.accent : t.line);
    g.drawRoundedRectangle (r, t.radius, 1.0f);
    juce::Path arrow;
    const auto centre = juce::Point<float> ((float)width - 15.0f, (float)height * 0.5f);
    arrow.startNewSubPath (centre.x - 4.0f, centre.y - 2.0f);
    arrow.lineTo (centre.x, centre.y + 2.0f);
    arrow.lineTo (centre.x + 4.0f, centre.y - 2.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId).withAlpha (box.isEnabled () ? 0.9f : 0.3f));
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void
ThemeLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    const auto& t = theme ();
    if (!t.glass)
    {
        LookAndFeel_V4::fillTextEditorBackground (g, width, height, editor);
        return;
    }
    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (juce::Rectangle<float> ((float)width, (float)height).reduced (0.5f), t.radius * 0.8f);
}

void
ThemeLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    const auto& t = theme ();
    if (!t.glass)
    {
        LookAndFeel_V4::drawTextEditorOutline (g, width, height, editor);
        return;
    }
    if (!editor.isEnabled ())
        return;
    const bool focused = editor.hasKeyboardFocus (true) && !editor.isReadOnly ();
    g.setColour (editor.findColour (focused ? juce::TextEditor::focusedOutlineColourId : juce::TextEditor::outlineColourId));
    g.drawRoundedRectangle (juce::Rectangle<float> ((float)width, (float)height).reduced (focused ? 1.0f : 0.5f),
                            t.radius * 0.8f, focused ? 2.0f : 1.0f);
}
