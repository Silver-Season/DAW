#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

class AudioEngine;
class Settings;

namespace ui
{
// The settings window (docs/spec/01-layout-project.md, task 1.9): Sound, MIDI, Appearance and Library tabs.
class SettingsWindow final : public juce::DocumentWindow
{
public:
    struct Hooks
    {
        std::function<void (const juce::File&)> libraryRootChanged; // the folder of the User Library was changed
        std::function<void()> placesChanged;                         // folders of the browser's "Places" changed
        std::function<void()> scanPlugins;                           // opens the plugin scanner
        std::function<void()> closed;
    };

    SettingsWindow (AudioEngine&, Settings&, Hooks);
    ~SettingsWindow() override;

    enum Tab { sound, midi, appearance, library };
    void showTab (Tab);
    void closeButtonPressed() override;

private:
    class Content;
    Content* content = nullptr; // owned by the window
    Hooks hooks;
};
} // namespace ui
