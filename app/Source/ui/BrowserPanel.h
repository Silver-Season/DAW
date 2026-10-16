#pragma once

#include "UiCommon.h"

namespace ui
{
// The left panel: a search field and the list of categories. In this part of stage 1 it is only a frame:
// there are no sources behind the categories yet (task 1.5), so the list below them stays empty.
class BrowserPanel final : public juce::Component, public HelpProvider, private juce::TextEditor::Listener
{
public:
    static constexpr int kMinWidth = 180, kMaxWidth = 420, kDefaultWidth = 236;

    BrowserPanel();

    void focusSearch();
    juce::String searchText() const { return search.getText(); }
    int numCategories() const { return (int) categories.size(); }
    int selectedCategory() const { return selected; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    juce::String helpAt (juce::Point<int>) const override;

private:
    struct Category
    {
        juce::String name;
        int paletteIndex; // dot colour from the theme palette; -1 = dim text
    };

    void textEditorTextChanged (juce::TextEditor&) override;
    void textEditorEscapeKeyPressed (juce::TextEditor&) override;
    void lookAndFeelChanged() override;
    juce::Rectangle<int> categoryRect (int index) const;
    int categoryAt (juce::Point<int>) const;

    std::vector<Category> categories;
    juce::TextEditor search;
    juce::Rectangle<int> searchArea, itemsArea;
    int selected = 1, hovered = -1;
};
} // namespace ui
