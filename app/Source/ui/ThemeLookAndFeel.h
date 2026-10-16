#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Standard JUCE widgets (menus, dialogs, text fields, scroll bars) in the colours and fonts of the current
// theme. Installed as the default look and feel; refresh() runs after every theme change.
class ThemeLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ThemeLookAndFeel();

    // Re-reads colours and the default font from the current theme.
    void refresh();

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawMenuBarBackground (juce::Graphics&, int width, int height, bool isMouseOverBar, juce::MenuBarComponent&) override;
    void drawMenuBarItem (juce::Graphics&, int width, int height, int itemIndex, const juce::String& itemText,
                          bool isMouseOverItem, bool isMenuOpen, bool isMouseOverBar, juce::MenuBarComponent&) override;
    juce::Font getMenuBarFont (juce::MenuBarComponent&, int itemIndex, const juce::String& itemText) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                        int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;
    int getDefaultScrollbarWidth() override { return 9; }
    juce::Font getPopupMenuFont() override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour, bool isMouseOver, bool isButtonDown) override;
};
