#pragma once

#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>

#include <array>
#include <vector>

// Application preferences, deliberately separate from the project's musical colours.
struct Appearance
{
    bool customAccent = false, gradient = true, reduceTransparency = false;
    juce::Colour primary{ 0xff16bddd }, secondary{ 0xff9855ef };
    float angle = 25.0f, glow = 0.35f, glass = 0.7f;
    juce::String toJson () const;
    static bool fromJson (const juce::String&, Appearance&);
};

// Colour, font and shape tokens of one skin (docs/spec/01-layout-project.md, task 1.8). Components take every
// colour and font from the current Theme; the model stores clip colours as palette indices.
struct Theme
{
    static constexpr int kPaletteSize = 16;

    juce::String id, name;
    juce::Colour background, chrome, panel, panel2, line, text, dimText, accent, accent2, record, play, clipInk;
    // Orange of "Back to arrangement" and other attention marks. Optional in a theme file: a user theme without it gets
    // an orange taken from its own clip saturation and lightness.
    juce::Colour warning;
    float clipSaturation = 0.78f; // 0..1
    float clipLightness = 0.62f;  // 0..1
    std::array<float, kPaletteSize> hues {}; // degrees
    float glow = 0.0f;   // strength of the glow around playing clips, 0 = none
    float radius = 6.0f; // corner radius of cells, buttons and panels, px
    bool scanlines = false;
    bool glass = false, gradients = false;
    float material = 0.0f, gradientAngle = 25.0f;
    juce::Colour accentFill, accentFill2;
    // Sizes of the interface (docs/design/UI-KIT.md). Optional in a theme file ("metrics": { ... }); the defaults are
    // the sizes the classic skins always had, with the corner radius taken from "radius".
    struct Metrics
    {
        float gap = 6.0f;            // between controls of a group
        float padding = 10.0f;       // inside a panel or card
        float controlHeight = 26.0f; // buttons, fields, combo boxes
        float compactHeight = 22.0f; // the same in dense places (mixer strips, headers)
        float controlRadius = 6.0f;  // buttons and fields
        float panelRadius = 6.0f;    // cards and panels
        bool pill = false;           // buttons with fully rounded ends
        float text = 12.5f, small = 11.0f, title = 14.0f, number = 13.0f;
    } metrics;
    bool isLight () const
    {
        return background.getPerceivedBrightness () > 0.5f;
    }
    juce::Colour highlight () const
    {
        return juce::Colours::white;
    }
    juce::Colour shadow () const
    {
        return juce::Colours::black;
    }
    juce::Colour inkOn (juce::Colour fill) const;
    juce::Colour readable (juce::Colour colour) const;
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

    const Theme& current () const
    {
        return effective;
    }
    const std::vector<Theme>& all() const { return themes; }
    // Bumped on every theme change; caches (glow images, scanlines) compare it.
    int generation() const { return generationCounter; }

    // Falls back to the first theme for an unknown id.
    void setCurrent (const juce::String& id);
    void setAppearance (const Appearance&);
    const Appearance& appearance () const
    {
        return preferences;
    }

    // Re-reads the built-in themes (from the embedded JSON files) and the user ones from `userDir`.
    // Returns the files that could not be parsed, with the reason.
    juce::StringArray reload (const juce::File& userDir = {});

private:
    ThemeManager();

    std::vector<Theme> themes;
    int currentIndex = 0;
    int generationCounter = 0;
    Appearance preferences;
    Theme effective;
    void updateEffective ();
};

inline const Theme& theme() { return ThemeManager::get().current(); }
