#include "BrowserSources.h"

#include "../Strings.h"

#include "daw/synth/PrismaParams.h"

namespace browser
{
namespace
{
bool isSoundFile (const juce::File& f)
{
    return f.hasFileExtension ("wav;aif;aiff;flac;mp3;ogg");
}
bool isMidiFile (const juce::File& f)
{
    return f.hasFileExtension ("mid;midi");
}

constexpr int kMaxDepth = 6;
constexpr int kMaxFilesPerPlace = 20000;

void scanRecursive (const juce::File& dir, const juce::File& root, const juce::String& placeName, int depth, BrowserSources::ScanResult& out, int& count)
{
    if (depth > kMaxDepth || count >= kMaxFilesPerPlace)
        return;
    auto children = dir.findChildFiles (juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles, false);
    children.sort();
    for (const auto& child : children)
    {
        if (count >= kMaxFilesPerPlace)
            return;
        if (child.isDirectory())
        {
            if (child.getFileName() != "Backup" && child.getFileName() != "History") // a project's own bookkeeping
                scanRecursive (child, root, placeName, depth + 1, out, count);
            continue;
        }
        const bool sound = isSoundFile (child), midi = isMidiFile (child);
        if (! sound && ! midi)
            continue;
        ++count;
        const auto rel = child.getParentDirectory().getRelativePathFrom (root);
        Item item;
        item.name = child.getFileNameWithoutExtension();
        item.group = rel == "." || rel.isEmpty() ? placeName : placeName + " / " + rel.replaceCharacter ('\\', '/');
        item.key = "file:" + child.getFullPathName();
        item.path = child.getFullPathName();
        item.kind = midi ? kinds::midiFile : kinds::audioFile;
        item.detail = child.getFileExtension().trimCharactersAtStart (".").toUpperCase();
        item.category = midi ? Category::midiFiles : Category::sounds;
        (midi ? out.midi : out.sounds).push_back (item);
        item.category = Category::places;
        out.places.push_back (item);
    }
}

juce::String pluginDetail (const juce::PluginDescription& d)
{
    return d.pluginFormatName;
}
} // namespace

BrowserSources::ScanResult BrowserSources::scanFolder (const juce::File& root, const juce::String& placeName)
{
    ScanResult result;
    int count = 0;
    scanRecursive (root, root, placeName, 0, result, count);
    return result;
}

BrowserSources::BrowserSources (Settings& s, juce::KnownPluginList& p, library::UserLibrary& l) : settings (s), plugins (p), library (l)
{
    refreshAll();
}

BrowserSources::~BrowserSources()
{
    alive->store (false);
    pool.removeAllJobs (true, 2000);
}

void BrowserSources::changed()
{
    if (onChanged)
        onChanged();
}

void BrowserSources::refreshAll()
{
    refreshBuiltIn();
    refreshPlugins();
    refreshLibrary();
}

void BrowserSources::refreshBuiltIn()
{
    std::vector<Item> instruments, presets;

    Item prisma;
    prisma.category = Category::instruments;
    prisma.name = "Prisma";
    prisma.group = "Prisma";
    prisma.key = "prisma";
    prisma.kind = kinds::prisma;
    prisma.instrument = true;
    prisma.detail = Strings::browser::builtIn;
    instruments.push_back (prisma);
    data.set (Category::instruments, instruments); // plugin instruments are appended by refreshPlugins()

    const auto& factory = daw::synth::factoryPresets();
    for (size_t i = 0; i < factory.size(); ++i)
    {
        Item p;
        p.category = Category::presets;
        p.name = factory[i].name;
        p.group = factoryPresetFolder (p.name);
        p.key = "prismaPreset:" + p.name;
        p.kind = kinds::prismaPreset;
        p.id = juce::String ((int) i);
        p.instrument = true;
        p.detail = "Prisma";
        presets.push_back (p);
    }
    std::stable_sort (presets.begin(), presets.end(), [] (const Item& a, const Item& b) { return a.group.compareIgnoreCase (b.group) < 0; });
    data.set (Category::presets, presets);
    refreshPlugins();
    refreshLibrary();
}

void BrowserSources::refreshPlugins()
{
    std::vector<Item> instruments, effects, all;

    // Prisma stays first in the instruments
    for (const auto& item : data.items (Category::instruments))
        if (item.kind == kinds::prisma)
            instruments.push_back (item);

    auto types = plugins.getTypes();
    std::sort (types.begin(), types.end(), [] (const juce::PluginDescription& a, const juce::PluginDescription& b) {
        const int m = a.manufacturerName.compareIgnoreCase (b.manufacturerName);
        return m != 0 ? m < 0 : a.name.compareIgnoreCase (b.name) < 0;
    });
    for (const auto& d : types)
    {
        Item item;
        item.name = d.name;
        item.group = d.manufacturerName.isEmpty() ? Strings::browser::otherVendor : d.manufacturerName;
        item.key = "plugin:" + d.createIdentifierString();
        item.kind = kinds::plugin;
        item.id = d.createIdentifierString();
        item.instrument = d.isInstrument;
        item.detail = pluginDetail (d);
        item.category = Category::plugins;
        all.push_back (item);
        item.category = d.isInstrument ? Category::instruments : Category::effects;
        (d.isInstrument ? instruments : effects).push_back (item);
    }

    Item builtin;
    builtin.category = Category::effects;
    builtin.name = Strings::browser::builtinEffectsSoon;
    builtin.group = Strings::browser::builtinGroup;
    builtin.key = "placeholder:builtin-effects";
    builtin.placeholder = true;
    effects.insert (effects.begin(), builtin);

    data.set (Category::instruments, std::move (instruments));
    data.set (Category::effects, std::move (effects));
    data.set (Category::plugins, std::move (all));
    changed();
}

void BrowserSources::refreshLibrary()
{
    // user presets join the factory ones in "Presets"
    std::vector<Item> presets;
    for (const auto& item : data.items (Category::presets))
        if (item.kind == kinds::prismaPreset)
            presets.push_back (item);
    for (const auto& info : library.listPresets())
    {
        Item item;
        item.category = Category::presets;
        item.name = info.data.name;
        item.group = Strings::browser::userGroup (info.deviceName);
        item.key = "userPreset:" + info.file.getFullPathName();
        item.kind = kinds::userPreset;
        item.path = info.file.getFullPathName();
        item.userEditable = true;
        item.detail = info.deviceName;
        if (info.data.deviceKind == "plugin")
        {
            if (const auto xml = juce::XmlDocument::parse (info.data.pluginDesc))
            {
                juce::PluginDescription desc;
                if (desc.loadFromXml (*xml))
                {
                    item.id = desc.createIdentifierString();
                    item.instrument = desc.isInstrument;
                }
            }
        }
        else
        {
            item.id = "prisma";
            item.instrument = true;
        }
        presets.push_back (item);
    }
    data.set (Category::presets, std::move (presets));

    std::vector<Item> chains;
    for (const auto& file : library.listChains())
    {
        const auto chain = library::UserLibrary::readChain (file);
        if (! chain.valid)
            continue;
        Item item;
        item.category = Category::chains;
        item.name = chain.name;
        item.group = Strings::browser::userChains;
        item.key = "chain:" + file.getFullPathName();
        item.kind = kinds::chain;
        item.path = file.getFullPathName();
        item.userEditable = true;
        item.detail = juce::String (chain.devices.size()) + " " + Strings::browser::devicesShort;
        chains.push_back (item);
    }
    data.set (Category::chains, std::move (chains));
    changed();
}

//==============================================================================
void BrowserSources::setProjectFolder (const juce::File& folder)
{
    if (folder == projectFolder)
        return;
    projectFolder = folder;
    scanPlaces (false);
}

juce::Array<juce::File> BrowserSources::places() const
{
    juce::Array<juce::File> list;
    for (const auto& path : settings.placeFolders())
        list.addIfNotAlreadyThere (juce::File (path));
    if (projectFolder != juce::File() && projectFolder.isDirectory())
        list.addIfNotAlreadyThere (projectFolder);
    return list;
}

bool BrowserSources::addPlaceFolder (const juce::File& folder)
{
    if (! folder.isDirectory())
        return false;
    auto folders = settings.placeFolders();
    if (folders.contains (folder.getFullPathName()))
        return false;
    folders.add (folder.getFullPathName());
    settings.setPlaceFolders (folders);
    return true;
}

void BrowserSources::scanPlaces (bool onlyIfStale)
{
    const auto now = juce::Time::getMillisecondCounter();
    if (onlyIfStale && lastScanMs != 0 && (juce::int64) now - lastScanMs < 10000)
        return;
    lastScanMs = (juce::int64) now;

    const auto folders = places();
    const int generation = ++scanGeneration;
    pending.clear();
    pendingCount = folders.size();
    if (folders.isEmpty())
    {
        data.set (Category::sounds, {});
        data.set (Category::midiFiles, {});
        data.set (Category::places, {});
        changed();
        return;
    }

    for (int i = 0; i < folders.size(); ++i)
    {
        const auto folder = folders[i];
        const auto name = folder == projectFolder ? Strings::browser::projectPlace (folder.getFileName()) : folder.getFileName();
        pool.addJob ([this, flag = alive, generation, i, folder, name] {
            auto result = scanFolder (folder, name);
            juce::MessageManager::callAsync ([this, flag, generation, i, result = std::move (result)]() mutable {
                if (! flag->load() || generation != scanGeneration)
                    return; // a newer scan has started
                pending[i] = std::move (result);
                if ((int) pending.size() < pendingCount)
                    return;
                std::vector<Item> sounds, midi, placeItems;
                for (auto& [index, r] : pending)
                {
                    sounds.insert (sounds.end(), r.sounds.begin(), r.sounds.end());
                    midi.insert (midi.end(), r.midi.begin(), r.midi.end());
                    placeItems.insert (placeItems.end(), r.places.begin(), r.places.end());
                }
                data.set (Category::sounds, std::move (sounds));
                data.set (Category::midiFiles, std::move (midi));
                data.set (Category::places, std::move (placeItems));
                changed();
            });
        });
    }
}
} // namespace browser
