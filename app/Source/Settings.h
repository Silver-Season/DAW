#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

#include <map>
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

    // Window layout (task 1.4): panel sizes and visibility, position of the main window
    int browserWidth() const;  // 180 .. 420
    void setBrowserWidth (int);
    int detailHeight() const;  // 160 .. 480
    int editorHeight() const;  // 160 .. 900, the panel while it shows the clip editor
    void setDetailHeight (int);
    void setEditorHeight (int);
    bool browserVisible() const;
    void setBrowserVisible (bool);
    bool detailVisible() const;
    void setDetailVisible (bool);
    bool mixerVisible() const;
    void setMixerVisible (bool);
    juce::String windowState() const; // juce::DocumentWindow::getWindowStateAsString()
    void setWindowState (const juce::String&);

    // Library
    juce::StringArray recentProjects() const;
    void addRecentProject (const juce::File&);
    void removeRecentProject (const juce::File&);
    void clearRecentProjects();
    juce::StringArray placeFolders() const;
    void setPlaceFolders (const juce::StringArray&);
    double previewVolumeDb() const; // -12 dB by default
    void setPreviewVolumeDb (double);
    bool previewEnabled() const; // the headphones button of the browser
    void setPreviewEnabled (bool);
    juce::String libraryPath() const; // User Library; empty = ~/Music/Prisma DAW/User Library
    void setLibraryPath (const juce::String&);
    // Colour labels of browser items (0..6, -1 = none), by item key.
    int favouriteColour (const juce::String& key) const;
    void setFavouriteColour (const juce::String& key, int colour);
    std::map<juce::String, int> favourites() const;
    // The last 30 items used from the browser, newest first.
    static constexpr int kMaxRecentItems = 30;
    juce::StringArray recentItems() const;
    void addRecentItem (const juce::String& key);
    bool firstRunDone() const; // false until the demo project was shown once
    void setFirstRunDone();

    // Audio
    std::unique_ptr<juce::XmlElement> audioDeviceState() const;
    void setAudioDeviceState (const juce::XmlElement&);
    // User names of the channels of a device ("Microphone", "Guitar"), keyed by device name.
    juce::String channelName (const juce::String& device, bool isInput, int channel) const;
    void setChannelName (const juce::String& device, bool isInput, int channel, const juce::String& name);

    // MIDI inputs: whether a device plays the armed tracks (on by default), by device identifier.
    bool midiInputPlays (const juce::String& identifier) const;
    void setMidiInputPlays (const juce::String& identifier, bool);

    // Plugins (list written by the plugin scanner)
    std::unique_ptr<juce::XmlElement> knownPlugins() const;
    void setKnownPlugins (const juce::XmlElement&);

private:
    static juce::String channelKey (const juce::String& device, bool isInput, int channel);

    std::unique_ptr<juce::PropertiesFile> props;
};
