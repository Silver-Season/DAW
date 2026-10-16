#include "BrowserCatalog.h"

#include <algorithm>

namespace browser
{
juce::String foldForSearch (const juce::String& s)
{
    juce::String out;
    for (auto it = s.getCharPointer(); ! it.isEmpty(); ++it)
    {
        juce::juce_wchar c = *it;
        if (c >= 'A' && c <= 'Z')
            c += 32;
        else if (c >= 0x0410 && c <= 0x042F)
            c += 0x20; // А-Я
        else if (c == 0x0401 || c == 0x0451)
            c = 0x0435; // Ё, ё -> е
        else if (c >= 0x0400 && c <= 0x040F)
            c += 0x50; // Ѐ-Џ
        else if (c >= 0xC0 && c <= 0xDE && c != 0xD7)
            c += 0x20; // Latin-1 capitals
        out += juce::String::charToString (c);
    }
    return out;
}

void Catalog::set (Category c, std::vector<Item> list)
{
    lists[(size_t) c] = std::move (list);
}

int Catalog::total() const
{
    int n = 0;
    for (const auto& l : lists)
        n += (int) l.size();
    return n;
}

const Item* Catalog::find (const juce::String& key) const
{
    for (const auto& l : lists)
        for (const auto& item : l)
            if (item.key == key)
                return &item;
    return nullptr;
}

bool Catalog::matches (const Item& item, const std::vector<juce::String>& words)
{
    if (words.empty())
        return true;
    const auto name = foldForSearch (item.name), group = foldForSearch (item.group);
    for (const auto& w : words)
        if (! name.contains (w) && ! group.contains (w))
            return false;
    return true;
}

std::vector<Item> Catalog::search (const juce::String& text) const
{
    std::vector<juce::String> words;
    juce::StringArray tokens;
    tokens.addTokens (foldForSearch (text.trim()), " ", "");
    tokens.removeEmptyStrings();
    for (const auto& t : tokens)
        words.push_back (t);

    std::vector<Item> out;
    for (const auto& list : lists)
    {
        const auto begin = out.size();
        for (const auto& item : list)
            if (matches (item, words))
                out.push_back (item);
        if (words.empty())
            continue;
        // Within one run of items of the same group, names that start with the text go first (the order of
        // the sources is kept otherwise).
        auto runStart = out.begin() + (std::ptrdiff_t) begin;
        while (runStart != out.end())
        {
            auto runEnd = std::find_if (runStart, out.end(), [&] (const Item& i) { return i.group != runStart->group; });
            std::stable_partition (runStart, runEnd, [&] (const Item& i) { return foldForSearch (i.name).startsWith (words[0]); });
            runStart = runEnd;
        }
    }
    return out;
}

juce::String factoryPresetFolder (const juce::String& name)
{
    for (const char* folder : { "Bass", "Lead", "Pad", "Pluck", "Init" })
        if (name.containsIgnoreCase (folder))
            return folder;
    return "FX";
}
} // namespace browser
