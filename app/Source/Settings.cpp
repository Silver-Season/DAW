#include "Settings.h"

namespace
{
juce::PropertiesFile::Options defaultOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "DAW";
    options.filenameSuffix = ".settings";
    options.folderName = "DAW";
    options.osxLibrarySubFolder = "Application Support";
    return options;
}
} // namespace

Settings::Settings() : props (std::make_unique<juce::PropertiesFile> (defaultOptions())) {}

Settings::Settings (const juce::File& file)
    : props (std::make_unique<juce::PropertiesFile> (file, defaultOptions()))
{
}

Settings::~Settings()
{
    props->saveIfNeeded();
}

void Settings::save()
{
    props->saveIfNeeded();
}

//==============================================================================
juce::String Settings::theme() const { return props->getValue ("theme", "prism"); }
void Settings::setTheme (const juce::String& id) { props->setValue ("theme", id); }

double Settings::uiScale() const { return juce::jlimit (0.8, 1.5, props->getDoubleValue ("uiScale", 1.0)); }
void Settings::setUiScale (double scale) { props->setValue ("uiScale", juce::jlimit (0.8, 1.5, scale)); }

bool Settings::animations() const { return props->getBoolValue ("animations", true); }
void Settings::setAnimations (bool on) { props->setValue ("animations", on); }

//==============================================================================
juce::StringArray Settings::recentProjects() const
{
    juce::StringArray list;
    list.addLines (props->getValue ("recentProjects"));
    list.removeEmptyStrings();
    return list;
}

void Settings::addRecentProject (const juce::File& f)
{
    auto list = recentProjects();
    list.removeString (f.getFullPathName());
    list.insert (0, f.getFullPathName());
    while (list.size() > kMaxRecentProjects)
        list.remove (list.size() - 1);
    props->setValue ("recentProjects", list.joinIntoString ("\n"));
}

void Settings::removeRecentProject (const juce::File& f)
{
    auto list = recentProjects();
    list.removeString (f.getFullPathName());
    props->setValue ("recentProjects", list.joinIntoString ("\n"));
}

void Settings::clearRecentProjects()
{
    props->removeValue ("recentProjects");
}

juce::StringArray Settings::placeFolders() const
{
    juce::StringArray list;
    list.addLines (props->getValue ("placeFolders"));
    list.removeEmptyStrings();
    return list;
}

void Settings::setPlaceFolders (const juce::StringArray& folders)
{
    props->setValue ("placeFolders", folders.joinIntoString ("\n"));
}

double Settings::previewVolumeDb() const { return props->getDoubleValue ("previewVolumeDb", -12.0); }
void Settings::setPreviewVolumeDb (double db) { props->setValue ("previewVolumeDb", juce::jlimit (-60.0, 0.0, db)); }

//==============================================================================
// "audioDevice" is the key the phase-0 build already used, so existing user settings keep working.
std::unique_ptr<juce::XmlElement> Settings::audioDeviceState() const { return props->getXmlValue ("audioDevice"); }
void Settings::setAudioDeviceState (const juce::XmlElement& xml) { props->setValue ("audioDevice", &xml); }

juce::String Settings::channelKey (const juce::String& device, bool isInput, int channel)
{
    return "channelName/" + device + (isInput ? "/in/" : "/out/") + juce::String (channel);
}

juce::String Settings::channelName (const juce::String& device, bool isInput, int channel) const
{
    return props->getValue (channelKey (device, isInput, channel));
}

void Settings::setChannelName (const juce::String& device, bool isInput, int channel, const juce::String& name)
{
    if (name.isEmpty())
        props->removeValue (channelKey (device, isInput, channel));
    else
        props->setValue (channelKey (device, isInput, channel), name);
}

std::unique_ptr<juce::XmlElement> Settings::knownPlugins() const { return props->getXmlValue ("knownPlugins"); }
void Settings::setKnownPlugins (const juce::XmlElement& xml) { props->setValue ("knownPlugins", &xml); }
