#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <vector>

// What the browser lists, and how it is searched (docs/spec/01-layout-project.md, task 1.5). No component in
// here: the panel shows a Catalog, the sources fill it.
namespace browser
{
enum class Category
{
    instruments,
    presets,
    effects,
    plugins,
    chains,
    sounds,
    midiFiles,
    places,
    count
};
constexpr int kNumCategories = (int) Category::count;

// The "kind" strings of dragged items (see BrowserDrag).
namespace kinds
{
inline const juce::String prisma = "prisma";             // the Prisma instrument itself
inline const juce::String prismaPreset = "prismaPreset"; // a factory preset, id = index of the program
inline const juce::String userPreset = "userPreset";     // a .dawpreset file, path = file
inline const juce::String plugin = "plugin";             // a hosted AU / VST3, id = PluginDescription identifier string
inline const juce::String chain = "chain";               // a .dawchain file
inline const juce::String midiFile = "midi";             // a .mid file
inline const juce::String audioFile = "audio";           // a sound file
} // namespace kinds

struct Item
{
    Category category = Category::instruments;
    juce::String name;     // shown
    juce::String group;    // folder / manufacturer, shown as a heading ("" = none)
    juce::String key;      // stable identity for favourites and recents: "prismaPreset:Reese Bass", "file:/path"
    juce::String kind;     // one of kinds::
    juce::String id;       // see kinds::
    juce::String path;     // file for files and user items
    juce::String detail;   // small right-aligned text: format, "factory"
    bool instrument = false; // plugins and presets: makes sound from MIDI (matters for drops)
    bool userEditable = false; // user presets and chains can be renamed and deleted
    bool placeholder = false;  // a row that only explains ("Appears in stage 6"): cannot be dragged
};

// Case-insensitive for Latin and Cyrillic (the C library's tolower does not do Cyrillic in the "C" locale),
// with yo folded into ye.
juce::String foldForSearch (const juce::String&);

class Catalog
{
public:
    void set (Category, std::vector<Item>);
    const std::vector<Item>& items (Category c) const { return lists[(size_t) c]; }
    int total() const;
    const Item* find (const juce::String& key) const;

    // Everything whose name or group contains the text (all words must match somewhere), grouped by category
    // in the order of Category, then in the order of the sources; names that start with the text come first
    // within a group. An empty text returns everything.
    std::vector<Item> search (const juce::String& text) const;
    static bool matches (const Item&, const std::vector<juce::String>& foldedWords);

private:
    std::array<std::vector<Item>, (size_t) kNumCategories> lists;
};

// The folder a factory preset is shown in (Bass, Lead, Pad, Pluck, FX, Init), taken from its name.
juce::String factoryPresetFolder (const juce::String& presetName);
} // namespace browser
