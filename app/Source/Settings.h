#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

#include <memory>

// Application settings: a thin typed layer over juce::PropertiesFile. Keys are private to this class,
// so the rest of the code never spells a settings key as a string.
class Settings
{
public:
    static constexpr int kMaxRecentProjects = 10;

    // Settings in the per-user application folder (~/.config/DAW on Linux, ~/Library/Application Support/DAW on macOS).
    Settings();
    // Settings in an explicit file (tests).
    explicit Settings (const juce::File& file);
    ~Settings();

    juce::PropertiesFile& getPropertiesFile() { return *props; }
    void save();

    // Appearance
    juce::String theme() const;
    void setTheme (const juce::String& id);
    double uiScale() const; // 0.8 .. 1.5
    void setUiScale (double scale);
    bool animations() const;
    void setAnimations (bool);

    // Library
    juce::StringArray recentProjects() const;
    void addRecentProject (const juce::File&);
    void removeRecentProject (const juce::File&);
    void clearRecentProjects();
    juce::StringArray placeFolders() const;
    void setPlaceFolders (const juce::StringArray&);
    double previewVolumeDb() const; // -12 dB by default
    void setPreviewVolumeDb (double);

    // Audio
    std::unique_ptr<juce::XmlElement> audioDeviceState() const;
    void setAudioDeviceState (const juce::XmlElement&);
    // User names of the channels of a device ("Microphone", "Guitar"), keyed by device name.
    juce::String channelName (const juce::String& device, bool isInput, int channel) const;
    void setChannelName (const juce::String& device, bool isInput, int channel, const juce::String& name);

    // Plugins (list written by the plugin scanner)
    std::unique_ptr<juce::XmlElement> knownPlugins() const;
    void setKnownPlugins (const juce::XmlElement&);

private:
    static juce::String channelKey (const juce::String& device, bool isInput, int channel);

    std::unique_ptr<juce::PropertiesFile> props;
};
