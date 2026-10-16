#include <cmath>
#include <algorithm>
#include "Theme.h"

#include <BinaryData.h>
#include <juce_core/juce_core.h>

namespace
{
struct Typefaces
{
    Typefaces()
    {
        auto make = [] (const char* data, int size) { return juce::Typeface::createSystemTypefaceFor (data, (size_t) size); };
        onest[0] = make (BinaryData::OnestRegular_ttf, BinaryData::OnestRegular_ttfSize);
        onest[1] = make (BinaryData::OnestMedium_ttf, BinaryData::OnestMedium_ttfSize);
        onest[2] = make (BinaryData::OnestSemiBold_ttf, BinaryData::OnestSemiBold_ttfSize);
        mono[0] = make (BinaryData::JetBrainsMonoRegular_ttf, BinaryData::JetBrainsMonoRegular_ttfSize);
        mono[1] = make (BinaryData::JetBrainsMonoBold_ttf, BinaryData::JetBrainsMonoBold_ttfSize);
    }

    static Typefaces& get()
    {
        static Typefaces instance;
        return instance;
    }

    juce::Typeface::Ptr onest[3], mono[2];
};

juce::Font makeFont (const juce::String& family, float height, int weight)
{
    auto& faces = Typefaces::get();
    juce::Typeface::Ptr face;
    if (family == "JetBrains Mono")
        face = faces.mono[weight >= 550 ? 1 : 0];
    else
        face = faces.onest[weight >= 550 ? 2 : (weight >= 450 ? 1 : 0)];

    if (face == nullptr) // embedded font failed to load: the platform default keeps the UI usable
        return juce::Font (juce::FontOptions (height, weight >= 550 ? juce::Font::bold : juce::Font::plain));
    return juce::Font (juce::FontOptions (face).withHeight (height));
}

const char* const kColourTokens[] = { "background", "chrome", "panel", "panel2", "line", "text", "dimText",
                                      "accent", "accent2", "record", "play", "clipInk" };
} // namespace

//==============================================================================
juce::Colour Theme::palette (int index) const
{
    const float hue = hues[(size_t) juce::jlimit (0, kPaletteSize - 1, index)];
    return juce::Colour::fromHSL (hue / 360.0f, clipSaturation, clipLightness, 1.0f);
}

float Theme::contrast (juce::Colour a, juce::Colour b)
{
    const auto lum = [] (juce::Colour c) {
        const auto lin = [] (float v) { return v <= 0.03928f ? v / 12.92f : std::pow ((v + 0.055f) / 1.055f, 2.4f); };
        return 0.2126f * lin (c.getFloatRed()) + 0.7152f * lin (c.getFloatGreen()) + 0.0722f * lin (c.getFloatBlue());
    };
    const float la = lum (a), lb = lum (b);
    return (std::max (la, lb) + 0.05f) / (std::min (la, lb) + 0.05f);
}

juce::Colour Theme::textPalette (int index) const
{
    const float hue = hues[(size_t) juce::jlimit (0, kPaletteSize - 1, index)] / 360.0f;
    const bool lightTheme = panel.getPerceivedBrightness() > 0.5f;
    float lightness = lightTheme ? 0.40f : clipLightness;
    const float step = lightTheme ? -0.01f : 0.01f;
    auto colour = juce::Colour::fromHSL (hue, clipSaturation, lightness, 1.0f);
    while (lightness > 0.05f && lightness < 0.95f && (contrast (colour, panel) < 4.5f || contrast (colour, chrome) < 4.5f))
    {
        lightness += step;
        colour = juce::Colour::fromHSL (hue, clipSaturation, lightness, 1.0f);
    }
    return colour;
}

juce::Font Theme::uiFont (float height, int weight) const { return makeFont (fontUi, height, weight); }
juce::Font Theme::numFont (float height, int weight) const { return makeFont (fontNum, height, weight); }

bool Theme::fromJson (const juce::String& json, Theme& result, juce::String& error)
{
    const auto parsed = juce::JSON::parse (json);
    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr)
    {
        error = "not a JSON object";
        return false;
    }

    Theme t;
    auto need = [&] (const char* key) -> bool {
        if (obj->hasProperty (key))
            return true;
        error = juce::String ("missing token: ") + key;
        return false;
    };

    if (! need ("id") || ! need ("name"))
        return false;
    t.id = obj->getProperty ("id").toString();
    t.name = obj->getProperty ("name").toString();
    if (t.id.isEmpty())
    {
        error = "empty id";
        return false;
    }

    juce::Colour* colours[] = { &t.background, &t.chrome, &t.panel, &t.panel2, &t.line, &t.text, &t.dimText,
                                &t.accent, &t.accent2, &t.record, &t.play, &t.clipInk };
    for (size_t i = 0; i < std::size (kColourTokens); ++i)
    {
        if (! need (kColourTokens[i]))
            return false;
        const auto text = obj->getProperty (kColourTokens[i]).toString().trimCharactersAtStart ("#");
        if (text.length() != 6 || ! text.containsOnly ("0123456789abcdefABCDEF"))
        {
            error = juce::String ("bad colour in token ") + kColourTokens[i] + ": " + text;
            return false;
        }
        *colours[i] = juce::Colour (0xff000000u | (juce::uint32) text.getHexValue32());
    }

    for (const char* key : { "clipSaturation", "clipLightness", "glow", "radius", "scanlines", "fontUi", "fontNum", "hues" })
        if (! need (key))
            return false;

    t.clipSaturation = juce::jlimit (0.0f, 1.0f, (float) (double) obj->getProperty ("clipSaturation") / 100.0f);
    t.clipLightness = juce::jlimit (0.0f, 1.0f, (float) (double) obj->getProperty ("clipLightness") / 100.0f);
    t.glow = juce::jmax (0.0f, (float) (double) obj->getProperty ("glow"));
    t.radius = juce::jmax (0.0f, (float) (double) obj->getProperty ("radius"));
    t.scanlines = (bool) obj->getProperty ("scanlines");
    t.fontUi = obj->getProperty ("fontUi").toString();
    t.fontNum = obj->getProperty ("fontNum").toString();

    const auto* hues = obj->getProperty ("hues").getArray();
    if (hues == nullptr || hues->size() != kPaletteSize)
    {
        error = "hues must be an array of 16 numbers";
        return false;
    }
    for (int i = 0; i < kPaletteSize; ++i)
        t.hues[(size_t) i] = (float) (double) (*hues)[i];

    result = std::move (t);
    return true;
}

//==============================================================================
ThemeManager::ThemeManager()
{
    reload();
}

ThemeManager& ThemeManager::get()
{
    static ThemeManager instance;
    return instance;
}

juce::StringArray ThemeManager::reload (const juce::File& userDir)
{
    juce::StringArray problems;
    const auto previous = themes.empty() ? juce::String ("prism") : current().id;
    std::vector<Theme> loaded;

    auto add = [&] (const juce::String& source, const juce::String& json) {
        Theme t;
        juce::String error;
        if (! Theme::fromJson (json, t, error))
        {
            problems.add (source + ": " + error);
            return;
        }
        for (auto& existing : loaded) // a user theme with the id of an existing one replaces it
            if (existing.id == t.id)
            {
                existing = t;
                return;
            }
        loaded.push_back (std::move (t));
    };

    // Built-in themes in a fixed order, then anything else that is embedded.
    const char* order[] = { "prism_json", "neon_json", "studio_json", "paper_json" };
    for (const char* name : order)
    {
        int size = 0;
        if (const char* data = BinaryData::getNamedResource (name, size))
            add (name, juce::String::fromUTF8 (data, size));
    }

    if (userDir.isDirectory())
        for (const auto& f : userDir.findChildFiles (juce::File::findFiles, false, "*.json"))
            add (f.getFileName(), f.loadFileAsString());

    jassert (! loaded.empty());
    themes = std::move (loaded);
    ++generationCounter;
    setCurrent (previous);
    return problems;
}

void ThemeManager::setCurrent (const juce::String& id)
{
    int index = 0;
    for (size_t i = 0; i < themes.size(); ++i)
        if (themes[i].id == id)
            index = (int) i;
    currentIndex = index;
    ++generationCounter;
    sendChangeMessage();
}
