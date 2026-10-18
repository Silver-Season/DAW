#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// The folders searched for VST3 plug-ins. They are kept under the key the plugin list component uses itself
// ("lastPluginScanPath_VST3"), so the settings page, the scanner window and a scan started from either agree.
// Audio Units have no folders: macOS registers them, and a scan asks the system for the list.
namespace pluginfolders
{
juce::AudioPluginFormat* findFormat (juce::AudioPluginFormatManager&, const juce::String& name);

// The folders a VST3 scan looks in: the user's list, or the format's defaults when none was set.
juce::StringArray vst3Folders (juce::PropertiesFile&, juce::AudioPluginFormatManager&);
juce::StringArray defaultVst3Folders (juce::AudioPluginFormatManager&);
// An empty list goes back to the defaults.
void setVst3Folders (juce::PropertiesFile&, juce::AudioPluginFormatManager&, const juce::StringArray&);

// What a scan of `format` looks at without asking: the plug-in files under its folders (VST3) or every
// plug-in the system knows (AU).
juce::StringArray filesToScan (juce::PropertiesFile&, juce::AudioPluginFormatManager&, juce::AudioPluginFormat&);
} // namespace pluginfolders
