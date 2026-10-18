#include "MainComponent.h"

#include "Inspiration.h"

#include "DemoProject.h"
#include "Strings.h"
#include "engine/PluginFolders.h"
#include "project/AudioImport.h"
#include "ui/Dialogs.h"
#include "ui/ThemeLookAndFeel.h"

#include "daw/synth/PrismaParams.h"

using namespace prisma_ui;

//==============================================================================
class PluginWindow final : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioProcessorEditor* editor, const juce::String& title, std::function<void()> onClose)
        : DocumentWindow (title, theme().chrome, DocumentWindow::closeButton | DocumentWindow::minimiseButton),
          closeCallback (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override { closeCallback(); } // owner deletes this window

private:
    std::function<void()> closeCallback;
};

// Keyboard shortcuts of the command manager, except while the focus is in a text field: typing a name must
// not trigger Delete, Cmd+D or the space bar.
class MainComponent::ShortcutRouter final : public juce::KeyListener
{
public:
    explicit ShortcutRouter (juce::KeyPressMappingSet& m) : mappings (m) {}

    bool keyPressed (const juce::KeyPress& key, juce::Component* origin) override
    {
        if (ui::textFieldHasFocus())
            return false;
        return mappings.keyPressed (key, origin);
    }
    bool keyStateChanged (bool isDown, juce::Component* origin) override
    {
        return ! ui::textFieldHasFocus() && mappings.keyStateChanged (isDown, origin);
    }

private:
    juce::KeyPressMappingSet& mappings;
};

// The computer keyboard keeps playing while a plugin window has the focus (turning a knob with a note held),
// and its note keys are swallowed there instead of making macOS beep.
class MainComponent::NoteKeysListener final : public juce::KeyListener
{
public:
    explicit NoteKeysListener (MainComponent& o) : owner (o) {}
    bool keyPressed (const juce::KeyPress& key, juce::Component*) override { return owner.keyPressed (key); }
    bool keyStateChanged (bool isDown, juce::Component*) override { return owner.keyStateChanged (isDown); }

private:
    MainComponent& owner;
};

//==============================================================================
MainComponent::MainComponent (AudioEngine& e, ProjectModel& m)
    : engine (e), model (m), settings (e.getSettings()), actions (m, &e), recorder (e, m),
      library (settings.libraryPath().isNotEmpty() ? juce::File (settings.libraryPath()) : library::UserLibrary::defaultRoot()),
      sources (settings, e.getKnownPlugins(), library), browserActions (m, e, library),
      doc (e, m, settings, library, juce::File::getSpecialLocation (juce::File::userMusicDirectory)), editCommands (e, m, actions, selection),
      menuModel (commandManager), computerKeys (e.getKeyboardState()),
      browserWidth (settings.browserWidth()), detailHeight (settings.detailHeight()), editorHeight (settings.editorHeight()),
      browserVisible (settings.browserVisible()), detailVisible (settings.detailVisible())
{
    setHelpText (Strings::help::logo);

    topBar = std::make_unique<ui::TopBar> (engine, model);
    topBar->setRecorder (&recorder);
    topBar->onRecordClicked = [this] { recordClicked(); };
    topBar->onRecordToArrangement = [this] { recordArrangementClicked(); };
    topBar->onRecordToSlot = [this] { recordToSlotClicked(); };
    recorder.arrangement().onFinished = [this] (const ArrangementRecorder::Summary& s) {
        infoBar->showMessage (s.warning.isNotEmpty() ? s.warning : Strings::arrRec::recorded (s.numClips, s.numTakes), s.warning.isNotEmpty() ? 8000 : 4000);
    };
    recorder.onWarning = [this] (const juce::String& text) { infoBar->showMessage (text, 8000); };
    recorder.sound().ensureProjectFolder = [this] { return doc.ensureFolder(); };
    topBar->onCaptureClicked = [this] { captureClicked(); };
    topBar->onCpuClicked = [this] { showSettings (ui::SettingsWindow::sound); };
    topBar->onVersionClicked = [this] { commandManager.invokeDirectly (ui::commands::versionHistory, false); };
    browser = std::make_unique<ui::BrowserPanel> (engine, settings, sources);
    sources.onChanged = [this] { browser->sourcesChanged(); };
    browser->onActivate = [this] (const browser::DragItem& item) {
        reportDrop (browserActions.useOnTrack (item, selection.get().trackId), item);
    };
    sources.refreshAll();
    engine.getKnownPlugins().addChangeListener (this);
    session = std::make_unique<ui::SessionView> (engine, model, actions, selection, editCommands);
    browserActions.ensureProjectFolder = [this] { return doc.ensureFolder(); };
    session->setBrowserActions (&browserActions);
    session->setRecorder (&recorder);
    session->onDropped = [this] (const browser::DropResult& r, const browser::DragItem& item) { reportDrop (r, item); };
    session->onSaveChain = [this] (int64_t trackId) { saveChainOf (trackId); };
    session->setMixerVisible (settings.mixerVisible());
    session->onMixerVisibilityChanged = [this] (bool visible) {
        settings.setMixerVisible (visible);
        commandManager.commandStatusChanged();
    };
    detail = std::make_unique<ui::DetailPanel> (engine, model, actions, selection);
    detail->setOpenDeviceHandler ([this] (int64_t deviceId) { openInstrumentEditor (deviceId); });
    detail->chain().onPresetMenu = [this] (int64_t deviceId, juce::Point<int> screenPosition) { showDeviceMenu (deviceId, screenPosition); };
    detail->chain().setBrowserActions (&browserActions);
    detail->chain().onDropped = [this] (const browser::DropResult& r, const browser::DragItem& item) { reportDrop (r, item); };
    arrangement = std::make_unique<ui::ArrangementView> (engine, model, actions, selection);
    arrangement->setRecorder (&recorder);
    arrangement->setBrowserActions (&browserActions);
    arrangement->onOpenClip = [this] (int64_t trackId, int64_t aclipId) { openArrangementClip (trackId, aclipId); };
    arrangement->onMessage = [this] (const juce::String& text) { infoBar->showMessage (text, 3000); };
    arrangement->lettersReserved = [this] { return computerKeys.isOn(); };
    session->onOpenClip = [this] (int64_t trackId, int scene) { openClipEditor (trackId, scene); };
    detail->onEditorShownChanged = [this] {
        resized();
        commandManager.commandStatusChanged();
    };
    detail->pianoRoll().onClipExtended = [this] (double beats) { infoBar->showMessage (Strings::editor::loopExtended (beats), 3500); };
    topBar->onViewSwitched = [this] (bool toArrangement) { setArrangementShown (toArrangement); };
    keyboardStrip = std::make_unique<ui::KeyboardStrip> (computerKeys);
    computerKeys.projectScale = [this] { return ui::ComputerKeys::ScaleInfo { model.scaleRoot(), model.scaleMode() }; };
    infoBar = std::make_unique<ui::InfoBar> (*this);
    computerKeys.onMessage = [this] (const juce::String& text) { infoBar->showMessage (text, 2500); };
    computerKeys.onStateChanged = [this] {
        topBar->setComputerKeysOn (computerKeys.isOn());
        topBar->setKeysOctave (computerKeys.octaveText());
        syncEditorKeyboards();
    };
    topBar->setKeysOctave (computerKeys.octaveText());

    arrangement->setVisible (false);
    keyboardStrip->setVisible (false);
    for (auto* c : std::initializer_list<juce::Component*> { topBar.get(), browser.get(), session.get(), arrangement.get(), keyboardStrip.get(), detail.get(), infoBar.get(),
                                                              &browserSplitter, &detailSplitter, &browserTab, &detailTab, &scanlines })
        addAndMakeVisible (c);

    browserSplitter.setHelpText (Strings::help::splitterBrowser);
    detailSplitter.setHelpText (Strings::help::splitterDetail);
    browserTab.setHelpText (Strings::help::splitterBrowser);
    detailTab.setHelpText (Strings::help::splitterDetail);
    browserTab.setTooltips (Strings::tips::hideBrowser, Strings::tips::showBrowser);
    detailTab.setTooltips (Strings::tips::hideDetail, Strings::tips::showDetail);
    browserSplitter.onDragStart = [this] { dragStartSize = browserWidth; };
    browserSplitter.onDrag = [this] (int delta) {
        browserWidth = juce::jlimit (ui::BrowserPanel::kMinWidth, ui::BrowserPanel::kMaxWidth, dragStartSize + delta);
        settings.setBrowserWidth (browserWidth);
        resized();
    };
    detailSplitter.onDragStart = [this] { dragStartSize = currentDetailHeight(); };
    detailSplitter.onDrag = [this] (int delta) {
        if (detail->isEditorShown())
        {
            editorHeight = juce::jlimit (ui::DetailPanel::kMinHeight, juce::jmax (ui::DetailPanel::kMinHeight, getHeight() - 260), dragStartSize - delta);
            settings.setEditorHeight (editorHeight);
        }
        else
        {
            detailHeight = juce::jlimit (ui::DetailPanel::kMinHeight, ui::DetailPanel::kMaxHeight, dragStartSize - delta);
            settings.setDetailHeight (detailHeight);
        }
        resized();
    };
    // Double-click: back and forth between the usual height and a tall editor
    detailSplitter.onDoubleClick = [this] {
        const int tall = juce::jmax (ui::DetailPanel::kMinHeight, juce::roundToInt ((float) getHeight() * 0.6f));
        if (detail->isEditorShown())
        {
            editorHeight = std::abs (editorHeight - tall) < 8 ? 360 : tall;
            settings.setEditorHeight (editorHeight);
        }
        else
        {
            detailHeight = std::abs (detailHeight - tall) < 8 ? ui::DetailPanel::kDefaultHeight : juce::jmin (tall, ui::DetailPanel::kMaxHeight);
            settings.setDetailHeight (detailHeight);
        }
        resized();
    };
    browserTab.onClick = [this] { setBrowserVisible (! browserVisible); };
    detailTab.onClick = [this] { setDetailVisible (! detailVisible); };
    browserTab.setOpen (browserVisible);
    detailTab.setOpen (detailVisible);

    // Menu and shortcuts
    commandManager.registerAllCommandsForTarget (this);
    commandManager.setFirstCommandTarget (this);
    router = std::make_unique<ShortcutRouter> (*commandManager.getKeyMappings());
    addKeyListener (router.get());
    noteKeys = std::make_unique<NoteKeysListener> (*this);

    menuModel.themeIds = [] {
        juce::StringArray ids;
        for (const auto& t : ThemeManager::get().all())
            ids.add (t.id);
        return ids;
    };
    menuModel.themeNames = [] {
        juce::StringArray names;
        for (const auto& t : ThemeManager::get().all())
            names.add (t.name);
        return names;
    };
    menuModel.currentThemeId = [] { return ThemeManager::get().current().id; };
    menuModel.inspirationMenu = [this] {
        return inspiration::menu (
            model, engine, [this] { return selection.get().trackId; }, [this] { return juce::jmax (0, selection.get().scene); },
            [this] (const inspiration::Result& r) {
                if (r.ok && r.scene >= 0)
                    selection.selectSlot (r.trackId, r.scene);
                infoBar->showMessage (r.message, 4000);
            });
    };
    menuModel.onThemeChosen = [this] (const juce::String& id) {
        settings.setTheme (id);
        ThemeManager::get().setCurrent (id);
    };
    menuModel.recentProjects = [this] { return settings.recentProjects(); };
    menuModel.onOpenRecent = [this] (const juce::String& path) {
        confirmDiscard ([this, path] { doOpen (juce::File (path)); });
    };
    menuModel.onClearRecent = [this] { settings.clearRecentProjects(); };
#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (&menuModel, nullptr);
#else
    if (juce::SystemStats::getEnvironmentVariable ("DAW_NO_MENUBAR", {}).isEmpty())
    {
        menuBar = std::make_unique<juce::MenuBarComponent> (&menuModel);
        addAndMakeVisible (*menuBar);
    }
#endif

    if (model.findTrack (model.selectedTrack()).isValid())
        selection.selectTrack (model.selectedTrack());

    model.root.addListener (this);
    model.undo.addChangeListener (this);
    recorder.startPolling();
    doc.onDocumentChanged = [this] { documentChanged(); };
    doc.onWriteFailed = [this] (const juce::String& error) { infoBar->showMessage (error, 10000); };
    ThemeManager::get().addChangeListener (this);
    applyTheme();

    setWantsKeyboardFocus (true);
    setSize (1440, 880);
    startTimerHz (30);
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)] {
        if (safe != nullptr)
            safe->grabKeyboardFocus();
    });
}

void MainComponent::attachKeyListeners (juce::Component& window)
{
    window.addKeyListener (noteKeys.get());
    window.addKeyListener (router.get());
}

MainComponent::~MainComponent()
{
    stopTimer();
    topBar->setRecorder (nullptr);
    session->setRecorder (nullptr);
    arrangement->setRecorder (nullptr);
    recorder.arrangement().onFinished = nullptr;
#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
#endif
    ThemeManager::get().removeChangeListener (this);
    engine.getKnownPlugins().removeChangeListener (this);
    model.undo.removeChangeListener (this);
    doc.onDocumentChanged = nullptr;
    doc.onWriteFailed = nullptr;
    model.root.removeListener (this);
    removeKeyListener (router.get());
    editorWindows.clear();
    scannerWindow.reset();
    versionWindow.reset();
    settingsWindow.reset();
    propertiesWindow.reset();
    keyboardStrip.reset();
    computerKeys.releaseAll();
}

//==============================================================================
juce::String MainComponent::windowTitle() const
{
    // An unsaved project lives in a folder named after the time; the user sees it as untitled.
    const auto name = doc.hasFile() && doc.name().isNotEmpty() ? doc.name() : Strings::windowUntitled;
    return doc.isDirty() ? name + juce::String::fromUTF8 (" \xe2\x80\xa2") : name;
}

void MainComponent::updateTitle()
{
    const auto title = windowTitle();
    if (title != lastTitle)
    {
            lastTitle = title;
        if (onTitleChanged)
            onTitleChanged (title);
    }
}

void MainComponent::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int)
{
    closeEditorsOfMissingDevices();
}

void MainComponent::closeEditorsOfMissingDevices()
{
    for (auto it = editorWindows.begin(); it != editorWindows.end();)
        it = model.findDevice (it->first).isValid() ? std::next (it) : editorWindows.erase (it);
}

//==============================================================================
void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &engine.getKnownPlugins())
        sources.refreshPlugins();
    else if (source == &model.undo)
    {
        // A fresh project has an empty history; anything else in it is an edit of the user's.
        if (model.undo.canUndo() || model.undo.canRedo())
            doc.noteEdited();
    }
    else
        applyTheme();
}

void MainComponent::applyTheme()
{
    if (auto* lnf = dynamic_cast<ThemeLookAndFeel*> (&juce::LookAndFeel::getDefaultLookAndFeel()))
        lnf->refresh();
    scanlines.setVisible (theme().scanlines);
    sendLookAndFeelChange();
    repaint();
}

void MainComponent::setBrowserVisible (bool visible)
{
    browserVisible = visible;
    settings.setBrowserVisible (visible);
    browserTab.setOpen (visible);
    resized();
    commandManager.commandStatusChanged();
}

void MainComponent::setDetailVisible (bool visible)
{
    detailVisible = visible;
    settings.setDetailVisible (visible);
    detailTab.setOpen (visible);
    resized();
    commandManager.commandStatusChanged();
}

void MainComponent::setArrangementShown (bool shown)
{
    arrangementShown = shown;
    topBar->setArrangementShown (shown);
    resized();
    if (shown)
    {
        arrangement->tick();
        arrangement->takeFocus(); // its shortcuts (Cmd+E, Cmd+D, Space ...) work at once
    }
    else
        session->tick(); // it was not ticking while hidden
    commandManager.commandStatusChanged();
}

void MainComponent::setKeyboardStripVisible (bool visible)
{
    stripWanted = visible;
    if (visible)
        keyboardStrip->setVisible (true);
    commandManager.commandStatusChanged();
}

void MainComponent::openClipEditor (int64_t trackId, int scene)
{
    if (arrangementShown)
        setArrangementShown (false);
    selection.selectSlot (trackId, scene);
    if (! detailVisible)
        setDetailVisible (true);
    detail->setEditorMode (true);
    commandManager.commandStatusChanged();
}

// Double-click on a clip of the Arrangement: the editor opens in the detail panel, the timeline stays.
void MainComponent::openArrangementClip (int64_t trackId, int64_t aclipId)
{
    model.setSelectedTrack (trackId);
    selection.selectArrangementClip (trackId, aclipId);
    if (! detailVisible)
        setDetailVisible (true);
    detail->setEditorMode (true);
    commandManager.commandStatusChanged();
}

int MainComponent::currentDetailHeight() const
{
    return detail->isEditorShown() ? editorHeight : detailHeight;
}

void MainComponent::showProjectProperties()
{
    if (propertiesWindow == nullptr)
        propertiesWindow = std::make_unique<ui::ProjectPropertiesWindow> (model, actions, [this] {
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)] {
                if (safe != nullptr)
                    safe->propertiesWindow.reset();
            });
        });
    propertiesWindow->toFront (true);
}

void MainComponent::setUiScale (double scale)
{
    scale = juce::jlimit (0.8, 1.5, std::round (scale * 10.0) / 10.0);
    settings.setUiScale (scale);
    juce::Desktop::getInstance().setGlobalScaleFactor ((float) scale);
}

//==============================================================================
void MainComponent::getAllCommands (juce::Array<juce::CommandID>& commands)
{
    using namespace ui::commands;
    commands.addArray ({ splitClips, joinClips, loopSelection, removeTime, addMarker, toggleFollow, toggleAutomation, newProject,  openProject,    saveProject,    saveProjectAs, saveTemplate,        versionHistory, exportAudio, showInFinder,
                         scanPlugins, quitApp,        undo,           redo,          cut,                 copy,           paste,       duplicate,
                         deleteItem,  renameItem,         newTrack,       newAudioTrack,  findMissingFiles, deleteUnusedRecordings, newScene,      captureScene,        newClip,        captureMidi,        recordArrangement, toggleBrowser, toggleDetail,
                         toggleMixer, switchView,     toggleClipEditor, toggleKeyboardStrip, projectProperties, quantiseNotes, quantiseOptions, zoomIn,         zoomOut,       openSettings,        playStop,       toggleComputerKeys, focusSearch });
}

void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    using namespace ui::commands;
    namespace M = Strings::menu;
    const auto cmd = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;
    const auto alt = juce::ModifierKeys::altModifier;
    const auto fileCategory = M::file, editCategory = M::edit, createCategory = M::create, viewCategory = M::view;

    auto set = [&] (const juce::String& name, const juce::String& category, bool active, juce::KeyPress key = {}) {
        info.setInfo (name, name, category, 0);
        info.setActive (active);
        if (key != juce::KeyPress())
            info.addDefaultKeypress (key.getKeyCode(), key.getModifiers());
    };
    auto key = [] (juce::juce_wchar c, int mods) { return juce::KeyPress (c, juce::ModifierKeys (mods), 0); };

    switch (id)
    {
        // File: the export waits for stage 4
        case newProject: set (M::newProject, fileCategory, true, key ('n', cmd)); break;
        case openProject: set (M::open, fileCategory, true, key ('o', cmd)); break;
        case saveProject: set (M::save, fileCategory, true, key ('s', cmd)); break;
        case saveProjectAs: set (M::saveAs, fileCategory, true, key ('s', cmd | shift)); break;
        case saveTemplate: set (M::saveTemplate, fileCategory, true); break;
        case versionHistory: set (M::versionHistory, fileCategory, true); break;
        case exportAudio: set (M::exportAudio, fileCategory, true, key ('r', cmd | shift)); break;
        case showInFinder: set (M::showInFinder, fileCategory, doc.folder() != juce::File() && doc.folder().exists()); break;
        case scanPlugins: set (M::scanPlugins, fileCategory, true); break;
        case findMissingFiles: set (M::findMissing, fileCategory, hasMissingFiles()); break;
        case deleteUnusedRecordings: set (M::deleteUnusedRecordings, fileCategory, true); break;
        case quitApp: set (M::quit, fileCategory, true, key ('q', cmd)); break;

        case undo:
        {
            const auto text = model.undo.getUndoDescription();
            set (text.isEmpty() ? M::undo : M::undo + " " + text, editCategory, model.undo.canUndo(), key ('z', cmd));
            break;
        }
        case redo:
        {
            const auto text = model.undo.getRedoDescription();
            set (text.isEmpty() ? M::redo : M::redo + " " + text, editCategory, model.undo.canRedo(), key ('z', cmd | shift));
            break;
        }
        case cut: set (M::cut, editCategory, arrangementShown ? ! arrangement->selectedClips().empty() : editCommands.canCopy(), key ('x', cmd)); break;
        case copy: set (M::copy, editCategory, arrangementShown ? ! arrangement->selectedClips().empty() : editCommands.canCopy(), key ('c', cmd)); break;
        case paste: set (M::paste, editCategory, arrangementShown ? arrangement->hasClipboard() : editCommands.canPaste(), key ('v', cmd)); break;
        case duplicate: set (M::duplicate, editCategory, arrangementShown ? true : editCommands.canDuplicate(), key ('d', cmd)); break;
        case deleteItem:
            set (M::remove, editCategory, arrangementShown ? true : editCommands.canDelete(), juce::KeyPress (juce::KeyPress::deleteKey));
            info.addDefaultKeypress (juce::KeyPress::backspaceKey, juce::ModifierKeys());
            break;
        case renameItem: set (M::rename, editCategory, editCommands.canRename(), key ('r', cmd)); break;

        case newTrack: set (M::newTrack, createCategory, true, key ('t', cmd)); break;
        case newAudioTrack: set (M::newAudioTrack, createCategory, true, key ('t', cmd | shift)); break;
        case newScene: set (arrangementShown ? Strings::arrangement::insertTime : M::newScene, createCategory, true, key ('i', cmd)); break;
        case captureScene: set (M::captureScene, createCategory, true, key ('i', cmd | shift)); break;
        case newClip: set (M::newClip, createCategory, model.numTracks() > 0, key ('m', cmd | shift)); break;
        case captureMidi: set (M::captureMidi, createCategory, model.numTracks() > 0, key ('c', cmd | shift)); break;
        case recordArrangement:
            set (Strings::arrRec::commandRecord, createCategory, model.numTracks() > 0, juce::KeyPress (juce::KeyPress::F9Key));
            info.setTicked (recorder.arrangement().isActive());
            break;

        case splitClips: set (Strings::arrangement::splitClip, editCategory, arrangementShown, key ('e', cmd)); break;
        case joinClips: set (Strings::arrangement::joinClips, editCategory, arrangementShown, key ('j', cmd)); break;
        case loopSelection: set (Strings::arrangement::loopSelection, editCategory, arrangementShown, key ('l', cmd)); break;
        case removeTime:
            set (Strings::arrangement::removeTime, editCategory, arrangementShown, juce::KeyPress (juce::KeyPress::backspaceKey, juce::ModifierKeys (cmd | shift), 0));
            info.addDefaultKeypress (juce::KeyPress::deleteKey, juce::ModifierKeys (cmd | shift));
            break;
        case addMarker: set (Strings::arrangement::newMarker, createCategory, arrangementShown, key ('m', cmd)); break;
        case toggleFollow:
            set (Strings::arrangement::followPlayhead, viewCategory, true, key ('f', cmd | shift));
            info.setTicked (model.followPlayhead());
            break;

        case toggleAutomation:
            set (Strings::arrangement::automationMode, viewCategory, arrangementShown);
            info.setTicked (arrangementShown && arrangement->automationMode());
            break;

        case toggleBrowser:
            set (M::browser, viewCategory, true, key ('b', cmd | alt));
            info.setTicked (browserVisible);
            break;
        case toggleDetail:
            set (M::detail, viewCategory, true, key ('l', cmd | alt));
            info.setTicked (detailVisible);
            break;
        case toggleMixer:
            set (M::mixer, viewCategory, true, key ('m', cmd | alt));
            info.setTicked (session->isMixerVisible());
            break;
        case switchView:
            set (M::switchView, viewCategory, true, juce::KeyPress (juce::KeyPress::tabKey));
            info.setTicked (arrangementShown);
            break;
        case toggleClipEditor:
            set (M::toggleClipEditor, viewCategory, true, juce::KeyPress (juce::KeyPress::tabKey, juce::ModifierKeys (shift), 0));
            info.setTicked (detail->editorMode());
            break;
        case toggleKeyboardStrip:
            set (M::keyboardStrip, viewCategory, true, key ('k', cmd | alt));
            info.setTicked (stripWanted);
            break;
        case projectProperties: set (M::projectProperties, fileCategory, true, key ('p', cmd | alt)); break;
        case quantiseNotes: set (M::quantise, editCategory, detail->isEditorShown(), key ('u', cmd)); break;
        case quantiseOptions: set (M::quantiseOptions, editCategory, detail->isEditorShown(), key ('u', cmd | shift)); break;
        case zoomIn:
            set (M::zoomIn, viewCategory, settings.uiScale() < 1.49, key ('=', cmd));
            info.addDefaultKeypress ('+', juce::ModifierKeys (cmd | shift));
            break;
        case zoomOut: set (M::zoomOut, viewCategory, settings.uiScale() > 0.81, key ('-', cmd)); break;
        case openSettings: set (M::settings, M::view, true, key (',', cmd)); break;

        case playStop: set (M::playStop, M::view, true, juce::KeyPress (juce::KeyPress::spaceKey)); break;
        case toggleComputerKeys:
            set (M::computerKeys, M::view, true, key ('m', 0));
            info.setTicked (computerKeys.isOn());
            break;
        case focusSearch: set (M::find, M::view, true, key ('f', cmd)); break;
        default: break;
    }
}

bool MainComponent::perform (const InvocationInfo& info)
{
    using namespace ui::commands;
    switch (info.commandID)
    {
        case newProject: confirmDiscard ([this] { doc.newProject(); }); return true;
        case openProject: confirmDiscard ([this] { doOpenDialog(); }); return true;
        case saveProject: doSave(); return true;
        case saveProjectAs: doSaveAs(); return true;
        case saveTemplate:
            if (const auto r = doc.saveAsDefaultTemplate(); r.wasOk())
                infoBar->showMessage (Strings::infoTemplateSaved);
            else
                ui::showMessage (Strings::dialogSaveFailed, r.getErrorMessage());
            return true;
        case versionHistory: showVersionHistory(); return true;
        case showInFinder: doc.folder().revealToUser(); return true;
        case openSettings: showSettings (ui::SettingsWindow::sound); return true;
        case scanPlugins: showPluginScanner(); return true;
        case findMissingFiles: findMissingFilesClicked(); return true;
        case exportAudio: showExport(); return true;
        case deleteUnusedRecordings: deleteUnusedRecordingsClicked(); return true;
        case quitApp: juce::JUCEApplication::getInstance()->systemRequestedQuit(); return true;

        case undo: model.undo.undo(); return true;
        case redo: model.undo.redo(); return true;
        case cut:
            if (arrangementShown)
                return arrangement->copySelection (true);
            editCommands.cutSelection();
            return true;
        case copy:
            if (arrangementShown)
                return arrangement->copySelection (false);
            editCommands.copySelection();
            return true;
        case paste:
            if (arrangementShown)
                return arrangement->pasteAtCursor();
            editCommands.pasteSelection();
            return true;
        case duplicate:
            if (arrangementShown)
                return arrangement->duplicateSelection();
            editCommands.duplicateSelection();
            return true;
        case deleteItem:
            if (arrangementShown)
                return arrangement->deleteSelection();
            editCommands.deleteSelection();
            return true;
        case renameItem:
            if (arrangementShown)
                return arrangement->renameSelection();
            session->startRename();
            return true;
        case splitClips: return arrangementShown && arrangement->splitAtCursor();
        case joinClips: return arrangementShown && arrangement->joinSelection();
        case loopSelection: return arrangementShown && arrangement->loopSelection();
        case removeTime: return arrangementShown && arrangement->removeTimeAtSelection();
        case addMarker: return arrangementShown && arrangement->addMarkerAtCursor();
        case toggleAutomation:
            if (! arrangementShown)
                return false;
            arrangement->toggleAutomationMode();
            commandManager.commandStatusChanged();
            return true;
        case toggleFollow:
            arrangement->toggleFollow();
            commandManager.commandStatusChanged();
            return true;

        case newTrack: editCommands.newTrack(); return true;
        case newAudioTrack: editCommands.newAudioTrack(); return true;
        case newScene:
            if (arrangementShown) // Cmd+I inserts time in the Arrangement and a scene in Session View
                return arrangement->insertTimeAtSelection();
            editCommands.newScene();
            return true;
        case captureScene: editCommands.captureScene(); return true;
        case newClip: editCommands.newClip(); return true;
        case captureMidi: captureClicked(); return true;
        case recordArrangement: recordArrangementClicked(); return true;

        case toggleBrowser: setBrowserVisible (! browserVisible); return true;
        case toggleDetail: setDetailVisible (! detailVisible); return true;
        case toggleMixer: session->setMixerVisible (! session->isMixerVisible()); return true;
        case zoomIn: setUiScale (settings.uiScale() + 0.1); return true;
        case zoomOut: setUiScale (settings.uiScale() - 0.1); return true;

        case playStop:
            if (engine.isPlaying())
                engine.stop();
            else
                engine.play();
            return true;
        case toggleComputerKeys: computerKeys.toggle(); return true;
        case switchView: setArrangementShown (! arrangementShown); return true;
        case toggleClipEditor:
            detail->setEditorMode (! detail->editorMode());
            if (detail->editorMode() && ! detailVisible)
                setDetailVisible (true);
            commandManager.commandStatusChanged();
            return true;
        case toggleKeyboardStrip: setKeyboardStripVisible (! stripWanted); return true;
        case projectProperties: showProjectProperties(); return true;
        case quantiseNotes: return detail->isEditorShown() && detail->pianoRoll().quantizeSelected();
        case quantiseOptions:
            if (! detail->isEditorShown())
                return false;
            detail->pianoRoll().showQuantizeWindow();
            return true;
        case focusSearch:
            if (! browserVisible)
                setBrowserVisible (true);
            browser->focusSearch();
            return true;

        default: break;
    }
    return false;
}

//==============================================================================
// The track that live notes go to: the selected one (a selected clip belongs to its track).
int64_t MainComponent::selectedTrackForRecording() const
{
    const auto& sel = selection.get();
    if ((sel.kind == ui::Selection::Kind::clip || sel.kind == ui::Selection::Kind::track || sel.kind == ui::Selection::Kind::device)
        && model.findTrack (sel.trackId).isValid())
        return sel.trackId;
    return model.selectedTrack();
}

int MainComponent::selectedSceneForRecording() const
{
    const auto& sel = selection.get();
    if ((sel.kind == ui::Selection::Kind::clip || sel.kind == ui::Selection::Kind::scene) && sel.scene >= 0 && sel.scene < model.numScenes())
        return sel.scene;
    return 0;
}

// What the record button means: see recordButtonAction (ArrangementRecorder.h).
void MainComponent::recordClicked()
{
    switch (recordButtonAction (recorder.arrangement().isActive(), recorder.isAnythingRecording(), arrangementShown, engine.isPlaying()))
    {
        case RecordButtonAction::stopTimeline: recorder.arrangement().stop(); break;
        case RecordButtonAction::stopSlots: recorder.sessionRecord (selectedSceneForRecording()); break; // stops them
        case RecordButtonAction::recordTimeline: recordArrangementClicked(); break;
        case RecordButtonAction::recordSlot: recordToSlotClicked(); break;
    }
}

void MainComponent::recordToSlotClicked()
{
    if (! recorder.sessionRecord (selectedSceneForRecording()) && ! recorder.isAnythingRecording())
        infoBar->showMessage (Strings::recording::recordNoTrack, 3500);
}

void MainComponent::recordArrangementClicked()
{
    auto& arr = recorder.arrangement();
    if (arr.isActive())
    {
        arr.stop();
        return;
    }
    if (recorder.isAnythingRecording())
        recorder.sessionRecord (selectedSceneForRecording());
    if (! arr.start())
        infoBar->showMessage (Strings::arrRec::noTrack, 3500);
}

void MainComponent::captureClicked()
{
    const auto summary = recorder.capture (selectedTrackForRecording(), selectedSceneForRecording());
    infoBar->showMessage (summary.info, 5000);
}

//==============================================================================
bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    // Keys that reach the window with nothing focused still drive the timeline
    if (arrangementShown && arrangement->handleKey (key))
        return true;
    return computerKeys.keyPressed (key);
}

bool MainComponent::keyStateChanged (bool)
{
    return computerKeys.keyStateChanged();
}

void MainComponent::focusLost (FocusChangeType)
{
    // Focus moving to a child or to a plugin window keeps held notes; key-ups that land elsewhere are caught
    // by the timer, and leaving the app releases everything.
}

//==============================================================================
void MainComponent::timerCallback()
{
    if (! juce::Process::isForegroundProcess())
        computerKeys.releaseAll();
    else
        computerKeys.releaseKeysNotDown();

    topBar->tick();
    topBar->setVersionText (doc.versionButtonText());
    if (arrangementShown)
        arrangement->tick();
    else
        session->tick();
    if (detailVisible)
        detail->tick();
    const double target = stripWanted ? 1.0 : 0.0;
    if (std::abs (stripFraction - target) > 1.0e-3)
    {
        stripFraction += (target - stripFraction) * 0.35;
        if (std::abs (stripFraction - target) < 0.02)
            stripFraction = target;
        keyboardStrip->setVisible (stripFraction > 0.0);
        resized();
    }
    updateTitle();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme().background);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    if (menuBar != nullptr)
        menuBar->setBounds (r.removeFromTop (kMenuBarHeight));
    topBar->setBounds (r.removeFromTop (ui::TopBar::kHeight));
    infoBar->setBounds (r.removeFromBottom (kInfoBarHeight));
    scanlines.setBounds (getLocalBounds());
    scanlines.toFront (false);

    const int stripHeight = juce::roundToInt (stripFraction * ui::KeyboardStrip::kHeight);
    keyboardStrip->setBounds (r.removeFromBottom (stripHeight));
    if (detailVisible)
    {
        const int h = juce::jmin (currentDetailHeight(), juce::jmax (ui::DetailPanel::kMinHeight, r.getHeight() - 200));
        detail->setBounds (r.removeFromBottom (h));
    }
    detail->setVisible (detailVisible);
    const int detailTop = r.getBottom();

    if (browserVisible)
        browser->setBounds (r.removeFromLeft (browserWidth));
    browser->setVisible (browserVisible);
    const int browserRight = r.getX();
    session->setBounds (r);
    session->setVisible (! arrangementShown);
    arrangement->setBounds (r);
    arrangement->setVisible (arrangementShown);

    // Splitters sit on the borders without taking room; the tabs sit in the padding of the Session View.
    browserSplitter.setVisible (browserVisible);
    browserSplitter.setBounds (browserRight - ui::Splitter::kThickness / 2 - 1, topBar->getBottom(), ui::Splitter::kThickness, detailTop - topBar->getBottom());
    detailSplitter.setVisible (detailVisible);
    detailSplitter.setBounds (0, detailTop - ui::Splitter::kThickness / 2, getWidth(), ui::Splitter::kThickness);
    browserTab.setBounds (browserRight, topBar->getBottom() + (detailTop - topBar->getBottom()) / 2 - 14, 10, 28);
    detailTab.setBounds (browserRight + r.getWidth() / 2 - 14 - 90, detailTop - 10, 28, 10);
    browserTab.toFront (false);
    detailTab.toFront (false);
    scanlines.toFront (false);
}

//==============================================================================
//==============================================================================
juce::File MainComponent::projectsFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Prisma DAW");
}

void MainComponent::documentChanged()
{
    editorWindows.clear();
    if (versionWindow != nullptr)
        versionWindow->refresh();
    selection.clear();
    if (model.findTrack (model.selectedTrack()).isValid())
        selection.selectTrack (model.selectedTrack());
    else if (model.numTracks() > 0)
        selection.selectTrack (model.trackAt (0).id());
    sources.setProjectFolder (doc.folder());
    if (doc.hasFile())
        settings.addRecentProject (doc.mainFile());
    updateTitle();
}

void MainComponent::confirmDiscard (std::function<void()> proceed)
{
    if (! doc.isDirty())
    {
        proceed();
        return;
    }
    const auto name = doc.hasFile() && doc.name().isNotEmpty() ? doc.name() : Strings::windowUntitled;
    ui::askThree (Strings::dialogSaveChanges (name), Strings::dialogSaveChangesText, Strings::dialogSave, Strings::dialogDontSave, Strings::dialogCancel,
                  [this, safe = juce::Component::SafePointer<MainComponent> (this), proceed = std::move (proceed)] (int answer) {
                      if (safe == nullptr)
                          return;
                      if (answer == 0)
                          doSave (proceed);
                      else if (answer == 1)
                          proceed();
                  });
}

void MainComponent::doSave (std::function<void()> then)
{
    if (! doc.hasFile())
    {
        doSaveAs (std::move (then));
        return;
    }
    const auto result = doc.save();
    if (! result.wasOk())
    {
        ui::showMessage (Strings::dialogSaveFailed, result.getErrorMessage());
        return;
    }
    settings.addRecentProject (doc.mainFile());
    infoBar->showMessage (Strings::infoSaved);
    if (then)
        then();
}

void MainComponent::doSaveAs (std::function<void()> then)
{
    ui::promptText (Strings::dialogSaveAsTitle, Strings::dialogSaveAsPrompt, doc.name(),
                    [this, safe = juce::Component::SafePointer<MainComponent> (this), then = std::move (then)] (const juce::String& entered) {
                        if (safe == nullptr)
                            return;
                        const auto name = juce::File::createLegalFileName (entered).trim();
                        if (name.isEmpty())
                            return;
                        const auto target = projectsFolder().getChildFile (name + " Project");
                        if (target.exists())
                        {
                            ui::showMessage (Strings::dialogSaveFailed, Strings::errorFolderExists (target.getFullPathName()));
                            return;
                        }
                        const auto result = doc.saveAs (target);
                        if (! result.wasOk())
                        {
                            ui::showMessage (Strings::dialogSaveFailed, result.getErrorMessage());
                            return;
                        }
                        infoBar->showMessage (Strings::infoSaved);
                        if (then)
                            then();
                    });
}

void MainComponent::showVersionHistory()
{
    if (versionWindow != nullptr)
    {
        versionWindow->refresh();
        versionWindow->toFront (true);
        return;
    }
    versionWindow = std::make_unique<ui::VersionWindow> (
        doc,
        [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)] {
                     if (safe != nullptr)
                         safe->versionWindow.reset();
                 }); },
        [this] (const juce::String& text) { infoBar->showMessage (text); });
}

namespace
{
#if JUCE_LINUX
constexpr bool useNativeFileDialogs = false; // the native chooser is unreliable under X11 window managers
#else
constexpr bool useNativeFileDialogs = true;
#endif
} // namespace

void MainComponent::doOpenDialog()
{
    chooser = std::make_unique<juce::FileChooser> (Strings::dialogOpenTitle, projectsFolder().isDirectory() ? projectsFolder() : juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.prisma", useNativeFileDialogs);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::FileChooser& fc) {
                              if (safe != nullptr && fc.getResult() != juce::File())
                                  safe->doOpen (fc.getResult());
                          });
}

bool MainComponent::hasMissingFiles() const
{
    for (const auto& f : model.audioFilesNode())
        if (! engine.resolveAudioFile (AudioFileRef (f)).existsAsFile())
            return true;
    return false;
}

void MainComponent::findMissingFilesClicked()
{
    const auto start = doc.folder() != juce::File() ? doc.folder() : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    chooser = std::make_unique<juce::FileChooser> (Strings::sessionAudio::findMissingTitle, start, "", useNativeFileDialogs);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::FileChooser& fc) {
                              if (safe == nullptr || fc.getResult() == juce::File())
                                  return;
                              safe->model.undo.beginNewTransaction (Strings::sessionAudio::undoRelink);
                              const int found = audioimport::relinkMissing (safe->model, safe->engine, fc.getResult());
                              safe->infoBar->showMessage (found > 0 ? Strings::sessionAudio::foundFiles (found) : Strings::sessionAudio::nothingFound, 3500);
                              safe->commandManager.commandStatusChanged();
                          });
}

// Recordings that no clip uses any more (undone takes) are deleted from Samples/Recorded and dropped from the pool. Files
// elsewhere (imported sounds) are only dropped from the pool, never deleted. Not undoable: the dialog says so.
void MainComponent::deleteUnusedRecordingsClicked()
{
    const auto unused = audioimport::unusedFiles (model);
    juce::Array<juce::File> files;
    const auto recorded = recorder.sound().recordingsFolder();
    for (const auto id : unused)
        if (const auto f = engine.resolveAudioFile (model.findAudioFile (id)); f.existsAsFile() && f.isAChildOf (recorded))
            files.add (f);
    if (files.isEmpty())
    {
        infoBar->showMessage (Strings::menu::noUnusedRecordings, 3000);
        return;
    }
    ui::confirm (Strings::menu::deleteUnusedRecordings, Strings::menu::deleteUnusedQuestion (files.size()), Strings::menu::deleteButton,
                 [safe = juce::Component::SafePointer<MainComponent> (this), files, unused] (bool ok) {
                     if (safe == nullptr || ! ok)
                         return;
                     for (const auto& f : files)
                         f.deleteFile();
                     for (const auto id : unused)
                         if (auto node = safe->model.findAudioFile (id); node.isValid())
                             node.state.getParent().removeChild (node.state, nullptr);
                     safe->model.undo.clearUndoHistory(); // undo could bring back clips whose files are gone
                     safe->infoBar->showMessage (Strings::menu::deletedRecordings (files.size()), 3000);
                 });
}

void MainComponent::showExport()
{
    if (exportWindow != nullptr)
    {
        exportWindow->toFront (true);
        return;
    }
    const auto folder = doc.folder() != juce::File() ? doc.folder().getChildFile ("Exports")
                                                     : juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("DAW").getChildFile ("Exports");
    const auto name = doc.name() + " " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d");
    exportWindow = std::make_unique<ui::ExportWindow> (settings, model, engine, folder, juce::File::createLegalFileName (name),
                                                       [this] { juce::MessageManager::callAsync ([this] { exportWindow.reset(); }); });
}

void MainComponent::doOpen (const juce::File& file)
{
    const auto result = doc.openFile (file);
    if (! result.wasOk())
    {
        settings.removeRecentProject (file);
        ui::showMessage (Strings::dialogOpenFailed, result.getErrorMessage());
    }
}

void MainComponent::openFromSystem (const juce::File& file)
{
    if (! file.existsAsFile())
        return;
    if (! started)
    {
        pendingOpen = file;
        return;
    }
    if (doc.hasFile() && doc.mainFile() == file)
        return; // already open
    confirmDiscard ([this, file] { doOpen (file); });
}

void MainComponent::startup (const juce::File& fileToOpen)
{
    started = true;
    if (fileToOpen.existsAsFile())
        pendingOpen = fileToOpen;
    const auto check = project::RecoveryCheck::check (project::ProjectSession::lockFileFor (settings));
    auto openFresh = [this] {
        if (const auto file = std::exchange (pendingOpen, juce::File()); file.existsAsFile())
        {
            settings.setFirstRunDone();
            doOpen (file);
            if (doc.hasFile())
                return;
        }
        if (! settings.firstRunDone())
        {
            // The first launch shows the demo project once.
            auto demo = ProjectModel::createEmpty();
            populateDemoProject (demo);
            doc.adoptNewTree (demo.root);
            settings.setFirstRunDone();
        }
        else
            doc.newProject();
    };
    if (! check.needed)
    {
        openFresh();
        return;
    }
    const auto name = check.projectFile != juce::File() ? check.projectFile.getFileNameWithoutExtension() : Strings::windowUntitled;
    ui::askThree (Strings::recoveryTitle, Strings::recoveryText (name), Strings::recoveryRestore, Strings::recoveryOpenSaved, Strings::recoveryDiscard,
                  [this, safe = juce::Component::SafePointer<MainComponent> (this), check, openFresh] (int answer) {
                      if (safe == nullptr)
                          return;
                      juce::Result result = juce::Result::ok();
                      if (answer == 0)
                      {
                          result = doc.openRecovered (check);
                          if (result.wasOk())
                          {
                              infoBar->showMessage (Strings::infoRecovered, 10000);
                              return;
                          }
                      }
                      else if (answer == 1 && check.projectFile.existsAsFile())
                      {
                          result = doc.openFile (check.projectFile);
                          if (result.wasOk())
                              return;
                      }
                      if (! result.wasOk())
                          ui::showMessage (Strings::dialogOpenFailed, result.getErrorMessage());
                      project::RecoveryCheck::removeLock (project::ProjectSession::lockFileFor (settings));
                      openFresh();
                  });
}

void MainComponent::requestQuit (std::function<void()> quitNow)
{
    confirmDiscard ([this, quitNow = std::move (quitNow)] {
        doc.closeCleanly();
        quitNow();
    });
}

void MainComponent::reportDrop (const browser::DropResult& result, const browser::DragItem& item)
{
    if (result.ok)
    {
        browser->noteUsed (item.key);
        if (result.trackId >= 0)
            selection.selectTrack (result.trackId);
    }
    else if (result.message.isNotEmpty())
        infoBar->showMessage (result.message);
}

void MainComponent::savePresetOf (int64_t deviceId)
{
    const auto device = model.findDevice (deviceId);
    if (! device.isValid())
        return;
    ui::promptText (Strings::browser::savePresetTitle, Strings::browser::savePresetPrompt, device.name(), [this, deviceId] (const juce::String& name) {
        juce::String error;
        const auto file = browserActions.savePreset (deviceId, name, error);
        if (file == juce::File())
        {
            ui::showMessage (Strings::browser::savePresetTitle, error.isNotEmpty() ? error : Strings::errorWriteFile (library.root().getFullPathName()));
            return;
        }
        sources.refreshLibrary();
        browser->selectCategory (browser::Category::presets);
        browser->selectItemByName (file.getFileNameWithoutExtension());
        infoBar->showMessage (Strings::browser::presetSaved (file.getFileNameWithoutExtension()));
    });
}

void MainComponent::saveChainOf (int64_t trackId)
{
    const auto track = model.findTrack (trackId);
    if (! track.isValid())
        return;
    ui::promptText (Strings::browser::saveChain, Strings::browser::saveChainPrompt, track.name(), [this, trackId] (const juce::String& name) {
        juce::String error;
        const auto file = browserActions.saveChain (trackId, name, error);
        if (file == juce::File())
        {
            ui::showMessage (Strings::browser::saveChain, error.isNotEmpty() ? error : Strings::errorWriteFile (library.root().getFullPathName()));
            return;
        }
        sources.refreshLibrary();
        browser->selectCategory (browser::Category::chains);
        browser->selectItemByName (file.getFileNameWithoutExtension());
        infoBar->showMessage (Strings::browser::chainSaved (file.getFileNameWithoutExtension()));
    });
}

void MainComponent::showDeviceMenu (int64_t deviceId, juce::Point<int> screenPosition)
{
    const auto device = model.findDevice (deviceId);
    if (! device.isValid())
        return;
    const auto trackNode = device.state.getParent().getParent();
    const int64_t trackId = (int64_t) (juce::int64) trackNode.getProperty (ids::id, -1);
    const auto track = model.findTrack (trackId);
    if (! track.isValid())
        return;
    const bool isInstrument = track.numDevices() > 0 && track.device (0).id() == deviceId;
    const bool isPrisma = device.kind() == "prisma";
    const bool isMissing = device.kind() == "missing";

    constexpr int kSave = 1, kFactory = 1000, kUser = 3000, kPrismaInstrument = 4000, kPlugin = 5000;
    juce::PopupMenu menu;
    menu.addItem (kSave, Strings::browser::savePreset, ! isMissing);

    const auto& presets = daw::synth::factoryPresets();
    if (isPrisma)
    {
        auto* processor = engine.findProcessor (deviceId);
        const int current = processor != nullptr ? processor->getCurrentProgram() : -1;
        juce::PopupMenu factory;
        for (int i = 0; i < (int) presets.size(); ++i)
            factory.addItem (kFactory + i, presets[(size_t) i].name, true, i == current);
        menu.addSubMenu (Strings::browser::factoryPresets, factory);
    }

    std::vector<library::PresetInfo> mine;
    const auto typeName = isPrisma ? juce::String ("Prisma") : device.name();
    for (auto& info : library.listPresets())
        if (info.deviceName == typeName)
            mine.push_back (info);
    juce::PopupMenu mineMenu;
    for (int i = 0; i < (int) mine.size(); ++i)
        mineMenu.addItem (kUser + i, mine[(size_t) i].data.name);
    if (mine.empty())
        mineMenu.addItem (-1, Strings::browser::noUserPresets, false);
    menu.addSubMenu (Strings::browser::myPresets, mineMenu, ! isMissing);

    std::vector<juce::PluginDescription> instruments;
    if (isInstrument)
    {
        for (auto& d : engine.getKnownPlugins().getTypes())
            if (d.isInstrument)
                instruments.push_back (d);
        std::sort (instruments.begin(), instruments.end(), [] (auto& a, auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });

        menu.addSeparator();
        juce::PopupMenu replace;
        replace.addItem (kPrismaInstrument, "Prisma", ! isPrisma);
        for (int i = 0; i < (int) instruments.size(); ++i)
            replace.addItem (kPlugin + i, instruments[(size_t) i].name + "  (" + instruments[(size_t) i].manufacturerName + ", " + instruments[(size_t) i].pluginFormatName + ")");
        if (instruments.empty())
            replace.addItem (-1, Strings::menuNoPlugins, false);
        menu.addSubMenu (Strings::browser::replaceInstrument, replace);
    }

    juce::Component::SafePointer<MainComponent> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
                        [safe, deviceId, trackId, mine, instruments] (int result) {
                            if (safe == nullptr || result <= 0)
                                return;
                            auto& self = *safe;
                            browser::DragItem item;
                            if (result == kSave)
                            {
                                self.savePresetOf (deviceId);
                                return;
                            }
                            if (result >= kFactory && result < kUser)
                            {
                                const int index = result - kFactory;
                                item.kind = browser::kinds::prismaPreset;
                                item.id = juce::String (index);
                                item.name = daw::synth::factoryPresets()[(size_t) index].name;
                                item.key = "prismaPreset:" + item.name;
                                self.reportDrop (self.browserActions.drop (item, browser::DropTarget::device, trackId, -1, -1, deviceId), item);
                            }
                            else if (result >= kUser && result < kPrismaInstrument)
                            {
                                const auto& info = mine[(size_t) (result - kUser)];
                                item.kind = browser::kinds::userPreset;
                                item.path = info.file.getFullPathName();
                                item.name = info.data.name;
                                item.key = "file:" + item.path;
                                self.reportDrop (self.browserActions.drop (item, browser::DropTarget::device, trackId, -1, -1, deviceId), item);
                            }
                            else if (result == kPrismaInstrument)
                            {
                                item.kind = browser::kinds::prisma;
                                item.name = "Prisma";
                                item.instrument = true;
                                item.key = "prisma";
                                self.reportDrop (self.browserActions.drop (item, browser::DropTarget::track, trackId), item);
                            }
                            else if (result >= kPlugin)
                            {
                                const auto& desc = instruments[(size_t) (result - kPlugin)];
                                item.kind = browser::kinds::plugin;
                                item.id = desc.createIdentifierString();
                                item.name = desc.name;
                                item.instrument = true;
                                item.key = "plugin:" + item.id;
                                self.reportDrop (self.browserActions.drop (item, browser::DropTarget::track, trackId), item);
                            }
                        });
}

void MainComponent::openInstrumentEditor (int64_t deviceId)
{
    auto* processor = engine.findProcessor (deviceId);
    if (processor == nullptr)
        return;
    if (auto it = editorWindows.find (deviceId); it != editorWindows.end())
    {
        it->second->toFront (true);
        return;
    }
    juce::AudioProcessorEditor* editor = processor->hasEditor() ? processor->createEditorIfNeeded() : nullptr;
    if (editor == nullptr)
        editor = new juce::GenericAudioProcessorEditor (*processor);
    auto window = std::make_unique<PluginWindow> (editor, processor->getName(), [this, deviceId] {
        // Defer deletion: this runs from the window's own close button.
        juce::MessageManager::callAsync ([this, alive = juce::Component::SafePointer<MainComponent> (this), deviceId] {
            if (alive != nullptr)
                editorWindows.erase (deviceId);
        });
    });
    attachKeyListeners (*window);
    editorWindows[deviceId] = std::move (window);
    if (auto* prisma = dynamic_cast<PrismaEditor*> (editor))
        prisma->getKeyboardPanel().onOctaveButton = [this] (int delta) { computerKeys.panel().shiftOctave (delta); };
    syncEditorKeyboards();
}

void MainComponent::syncEditorKeyboards()
{
    // A Prisma window shows the octave and velocity of the computer keyboard, which is what its keys play here.
    for (auto& [id, window] : editorWindows)
        if (auto* prisma = dynamic_cast<PrismaEditor*> (window->getContentComponent()))
            prisma->getKeyboardPanel().showState (computerKeys.panel().getBaseNote(), computerKeys.panel().getVelocity());
}

void MainComponent::showSettings (ui::SettingsWindow::Tab tab)
{
    if (settingsWindow == nullptr)
    {
        ui::SettingsWindow::Hooks hooks;
        hooks.libraryRootChanged = [this] (const juce::File& root) {
            library.setRoot (root);
            for (const auto& problem : ThemeManager::get().reload (library.themesFolder()))
                juce::ignoreUnused (problem);
            ThemeManager::get().setCurrent (settings.theme());
            sources.refreshAll();
        };
        hooks.placesChanged = [this] { sources.scanPlaces (false); };
        hooks.scanPlugins = [this] { showPluginScanner(); };
        hooks.scanPluginFolders = [this] { showPluginScanner (true); };
        hooks.closed = [this] {
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)] {
                if (safe != nullptr)
                    safe->settingsWindow.reset();
            });
        };
        settingsWindow = std::make_unique<ui::SettingsWindow> (engine, settings, std::move (hooks));
    }
    settingsWindow->showTab (tab);
    settingsWindow->toFront (true);
}

namespace
{
// The plug-in list with one line above it: where a VST3 scan looks, a link to change it and a button that scans now.
// A scan started here (or from Settings) runs each format in turn without the folder dialog: VST3 over the folders
// of the settings, then the Audio Units the system knows.
class PluginScanner final : public juce::Component, private juce::Timer
{
public:
    PluginScanner (AudioEngine& e, std::function<void()> openFolders) : engine (e)
    {
        auto& props = engine.getSettings().getPropertiesFile();
        auto deadMansPedal = props.getFile().getSiblingFile ("RecentlyCrashedPluginsList");
        list = std::make_unique<juce::PluginListComponent> (engine.getFormatManager(), engine.getKnownPlugins(), deadMansPedal, &props, true);
        addAndMakeVisible (*list);
        change.setButtonText (Strings::settingsui::change);
        change.onClick = std::move (openFolders);
        scan.setButtonText (Strings::settingsui::scanNow);
        scan.onClick = [this] { scanAll(); };
        line.setFont (theme().uiFont (12.5f));
        line.setColour (juce::Label::textColourId, theme().dimText);
        for (auto* c : std::initializer_list<juce::Component*> { &line, &change, &scan })
            addAndMakeVisible (c);
        refreshLine();
        setSize (760, 560);
    }

    void scanAll()
    {
        if (list->isScanning())
            return;
        pending.clear();
        for (auto* f : engine.getFormatManager().getFormats())
            if (f->getName() == "VST3" || f->getName() == "AudioUnit")
                pending.add (f->getName());
        startTimer (200);
        timerCallback();
    }

    void refreshLine()
    {
        const auto folders = pluginfolders::vst3Folders (engine.getSettings().getPropertiesFile(), engine.getFormatManager());
        line.setText (Strings::settingsui::scanFoldersLine (folders.size()), juce::dontSendNotification);
        line.setTooltip (folders.joinIntoString ("\n"));
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (40).reduced (10, 7);
        scan.setBounds (top.removeFromRight (120));
        top.removeFromRight (6);
        change.setBounds (top.removeFromRight (110));
        top.removeFromRight (6);
        line.setBounds (top);
        list->setBounds (r);
    }

    void paint (juce::Graphics& g) override { g.fillAll (theme().chrome); }

private:
    void timerCallback() override
    {
        refreshLine();
        if (list->isScanning())
            return;
        while (! pending.isEmpty())
        {
            const auto name = pending[0];
            pending.remove (0);
            if (auto* format = pluginfolders::findFormat (engine.getFormatManager(), name))
            {
                const auto files = pluginfolders::filesToScan (engine.getSettings().getPropertiesFile(), engine.getFormatManager(), *format);
                if (! files.isEmpty())
                {
                    list->scanFor (*format, files);
                    return;
                }
            }
        }
        stopTimer();
    }

    AudioEngine& engine;
    std::unique_ptr<juce::PluginListComponent> list;
    juce::Label line;
    juce::TextButton change, scan;
    juce::StringArray pending;
};
} // namespace

void MainComponent::showPluginScanner (bool scanNow)
{
    if (scannerWindow == nullptr)
    {
        auto* content = new PluginScanner (engine, [this] { showSettings (ui::SettingsWindow::plugins); });

        struct ScannerWindow final : juce::DocumentWindow
        {
            ScannerWindow (juce::Component* c, std::function<void()> onClose)
                : DocumentWindow (Strings::windowPlugins, theme().panel, DocumentWindow::closeButton), close (std::move (onClose))
            {
                setUsingNativeTitleBar (true);
                setContentOwned (c, true);
                setResizable (true, false);
                centreWithSize (getWidth(), getHeight());
                setVisible (true);
            }
            void closeButtonPressed() override { close(); }
            std::function<void()> close;
        };

        scannerWindow = std::make_unique<ScannerWindow> (content, [this] {
            juce::MessageManager::callAsync ([this] { scannerWindow.reset(); });
        });
    }
    scannerWindow->toFront (true);
    if (auto* scanner = dynamic_cast<PluginScanner*> (scannerWindow->getContentComponent()))
    {
        scanner->refreshLine();
        if (scanNow)
            scanner->scanAll();
    }
}
