#pragma once

#include "BrowserCatalog.h"

#include "../Settings.h"
#include "../library/UserLibrary.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>

namespace browser
{
// Fills a Catalog from the real sources: Prisma and its presets, the plugins the scanner found, the user
// library on disk, and the "Places" folders (scanned on a background thread).
class BrowserSources
{
public:
    BrowserSources (Settings&, juce::KnownPluginList&, library::UserLibrary&);
    ~BrowserSources();

    Catalog& catalog() { return data; }
    const Catalog& catalog() const { return data; }

    // Called on the message thread whenever any list changed.
    std::function<void()> onChanged;

    void refreshBuiltIn();   // Prisma and the factory presets
    void refreshPlugins();   // from the KnownPluginList
    void refreshLibrary();   // user presets and chains: a few small files, read at once
    void refreshAll();

    // Places = the folders the user added plus the folder of the open project. Scanning runs in the
    // background, one job per folder; results arrive on the message thread.
    void setProjectFolder (const juce::File&);
    juce::Array<juce::File> places() const;
    void scanPlaces (bool onlyIfStale);
    // The folder is linked, not copied: only its path goes into the settings. Both "Add folder..." and a folder
    // dropped on the panel come here. False if it is not a folder or is there already. Does not rescan.
    bool addPlaceFolder (const juce::File&);

    // The scan of one folder, synchronous (used by the job and by tests): sound files, MIDI files, and the
    // same files grouped under the place for the "Places" category.
    struct ScanResult
    {
        std::vector<Item> sounds, midi, places;
    };
    static ScanResult scanFolder (const juce::File& root, const juce::String& placeName);

private:
    void changed();

    Settings& settings;
    juce::KnownPluginList& plugins;
    library::UserLibrary& library;
    Catalog data;
    juce::File projectFolder;
    juce::ThreadPool pool { 1 };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    juce::int64 lastScanMs = 0;
    int scanGeneration = 0;
    std::map<int, ScanResult> pending; // by place index, for the scan in progress
    int pendingCount = 0;
};
} // namespace browser
