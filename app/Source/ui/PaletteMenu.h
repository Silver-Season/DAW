#pragma once

#include "UiCommon.h"

namespace ui
{
// The 16 colours of the theme palette as a grid inside a popup menu: one click picks a colour and closes
// the menu, no dialog (docs/spec/99-details.md, stage 1).
class PaletteMenuItem final : public juce::PopupMenu::CustomComponent
{
public:
    static constexpr int kCell = 24, kGap = 4, kColumns = 8, kRows = 2;

    PaletteMenuItem (int currentIndex, std::function<void (int)> onPick)
        : juce::PopupMenu::CustomComponent (false), current (currentIndex), pick (std::move (onPick))
    {
    }

    void getIdealSize (int& width, int& height) override
    {
        width = kColumns * (kCell + kGap) + kGap + 8;
        height = kRows * (kCell + kGap) + kGap + 8;
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        for (int i = 0; i < Theme::kPaletteSize; ++i)
        {
            const auto r = cellBounds (i).toFloat();
            g.setColour (t.palette (i));
            g.fillRoundedRectangle (r, juce::jmin (t.radius, 5.0f));
            if (i == current || i == hovered)
            {
                g.setColour (i == hovered ? t.text : t.text.withAlpha (0.65f));
                g.drawRoundedRectangle (r.expanded (1.5f), juce::jmin (t.radius, 5.0f) + 1.5f, 1.5f);
            }
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { setHovered (indexAt (e.getPosition())); }
    void mouseExit (const juce::MouseEvent&) override { setHovered (-1); }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const int i = indexAt (e.getPosition());
        if (i >= 0)
        {
            if (pick)
                pick (i);
            triggerMenuItem();
        }
    }

private:
    juce::Rectangle<int> cellBounds (int i) const
    {
        const int col = i % kColumns, row = i / kColumns;
        return { 4 + kGap + col * (kCell + kGap), 4 + kGap + row * (kCell + kGap), kCell, kCell };
    }

    int indexAt (juce::Point<int> p) const
    {
        for (int i = 0; i < Theme::kPaletteSize; ++i)
            if (cellBounds (i).expanded (kGap / 2).contains (p))
                return i;
        return -1;
    }

    void setHovered (int i)
    {
        if (i != hovered)
        {
            hovered = i;
            repaint();
        }
    }

    int current, hovered = -1;
    std::function<void (int)> pick;
};

// A submenu with the palette grid. `current` is highlighted (-1 = none).
inline juce::PopupMenu makePaletteMenu (int current, std::function<void (int)> onPick)
{
    juce::PopupMenu menu;
    menu.addCustomItem (9999, std::make_unique<PaletteMenuItem> (current, std::move (onPick)), nullptr);
    return menu;
}
} // namespace ui
