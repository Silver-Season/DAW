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
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
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

namespace
{
// Glass settings: the line icons of the tabs (Audio, MIDI, Recording, Appearance, Library, Plug-ins).
juce::Path settingsIcon (int index, juce::Rectangle<float> b)
{
    juce::Path p;
    const float x = b.getX(), y = b.getY(), w = b.getWidth(), h = b.getHeight();
    const auto line = [&] (float x0, float y0, float x1, float y1) { p.startNewSubPath (x + w * x0, y + h * y0); p.lineTo (x + w * x1, y + h * y1); };
    switch (index)
    {
        case 0: // audio: waveform bars
            for (auto [k, a] : { std::pair { 0.1f, 0.3f }, { 0.3f, 0.8f }, { 0.5f, 0.5f }, { 0.7f, 0.95f }, { 0.9f, 0.4f } })
                line (k, 0.5f - a * 0.5f, k, 0.5f + a * 0.5f);
            break;
        case 1: // MIDI: a keyboard
            p.addRoundedRectangle (b.reduced (0.5f, 1.5f), 2.0f);
            for (float k : { 0.3f, 0.5f, 0.7f })
                line (k, 0.12f, k, 0.6f);
            break;
        case 2: // recording: a ring with a dot
            p.addEllipse (b.reduced (0.5f));
            p.addEllipse (b.reduced (w * 0.32f));
            break;
        case 3: // appearance: a sparkle
            p.startNewSubPath (x + w * 0.5f, y);
            p.quadraticTo (x + w * 0.56f, y + h * 0.44f, x + w, y + h * 0.5f);
            p.quadraticTo (x + w * 0.56f, y + h * 0.56f, x + w * 0.5f, y + h);
            p.quadraticTo (x + w * 0.44f, y + h * 0.56f, x, y + h * 0.5f);
            p.quadraticTo (x + w * 0.44f, y + h * 0.44f, x + w * 0.5f, y);
            break;
        case 4: // library: a book
            p.addRoundedRectangle (x + w * 0.15f, y, w * 0.7f, h, 2.0f);
            line (0.32f, 0.0f, 0.32f, 1.0f);
            break;
        default: // plug-ins: a plug
            p.addRoundedRectangle (x + w * 0.2f, y + h * 0.3f, w * 0.6f, h * 0.4f, 2.0f);
            line (0.38f, 0.05f, 0.38f, 0.3f);
            line (0.62f, 0.05f, 0.62f, 0.3f);
            line (0.5f, 0.7f, 0.5f, 0.98f);
            break;
    }
    juce::Path stroked;
    juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (stroked, p);
    return stroked;
}

// A small picture of a theme: its window background, title dots, a side column, the transport and a few clips.
void drawThemeThumbnail (juce::Graphics& g, juce::Rectangle<float> r, const Theme& th)
{
    g.setColour (th.background);
    g.fillRoundedRectangle (r, 8.0f);
    const auto inner = r.reduced (5.0f);
    for (int i = 0; i < 3; ++i)
    {
        const juce::Colour dots[] { juce::Colour (0xffff5f57), juce::Colour (0xfffebc2e), juce::Colour (0xff28c840) };
        g.setColour (dots[i]);
        g.fillEllipse (inner.getX() + (float) i * 7.0f, inner.getY(), 4.5f, 4.5f);
    }
    g.setColour (th.chrome);
    g.fillRoundedRectangle (inner.getX() + 26.0f, inner.getY() - 0.5f, inner.getWidth() - 26.0f, 5.5f, 2.5f);
    const auto body = inner.withTrimmedTop (9.0f);
    g.setColour (th.chrome);
    g.fillRoundedRectangle (body.withWidth (body.getWidth() * 0.22f), 3.0f);
    const auto main = body.withTrimmedLeft (body.getWidth() * 0.26f);
    g.setColour (th.panel);
    g.fillRoundedRectangle (main, 3.0f);
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 4; ++col)
        {
            if ((row + col) % 3 == 2)
                continue;
            const float cw = (main.getWidth() - 10.0f) / 4.0f, ch = (main.getHeight() - 8.0f) / 3.0f;
            g.setColour (th.palette ((col * 4 + row) % 16).withAlpha (th.glass ? 0.75f : 1.0f));
            g.fillRoundedRectangle (main.getX() + 3.0f + (float) col * (cw + 1.3f), main.getY() + 3.0f + (float) row * (ch + 1.0f), cw, ch - 1.0f, 1.5f);
        }
}
} // namespace

void ThemeLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool down)
{
    const auto& t = theme();
    if (t.glass && b.getProperties().contains ("themeCard"))
    {
        g.setColour (b.getToggleState() ? t.accent : t.text);
        g.setFont (t.uiFont (12.0f, b.getToggleState() ? 600 : 500));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().removeFromBottom (18), juce::Justification::centred, 1, 1.0f);
        return;
    }
    if (t.glass && b.getProperties().contains ("primary"))
    {
        g.setColour (b.isEnabled() ? t.inkOn (t.accentFill) : t.dimText);
        g.setFont (t.uiFont (13.0f, 600));
        g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
        return;
    }
    if (t.glass && b.getProperties().contains ("navIcon"))
    {
        const auto r = b.getLocalBounds().toFloat();
        g.setColour (t.text.withAlpha (b.getToggleState() ? 1.0f : 0.8f));
        g.fillPath (settingsIcon ((int) b.getProperties()["navIcon"], juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ r.getX() + 19.0f, r.getCentreY() })));
        g.setColour (t.text);
        g.setFont (t.uiFont (14.0f, b.getToggleState() ? 600 : 400));
        g.drawText (b.getButtonText(), b.getLocalBounds().withTrimmedLeft (38), juce::Justification::centredLeft, true);
        return;
    }
    LookAndFeel_V4::drawButtonText (g, b, over, down);
}

void ThemeLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& background, bool over, bool down)
{
    const auto& t = theme();
    if (t.glass && b.getProperties().contains ("themeCard"))
    {
        auto pic = b.getLocalBounds().toFloat().withTrimmedBottom (20.0f).reduced (1.5f);
        for (const auto& th : ThemeManager::get().all())
            if (th.id == b.getProperties()["themeCard"].toString())
                drawThemeThumbnail (g, pic, th);
        g.setColour (b.getToggleState() ? t.accent : (over ? t.dimText : t.line));
        g.drawRoundedRectangle (pic.expanded (b.getToggleState() ? 1.0f : 0.0f), 8.0f, b.getToggleState() ? 2.0f : 1.0f);
        return;
    }
    if (t.glass && b.getProperties().contains ("primary"))
    {
        // the main action of a dialog: accent-filled, with its own contrasting label
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        ui::glass::accent (g, r, b.isEnabled() ? t.accentFill : t.panel2, t.radius);
        if (over || down)
        {
            g.setColour (t.highlight().withAlpha (down ? 0.18f : 0.1f));
            g.fillRoundedRectangle (r, t.radius);
        }
        return;
    }
    if (t.glass && b.getProperties().contains ("navIcon"))
    {
        if (b.getToggleState() || over)
        {
            g.setColour (b.getToggleState() ? t.accentFill.withAlpha (t.isLight() ? 0.18f : 0.24f) : t.panel2.withAlpha (0.7f));
            g.fillRoundedRectangle (b.getLocalBounds().toFloat(), 8.0f);
        }
        return;
    }
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
    // a range across zero fills from the centre (mod amounts), otherwise from the start
    const bool bipolar = s.getMinimum () < 0.0 && s.getMaximum () > 0.0;
    const float zero = bipolar ? (float) s.getPositionOfValue (0.0) : (vertical ? track.getBottom () : track.getX ());
    const auto filled = vertical ? juce::Rectangle<float>::leftTopRightBottom (track.getX (), juce::jmin (pos, zero), track.getRight (), juce::jmax (pos, zero))
                                 : juce::Rectangle<float>::leftTopRightBottom (juce::jmin (pos, zero), track.getY (), juce::jmax (pos, zero), track.getBottom ());
    ui::glass::accent (g, filled, s.isEnabled () ? t.accentFill : t.dimText, 2.0f);
    const auto centre =
      vertical ? juce::Point<float> (bounds.getCentreX (), pos) : juce::Point<float> (pos, bounds.getCentreY ());
    ui::glass::thumb (g, juce::Rectangle<float> (vertical ? 20.0f : 18.0f, vertical ? 14.0f : 18.0f).withCentre (centre));
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
