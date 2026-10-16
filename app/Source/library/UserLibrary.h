#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <vector>

// The user's own presets and device chains on disk (docs/spec/01-layout-project.md, task 1.6):
//
//   <User Library>/Presets/<device name>/<name>.dawpreset
//   <User Library>/Chains/<name>.dawchain
//   <User Library>/Templates/Default.prisma
//   <User Library>/Themes/*.json
namespace library
{
struct PresetData
{
    bool valid = false;
    juce::String deviceKind; // "prisma" | "plugin"
    juce::String name;
    juce::String pluginDesc; // XML of the juce::PluginDescription, empty for Prisma
    juce::String state;      // base64 of getStateInformation
};

struct PresetInfo
{
    juce::File file;
    juce::String deviceName; // the folder: "Prisma" or the plugin's name
    PresetData data;
};

struct ChainData
{
    bool valid = false;
    juce::String name;
    int colour = 0;
    juce::Array<juce::ValueTree> devices; // DEVICE nodes without ids
};

class UserLibrary
{
public:
    explicit UserLibrary (const juce::File& root) : rootFolder (root) {}

    // ~/Music/Prisma DAW/User Library
    static juce::File defaultRoot();

    const juce::File& root() const { return rootFolder; }
    void setRoot (const juce::File& r) { rootFolder = r; }
    juce::File presetsFolder() const { return rootFolder.getChildFile ("Presets"); }
    juce::File presetsFolderFor (const juce::String& deviceName) const;
    juce::File chainsFolder() const { return rootFolder.getChildFile ("Chains"); }
    juce::File templatesFolder() const { return rootFolder.getChildFile ("Templates"); }
    juce::File themesFolder() const { return rootFolder.getChildFile ("Themes"); }
    juce::File defaultTemplate() const { return templatesFolder().getChildFile ("Default.prisma"); }

    // Presets. A name that is taken becomes "name 2". Returns the file written, or an invalid File with `error`.
    juce::File savePreset (const juce::String& deviceName, const juce::String& name, const juce::String& deviceKind,
                           const juce::String& pluginDesc, const juce::String& stateBase64, juce::String& error) const;
    std::vector<PresetInfo> listPresets() const;
    static PresetData readPreset (const juce::File&);
    // Renames the preset (file and name inside). Returns the new file, or an invalid File.
    static juce::File renamePreset (const juce::File&, const juce::String& newName);
    static bool deleteFile (const juce::File& f) { return f.deleteFile(); }

    // Chains of one track: its DEVICE nodes and its colour index.
    juce::File saveChain (const juce::String& name, int colour, const juce::Array<juce::ValueTree>& deviceNodes, juce::String& error) const;
    juce::Array<juce::File> listChains() const;
    static ChainData readChain (const juce::File&);
    static juce::File renameChain (const juce::File&, const juce::String& newName);

private:
    juce::File rootFolder;
};
} // namespace library
