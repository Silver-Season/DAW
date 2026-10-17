#pragma once

#include "../../plugins/prisma/Source/PrismaEditor.h"
#include "browser/BrowserActions.h"
#include "browser/BrowserSources.h"
#include "engine/AudioEngine.h"
#include "model/ProjectActions.h"
#include "model/ProjectModel.h"
#include "project/ProjectSession.h"
#include "ui/AppMenu.h"
#include "ui/BrowserPanel.h"
#include "ui/Chrome.h"
#include "ui/ComputerKeys.h"
#include "ui/ArrangementView.h"
#include "ui/DetailPanel.h"
#include "ui/KeyboardStrip.h"
#include "ui/ProjectPropertiesWindow.h"
#include "ui/EditCommands.h"
#include "ui/SettingsWindow.h"
#include "ui/SessionView.h"
#include "ui/TopBar.h"
#include "ui/VersionWindow.h"

class PluginWindow;

// The window content: top bar, browser, Session View, detail panel, info bar and the menu. It reads and
// writes only the ProjectModel (through ProjectActions); sound comes from the engine that listens to the model.
class MainComponent final : public juce::Component,
                            public juce::DragAndDropContainer,
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
    bool isDirty() const { return doc.isDirty(); }

    // Called once the window is visible: offers the recovery of a lost session, or opens the first-run demo
    // or a new project (the template if there is one).
    void startup();
    // Asks about unsaved changes, then calls `quitNow` (the lock is removed first). Cancel does nothing.
    void requestQuit (std::function<void()> quitNow);
    project::ProjectSession& document() { return doc; }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    // Keys that reach a window without passing through this component (nothing focused in it, or a plugin
    // window) still play notes and run shortcuts. The listeners live as long as this component.
    void attachKeyListeners (juce::Component& window);
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
    ui::KeyboardStrip& getKeyboardStrip() { return *keyboardStrip; }
    ui::ComputerKeys& getComputerKeys() { return computerKeys; }
    bool isArrangementShown() const { return arrangementShown; }
    bool isKeyboardStripWanted() const { return stripWanted; }

private:
    class ShortcutRouter;

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeRedirected (juce::ValueTree&) override { closeEditorsOfMissingDevices(); }

    void setBrowserVisible (bool);
    void setDetailVisible (bool);
    void setArrangementShown (bool);
    void setKeyboardStripVisible (bool);
    void openClipEditor (int64_t trackId, int scene);
    int currentDetailHeight() const;
    void showProjectProperties();
    void setUiScale (double);
    void applyTheme();
    void updateTitle();

    // project files (task 1.7)
    void confirmDiscard (std::function<void()> proceed);
    void doSave (std::function<void()> then = nullptr);
    void doSaveAs (std::function<void()> then = nullptr);
    void showVersionHistory();
    void doOpenDialog();
    void doOpen (const juce::File&);
    void documentChanged();
    juce::File projectsFolder() const;

    // the browser and the library (tasks 1.5 and 1.6)
    void showDeviceMenu (int64_t deviceId, juce::Point<int> screenPosition);
    void savePresetOf (int64_t deviceId);
    void saveChainOf (int64_t trackId);
    void reportDrop (const browser::DropResult&, const browser::DragItem&);

    void openInstrumentEditor (int64_t deviceId);
    void closeEditorsOfMissingDevices();
    void showSettings (ui::SettingsWindow::Tab);
    void showPluginScanner();

    AudioEngine& engine;
    ProjectModel& model;
    Settings& settings;
    ProjectActions actions;
    library::UserLibrary library;
    browser::BrowserSources sources;
    browser::BrowserActions browserActions;
    project::ProjectSession doc;
    ui::SelectionState selection;
    ui::EditCommands editCommands;

    std::unique_ptr<ui::TopBar> topBar;
    std::unique_ptr<ui::BrowserPanel> browser;
    std::unique_ptr<ui::SessionView> session;
    std::unique_ptr<ui::DetailPanel> detail;
    std::unique_ptr<ui::InfoBar> infoBar;
    std::unique_ptr<ui::ArrangementView> arrangement;
    std::unique_ptr<ui::KeyboardStrip> keyboardStrip;
    std::unique_ptr<ui::ProjectPropertiesWindow> propertiesWindow;
    bool arrangementShown = false, stripWanted = false;
    double stripFraction = 0.0; // 0 hidden, 1 shown; slides in the 30 Hz timer
    ui::Splitter browserSplitter { true }, detailSplitter { false };
    ui::EdgeTab browserTab { 3 }, detailTab { 2 };
    ui::ScanlineOverlay scanlines;

    juce::ApplicationCommandManager commandManager;
    ui::AppMenuModel menuModel;
    std::unique_ptr<juce::MenuBarComponent> menuBar;
    std::unique_ptr<ShortcutRouter> router;
    class NoteKeysListener;
    std::unique_ptr<NoteKeysListener> noteKeys;
    ui::ComputerKeys computerKeys; // turns the computer keys into notes (key M switches it on); the panel is shown in the keyboard strip

    int browserWidth, detailHeight, editorHeight, dragStartSize = 0;
    bool browserVisible, detailVisible;
    juce::String lastTitle;
    std::unique_ptr<juce::FileChooser> chooser;

    std::map<int64_t, std::unique_ptr<PluginWindow>> editorWindows; // by device id
    std::unique_ptr<juce::DocumentWindow> scannerWindow;
    std::unique_ptr<ui::VersionWindow> versionWindow;
    std::unique_ptr<ui::SettingsWindow> settingsWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
