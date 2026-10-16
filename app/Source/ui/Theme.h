#pragma once

#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>

#include <array>
#include <vector>

// Colour, font and shape tokens of one skin (docs/spec/01-layout-project.md, task 1.8). Components take every
// colour and font from the current Theme; the model stores clip colours as palette indices.
struct Theme
{
    static constexpr int kPaletteSize = 16;

    juce::String id, name;
    juce::Colour background, chrome, panel, panel2, line, text, dimText, accent, accent2, record, play, clipInk;
    float clipSaturation = 0.78f; // 0..1
    float clipLightness = 0.62f;  // 0..1
    std::array<float, kPaletteSize> hues {}; // degrees
    float glow = 0.0f;   // strength of the glow around playing clips, 0 = none
    float radius = 6.0f; // corner radius of cells, buttons and panels, px
    bool scanlines = false;
    juce::String fontUi { "Onest" }, fontNum { "JetBrains Mono" };

    // Palette colour i (0..15); inactive clips use the same colour at 55 % opacity.
    juce::Colour palette (int index) const;
    // Palette colour i for text on panel/chrome: the same hue, with the lightness moved until the contrast
    // against both backgrounds is at least 4.5:1 (about 38-42 % lightness on light themes).
    juce::Colour textPalette (int index) const;
    static float contrast (juce::Colour a, juce::Colour b);
    juce::Colour clipColour (int index, bool active) const { return palette (index).withAlpha (active ? 1.0f : 0.55f); }

    // weight: 400 regular, 500 medium, 600 semibold (bold for the monospace family)
    juce::Font uiFont (float height, int weight = 400) const;
    // Digits of tempo, position and dB readouts. The face is monospaced, so digits are tabular.
    juce::Font numFont (float height, int weight = 400) const;

    // Strict: every token must be present, otherwise `error` names the first one missing.
    static bool fromJson (const juce::String& json, Theme& result, juce::String& error);
};

// Owns the list of skins (built in, then those in User Library/Themes) and the current one.
// Changing the theme broadcasts a change message; the root component repaints the whole tree.
class ThemeManager : public juce::ChangeBroadcaster
{
public:
    static ThemeManager& get();

    const Theme& current() const { return themes[(size_t) currentIndex]; }
    const std::vector<Theme>& all() const { return themes; }
    // Bumped on every theme change; caches (glow images, scanlines) compare it.
    int generation() const { return generationCounter; }

    // Falls back to the first theme for an unknown id.
    void setCurrent (const juce::String& id);

    // Re-reads the built-in themes (from the embedded JSON files) and the user ones from `userDir`.
    // Returns the files that could not be parsed, with the reason.
    juce::StringArray reload (const juce::File& userDir = {});

private:
    ThemeManager();

    std::vector<Theme> themes;
    int currentIndex = 0;
    int generationCounter = 0;
};

inline const Theme& theme() { return ThemeManager::get().current(); }
