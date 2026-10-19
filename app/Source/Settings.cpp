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

juce::File Settings::defaultFile() { return defaultOptions().getDefaultFile(); }

juce::File Settings::languageFileFor (const juce::File& settingsFile) { return settingsFile.getSiblingFile ("language"); }

juce::String Settings::language() const
{
    return languageFileFor (props->getFile()).loadFileAsString().trim().equalsIgnoreCase ("ru") ? "ru" : "en";
}

void Settings::setLanguage (const juce::String& code)
{
    const auto file = languageFileFor (props->getFile());
    file.getParentDirectory().createDirectory();
    file.replaceWithText (code);
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
juce::String
Settings::theme () const
{
    return props->getValue ("theme", "glass-dark");
}
void Settings::setTheme (const juce::String& id) { props->setValue ("theme", id); }

double Settings::uiScale() const { return juce::jlimit (0.8, 1.5, props->getDoubleValue ("uiScale", 1.0)); }
void Settings::setUiScale (double scale) { props->setValue ("uiScale", juce::jlimit (0.8, 1.5, scale)); }

bool Settings::animations() const { return props->getBoolValue ("animations", true); }
void Settings::setAnimations (bool on) { props->setValue ("animations", on); }
bool Settings::gpuRendering() const { return props->getBoolValue ("gpuRendering", true); }
void Settings::setGpuRendering (bool on) { props->setValue ("gpuRendering", on); }
juce::String
Settings::appearance () const
{
    return props->getValue ("appearance");
}
void
Settings::setAppearance (const juce::String& json)
{
    props->setValue ("appearance", json);
}

//==============================================================================
int Settings::browserWidth() const { return juce::jlimit (180, 420, props->getIntValue ("browserWidth", 236)); }
void Settings::setBrowserWidth (int w) { props->setValue ("browserWidth", juce::jlimit (180, 420, w)); }
int Settings::detailHeight() const { return juce::jlimit (160, 480, props->getIntValue ("detailHeight", 232)); }
int Settings::editorHeight() const { return juce::jlimit (160, 900, props->getIntValue ("editorHeight", 360)); }
void Settings::setEditorHeight (int h) { props->setValue ("editorHeight", juce::jlimit (160, 900, h)); }
void Settings::setDetailHeight (int h) { props->setValue ("detailHeight", juce::jlimit (160, 480, h)); }
bool Settings::browserVisible() const { return props->getBoolValue ("browserVisible", true); }
void Settings::setBrowserVisible (bool v) { props->setValue ("browserVisible", v); }
bool Settings::detailVisible() const { return props->getBoolValue ("detailVisible", true); }
void Settings::setDetailVisible (bool v) { props->setValue ("detailVisible", v); }
bool Settings::mixerVisible() const { return props->getBoolValue ("mixerVisible", true); }
void Settings::setMixerVisible (bool v) { props->setValue ("mixerVisible", v); }
juce::String Settings::windowState() const { return props->getValue ("windowState"); }
void Settings::setWindowState (const juce::String& s) { props->setValue ("windowState", s); }

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

bool Settings::previewEnabled() const { return props->getBoolValue ("previewEnabled", false); }
void Settings::setPreviewEnabled (bool on) { props->setValue ("previewEnabled", on); }
juce::String Settings::libraryPath() const { return props->getValue ("libraryPath"); }
void Settings::setLibraryPath (const juce::String& p)
{
    if (p.isEmpty())
        props->removeValue ("libraryPath");
    else
        props->setValue ("libraryPath", p);
}

std::map<juce::String, int> Settings::favourites() const
{
    std::map<juce::String, int> out;
    juce::StringArray lines;
    lines.addLines (props->getValue ("favourites"));
    for (const auto& line : lines)
    {
        const int tab = line.lastIndexOfChar ('\t');
        if (tab > 0)
            out[line.substring (0, tab)] = line.substring (tab + 1).getIntValue();
    }
    return out;
}

int Settings::favouriteColour (const juce::String& key) const
{
    const auto all = favourites();
    const auto it = all.find (key);
    return it == all.end() ? -1 : it->second;
}

void Settings::setFavouriteColour (const juce::String& key, int colour)
{
    auto all = favourites();
    if (colour < 0)
        all.erase (key);
    else
        all[key] = juce::jlimit (0, 6, colour);
    juce::StringArray lines;
    for (const auto& [k, c] : all)
        lines.add (k + "\t" + juce::String (c));
    props->setValue ("favourites", lines.joinIntoString ("\n"));
}

juce::StringArray Settings::recentItems() const
{
    juce::StringArray list;
    list.addLines (props->getValue ("recentItems"));
    list.removeEmptyStrings();
    return list;
}

void Settings::addRecentItem (const juce::String& key)
{
    auto list = recentItems();
    list.removeString (key);
    list.insert (0, key);
    while (list.size() > kMaxRecentItems)
        list.remove (list.size() - 1);
    props->setValue ("recentItems", list.joinIntoString ("\n"));
}

bool Settings::firstRunDone() const { return props->getBoolValue ("firstRunDone", false); }
void Settings::setFirstRunDone() { props->setValue ("firstRunDone", true); }

bool Settings::midiInputPlays (const juce::String& id) const { return props->getBoolValue ("midiPlays/" + id, true); }
void Settings::setMidiInputPlays (const juce::String& id, bool on) { props->setValue ("midiPlays/" + id, on); }

double Settings::recordCorrectionMs() const { return juce::jlimit (-50.0, 50.0, props->getDoubleValue ("recordCorrectionMs", 0.0)); }
void Settings::setRecordCorrectionMs (double ms) { props->setValue ("recordCorrectionMs", juce::jlimit (-50.0, 50.0, ms)); }

bool Settings::copySamplesIntoProject() const { return props->getBoolValue ("copySamples", true); }
void Settings::setCopySamplesIntoProject (bool on) { props->setValue ("copySamples", on); }
int Settings::recordBitDepth() const
{
    const int v = props->getIntValue ("recordBitDepth", 24);
    return v == 16 || v == 32 ? v : 24;
}
void Settings::setRecordBitDepth (int bits) { props->setValue ("recordBitDepth", bits == 16 || bits == 32 ? bits : 24); }
double Settings::audioLatencyCorrectionMs() const { return juce::jlimit (-100.0, 100.0, props->getDoubleValue ("audioLatencyMs", 0.0)); }
void Settings::setAudioLatencyCorrectionMs (double ms) { props->setValue ("audioLatencyMs", std::round (juce::jlimit (-100.0, 100.0, ms) * 10.0) / 10.0); }
bool Settings::audioInputsOpened() const { return props->getBoolValue ("audioInputsOpened", false); }
void Settings::setAudioInputsOpened (bool on) { props->setValue ("audioInputsOpened", on); }
int Settings::audioCaptureMinutes() const
{
    const int v = props->getIntValue ("audioCaptureMinutes", 5);
    return v == 1 || v == 2 || v == 10 ? v : 5;
}
void Settings::setAudioCaptureMinutes (int m) { props->setValue ("audioCaptureMinutes", m == 1 || m == 2 || m == 10 ? m : 5); }
bool Settings::trimRecordedSilence() const { return props->getBoolValue ("trimSilence", false); }
void Settings::setTrimRecordedSilence (bool on) { props->setValue ("trimSilence", on); }

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
