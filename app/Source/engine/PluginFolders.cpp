#include "PluginFolders.h"

namespace pluginfolders
{
namespace
{
juce::StringArray foldersOf (const juce::FileSearchPath& path)
{
    juce::StringArray result;
    for (int i = 0; i < path.getNumPaths(); ++i)
        result.add (path.getRawString (i));
    return result;
}
} // namespace

juce::AudioPluginFormat* findFormat (juce::AudioPluginFormatManager& formats, const juce::String& name)
{
    for (auto* f : formats.getFormats())
        if (f->getName() == name)
            return f;
    return nullptr;
}

juce::StringArray defaultVst3Folders (juce::AudioPluginFormatManager& formats)
{
    if (auto* vst3 = findFormat (formats, "VST3"))
        return foldersOf (vst3->getDefaultLocationsToSearch());
    return {};
}

juce::StringArray vst3Folders (juce::PropertiesFile& props, juce::AudioPluginFormatManager& formats)
{
    if (auto* vst3 = findFormat (formats, "VST3"))
        return foldersOf (juce::PluginListComponent::getLastSearchPath (props, *vst3));
    return {};
}

void setVst3Folders (juce::PropertiesFile& props, juce::AudioPluginFormatManager& formats, const juce::StringArray& folders)
{
    auto* vst3 = findFormat (formats, "VST3");
    if (vst3 == nullptr)
        return;
    juce::FileSearchPath path;
    for (const auto& f : folders)
        if (f.trim().isNotEmpty())
            path.add (juce::File (f));
    path.removeRedundantPaths();
    juce::PluginListComponent::setLastSearchPath (props, *vst3, path);
    props.saveIfNeeded();
}

juce::StringArray filesToScan (juce::PropertiesFile& props, juce::AudioPluginFormatManager& formats, juce::AudioPluginFormat& format)
{
    if (format.getName() == "VST3")
    {
        juce::FileSearchPath path;
        for (const auto& f : vst3Folders (props, formats))
            path.add (juce::File (f));
        return format.searchPathsForPlugins (path, true, false);
    }
    return format.searchPathsForPlugins (format.getDefaultLocationsToSearch(), true, false);
}
} // namespace pluginfolders
