#include "BrowserPanel.h"

#include "../Strings.h"

namespace ui
{
namespace
{
constexpr int kRowHeight = 30;
}

BrowserPanel::BrowserPanel()
{
    namespace B = Strings::browser;
    categories = { { B::instruments, -1 }, { B::presets, -2 }, { B::effects, 2 }, { B::plugins, 7 },
                   { B::sounds, 10 },      { B::midiFiles, 0 }, { B::places, -3 } };
    setHelpText (Strings::help::browserPanel);

    search.setMultiLine (false);
    search.setReturnKeyStartsNewLine (false);
    search.setScrollbarsShown (false);
    search.setBorder ({ 0, 0, 0, 0 });
    search.setTextToShowWhenEmpty (B::searchPlaceholder, theme().dimText);
    search.setHelpText (Strings::help::browserSearch);
    search.addListener (this);
    addAndMakeVisible (search);
    lookAndFeelChanged();
}

void BrowserPanel::lookAndFeelChanged()
{
    const auto& t = theme();
    search.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::textColourId, t.text);
    search.setTextToShowWhenEmpty (Strings::browser::searchPlaceholder, t.dimText);
    search.applyFontToAllText (t.uiFont (12.5f));
    search.setFont (t.uiFont (12.5f));
}

void BrowserPanel::focusSearch()
{
    search.grabKeyboardFocus();
    search.selectAll();
}

void BrowserPanel::textEditorTextChanged (juce::TextEditor&)
{
    repaint();
}

void BrowserPanel::textEditorEscapeKeyPressed (juce::TextEditor&)
{
    search.clear();
    search.giveAwayKeyboardFocus();
    if (auto* top = getTopLevelComponent())
        top->grabKeyboardFocus();
}

juce::Rectangle<int> BrowserPanel::categoryRect (int index) const
{
    return { 6, searchArea.getBottom() + 8 + index * kRowHeight, getWidth() - 12, kRowHeight - 1 };
}

int BrowserPanel::categoryAt (juce::Point<int> p) const
{
    for (int i = 0; i < (int) categories.size(); ++i)
        if (categoryRect (i).contains (p))
            return i;
    return -1;
}

void BrowserPanel::resized()
{
    searchArea = { 10, 10, getWidth() - 20, 32 };
    search.setBounds (searchArea.withTrimmedLeft (30).withTrimmedRight (8).reduced (0, 6));
    const int catsBottom = categoryRect ((int) categories.size() - 1).getBottom() + 8;
    itemsArea = { 0, catsBottom, getWidth(), juce::jmax (0, getHeight() - catsBottom) };
}

void BrowserPanel::paint (juce::Graphics& g)
{
    const auto& t = theme();
    g.fillAll (t.chrome);

    fillCell (g, searchArea.toFloat(), t.panel);
    strokeCell (g, searchArea.toFloat(), search.hasKeyboardFocus (true) ? t.accent : t.line);
    g.setColour (t.dimText);
    g.fillPath (icons::search (juce::Rectangle<float> (13.0f, 13.0f).withCentre ({ (float) searchArea.getX() + 17.0f, (float) searchArea.getCentreY() })));

    const bool searching = search.getText().isNotEmpty();
    for (int i = 0; i < (int) categories.size(); ++i)
    {
        const auto r = categoryRect (i);
        if (i == selected && ! searching)
            fillCell (g, r.toFloat(), t.panel2, 0.8f);
        else if (i == hovered)
            fillCell (g, r.toFloat(), t.panel, 0.8f);

        const auto& c = categories[(size_t) i];
        juce::Colour dot = t.dimText;
        if (c.paletteIndex >= 0)
            dot = t.palette (c.paletteIndex);
        else if (c.paletteIndex == -1)
            dot = t.accent;
        else if (c.paletteIndex == -2)
            dot = t.accent2;
        g.setColour (dot);
        g.fillRoundedRectangle (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ (float) r.getX() + 16.0f, (float) r.getCentreY() }), 2.0f);
        g.setColour (t.text);
        g.setFont (t.uiFont (13.0f));
        g.drawText (c.name, r.withTrimmedLeft (32), juce::Justification::centredLeft, true);
    }

    g.setColour (t.line);
    g.fillRect (0, itemsArea.getY() - 1, getWidth(), 1);

    g.setColour (t.dimText);
    g.setFont (t.uiFont (11.0f, 500));
    const auto message = searching ? Strings::browser::notFound (search.getText().trim())
                                   : Strings::browser::emptyCategory + " " + juce::String::fromUTF8 ("\xc2\xab") + categories[(size_t) selected].name
                                         + juce::String::fromUTF8 ("\xc2\xbb");
    g.drawFittedText (message, itemsArea.reduced (14, 12).withHeight (40), juce::Justification::topLeft, 3);
}

void BrowserPanel::mouseDown (const juce::MouseEvent& e)
{
    const int i = categoryAt (e.getPosition());
    if (i >= 0)
    {
        selected = i;
        search.clear();
        repaint();
    }
}

void BrowserPanel::mouseMove (const juce::MouseEvent& e)
{
    const int i = categoryAt (e.getPosition());
    if (i != hovered)
    {
        hovered = i;
        repaint();
    }
}

void BrowserPanel::mouseExit (const juce::MouseEvent&)
{
    if (hovered != -1)
    {
        hovered = -1;
        repaint();
    }
}

juce::String BrowserPanel::helpAt (juce::Point<int> p) const
{
    const int i = categoryAt (p);
    return i >= 0 ? Strings::help::browserCategory (categories[(size_t) i].name) : juce::String();
}
} // namespace ui
