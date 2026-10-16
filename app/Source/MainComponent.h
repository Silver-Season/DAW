#pragma once

#include "../../plugins/prisma/Source/PrismaEditor.h"
#include "engine/AudioEngine.h"
#include "model/ProjectActions.h"
#include "model/ProjectModel.h"
#include "ui/AppMenu.h"
#include "ui/BrowserPanel.h"
#include "ui/Chrome.h"
#include "ui/DetailPanel.h"
#include "ui/EditCommands.h"
#include "ui/SessionView.h"
#include "ui/TopBar.h"

class PluginWindow;

// The window content: top bar, browser, Session View, detail panel, info bar and the menu. It reads and
// writes only the ProjectModel (through ProjectActions); sound comes from the engine that listens to the model.
class MainComponent final : public juce::Component,
                            public juce::ApplicationCommandTarget,
                            private juce::Timer,
                            private juce::ChangeListener,
                            private juce::ValueTree::Listener
{
public:
    static constexpr int kInfoBarHeight = 22;
    static constexpr int kMenuBarHeight = 24;

    MainComponent (AudioEngine&, ProjectModel&);
    ~MainComponent() override;

    // "Name" or "Name •" when there are unsaved changes (docs/spec/99-details.md, stage 1).
    juce::String windowTitle() const;
    std::function<void (const juce::String&)> onTitleChanged;
    void markClean();
    bool isDirty() const { return dirty; }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool) override;
    void focusLost (FocusChangeType) override;

    // juce::ApplicationCommandTarget
    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands (juce::Array<juce::CommandID>&) override;
    void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform (const InvocationInfo&) override;

    // Access for the screenshot script and tests.
    ui::SessionView& getSessionView() { return *session; }
    ui::InfoBar& getInfoBar() { return *infoBar; }
    juce::ApplicationCommandManager& getCommandManager() { return commandManager; }
    ui::BrowserPanel& getBrowser() { return *browser; }
    ui::DetailPanel& getDetailPanel() { return *detail; }

private:
    class ShortcutRouter;

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { dirty = true; }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { dirty = true; }

    void setBrowserVisible (bool);
    void setDetailVisible (bool);
    void setUiScale (double);
    void applyTheme();
    void updateTitle();

    // instruments and plugin windows (kept from phase 0 until the browser of task 1.5 replaces the menu)
    void showInstrumentMenu (int64_t deviceId, juce::Point<int> screenPosition);
    void setPrismaInstrument (int64_t trackId, const juce::String& presetName);
    void loadPluginInstrument (int64_t trackId, const juce::PluginDescription&);
    void openInstrumentEditor (int64_t deviceId);
    void closeEditorsOfMissingDevices();
    void showAudioSettings();
    void showPluginScanner();

    AudioEngine& engine;
    ProjectModel& model;
    Settings& settings;
    ProjectActions actions;
    ui::SelectionState selection;
    ui::EditCommands editCommands;

    std::unique_ptr<ui::TopBar> topBar;
    std::unique_ptr<ui::BrowserPanel> browser;
    std::unique_ptr<ui::SessionView> session;
    std::unique_ptr<ui::DetailPanel> detail;
    std::unique_ptr<ui::InfoBar> infoBar;
    ui::Splitter browserSplitter { true }, detailSplitter { false };
    ui::EdgeTab browserTab { 3 }, detailTab { 2 };
    ui::ScanlineOverlay scanlines;

    juce::ApplicationCommandManager commandManager;
    ui::AppMenuModel menuModel;
    std::unique_ptr<juce::MenuBarComponent> menuBar;
    std::unique_ptr<ShortcutRouter> router;
    prisma_ui::KeyboardPanel keyboard; // not shown: it turns the computer keys into notes (key M switches it on)
    bool computerKeysOn = false;

    int browserWidth, detailHeight, dragStartSize = 0;
    bool browserVisible, detailVisible;
    bool dirty = false;
    juce::String lastTitle;

    std::map<int64_t, std::unique_ptr<PluginWindow>> editorWindows; // by device id
    std::unique_ptr<juce::DocumentWindow> scannerWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
