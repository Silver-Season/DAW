#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ui
{
// Command ids of the main menu and the global shortcuts (task 1.10).
namespace commands
{
enum : juce::CommandID
{
    newProject = 0x2000,
    openProject,
    saveProject,
    saveProjectAs,
    projectProperties,
    saveTemplate,
    versionHistory,
    exportAudio,
    showInFinder,
    scanPlugins,
    quitApp,

    undo,
    redo,
    cut,
    copy,
    paste,
    duplicate,
    deleteItem,
    renameItem,
    quantiseNotes,
    quantiseOptions,

    newTrack,
    newScene,
    captureScene,
    newClip,

    toggleBrowser,
    toggleDetail,
    toggleMixer,
    switchView,
    toggleClipEditor,
    toggleKeyboardStrip,
    zoomIn,
    zoomOut,
    openSettings,

    playStop,
    toggleComputerKeys,
    focusSearch
};
} // namespace commands

// The menu bar: File, Edit, Create, View. On macOS it is the main menu of the screen, elsewhere a bar
// at the top of the window. Items are command items, so they show their shortcuts on the right.
class AppMenuModel final : public juce::MenuBarModel
{
public:
    explicit AppMenuModel (juce::ApplicationCommandManager&);

    // Themes in the View menu: id and display name; the handler is called with the chosen id.
    std::function<juce::StringArray()> themeIds, themeNames;
    std::function<juce::String()> currentThemeId;
    std::function<void (const juce::String&)> onThemeChosen;
    std::function<juce::StringArray()> recentProjects;
    std::function<void (const juce::String& path)> onOpenRecent;
    std::function<void()> onClearRecent;

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int index, const juce::String& name) override;
    void menuItemSelected (int, int) override {}
    juce::PopupMenu getMacExtraMenu();

private:
    juce::ApplicationCommandManager& manager;
};
} // namespace ui
