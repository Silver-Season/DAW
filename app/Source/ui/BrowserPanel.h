#pragma once

#include "UiCommon.h"

#include "../browser/BrowserActions.h"
#include "../browser/BrowserSources.h"

class AudioEngine;
class Settings;

namespace ui
{
// The left panel (docs/spec/01-layout-project.md, task 1.5): search, the list of places to look in (recent
// items, colour labels, the eight categories) and the items of the chosen one. Items drag out into the
// Session View and the device chain; a double click or Enter puts the selected one on the selected track.
class BrowserPanel final : public juce::Component, public HelpProvider, private juce::TextEditor::Listener, private juce::KeyListener,
                           private juce::ScrollBar::Listener
{
public:
    static constexpr int kMinWidth = 180, kMaxWidth = 420, kDefaultWidth = 236;

    BrowserPanel (AudioEngine&, Settings&, browser::BrowserSources&, library::UserLibrary&);
    ~BrowserPanel() override;

    void focusSearch();
    juce::String searchText() const { return search.getText(); }
    void setSearchText (const juce::String&);
    int numCategories() const { return browser::kNumCategories; }
    void selectCategory (browser::Category);
    // The rows shown now (items only), for tests and the screenshot script.
    int numItemRows() const;
    juce::String selectedItemName() const;
    void selectItemByName (const juce::String&);

    // Double click / Enter on an item.
    std::function<void (const browser::DragItem&)> onActivate;
    // The user changed the library from here (renamed or deleted a preset): other views refresh.
    std::function<void()> onLibraryEdited;
    // An item was used (dropped or activated): it joins the recent list.
    void noteUsed (const juce::String& key);
    // The catalog changed under us.
    void sourcesChanged();
    void stopPreview();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;
    juce::String helpAt (juce::Point<int>) const override;

private:
    struct Nav
    {
        enum Kind { recent, label, category } kind = category;
        int index = 0; // label colour or category
    };
    struct Row
    {
        enum Kind { header, entry, action, message } kind = entry;
        browser::Item item;
        juce::String text;
    };

    // juce::TextEditor::Listener / KeyListener / ScrollBar::Listener
    void textEditorTextChanged (juce::TextEditor&) override;
    void textEditorEscapeKeyPressed (juce::TextEditor&) override;
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;
    void scrollBarMoved (juce::ScrollBar*, double) override;
    void lookAndFeelChanged() override;

    std::vector<Nav> navEntries() const;
    juce::String navTitle (const Nav&) const;
    int navCount (const Nav&) const;
    juce::Rectangle<int> navRect (int index) const;
    int navAt (juce::Point<int>) const;
    void rebuildRows();
    int contentHeight() const;
    int rowAt (juce::Point<int>) const;
    juce::Rectangle<int> rowRect (int index) const;
    void select (int rowIndex, bool preview);
    void moveSelection (int delta);
    void ensureVisible (int rowIndex);
    void previewSelected();
    void showItemMenu (const Row&, juce::Point<int> screenPosition);
    void addPlaceFolder();
    void activate (const browser::Item&);
    juce::Image dragImage (const juce::String& text) const;
    juce::Rectangle<int> previewButtonRect() const;

    AudioEngine& engine;
    Settings& settings;
    browser::BrowserSources& sources;
    library::UserLibrary& library;

    juce::TextEditor search;
    juce::ScrollBar vbar { true };
    std::vector<Row> rows;
    juce::Rectangle<int> searchArea, navArea, listArea, bottomArea;
    Nav current { Nav::category, 1 }; // what the list shows: the presets, to begin with
    int hoveredNav = -1, hoveredRow = -1, selectedRow = -1;
    juce::String selectedKey;
    int scrollY = 0;
    juce::Point<int> pressPosition;
    bool dragStarted = false;
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace ui
