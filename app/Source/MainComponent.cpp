#include "MainComponent.h"

#include "Strings.h"
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

//==============================================================================
MainComponent::MainComponent (AudioEngine& e, ProjectModel& m)
    : engine (e), model (m), settings (e.getSettings()), actions (m, &e),
      library (settings.libraryPath().isNotEmpty() ? juce::File (settings.libraryPath()) : library::UserLibrary::defaultRoot()),
      sources (settings, e.getKnownPlugins(), library), browserActions (m, e, library), editCommands (e, m, actions, selection),
      menuModel (commandManager), keyboard (e.getKeyboardState()),
      browserWidth (settings.browserWidth()), detailHeight (settings.detailHeight()),
      browserVisible (settings.browserVisible()), detailVisible (settings.detailVisible())
{
    setHelpText (Strings::help::logo);

    topBar = std::make_unique<ui::TopBar> (engine, model);
    topBar->onCpuClicked = [this] { showAudioSettings(); };
    browser = std::make_unique<ui::BrowserPanel> (engine, settings, sources, library);
    sources.onChanged = [this] { browser->sourcesChanged(); };
    browser->onActivate = [this] (const browser::DragItem& item) {
        reportDrop (browserActions.useOnTrack (item, selection.get().trackId), item);
    };
    sources.refreshAll();
    engine.getKnownPlugins().addChangeListener (this);
    session = std::make_unique<ui::SessionView> (engine, model, actions, selection, editCommands);
    session->setBrowserActions (&browserActions);
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
    infoBar = std::make_unique<ui::InfoBar> (*this);

    for (auto* c : std::initializer_list<juce::Component*> { topBar.get(), browser.get(), session.get(), detail.get(), infoBar.get(),
                                                              &browserSplitter, &detailSplitter, &browserTab, &detailTab, &scanlines })
        addAndMakeVisible (c);

    browserSplitter.setHelpText (Strings::help::splitterBrowser);
    detailSplitter.setHelpText (Strings::help::splitterDetail);
    browserTab.setHelpText (Strings::help::splitterBrowser);
    detailTab.setHelpText (Strings::help::splitterDetail);
    browserSplitter.onDragStart = [this] { dragStartSize = browserWidth; };
    browserSplitter.onDrag = [this] (int delta) {
        browserWidth = juce::jlimit (ui::BrowserPanel::kMinWidth, ui::BrowserPanel::kMaxWidth, dragStartSize + delta);
        settings.setBrowserWidth (browserWidth);
        resized();
    };
    detailSplitter.onDragStart = [this] { dragStartSize = detailHeight; };
    detailSplitter.onDrag = [this] (int delta) {
        detailHeight = juce::jlimit (ui::DetailPanel::kMinHeight, ui::DetailPanel::kMaxHeight, dragStartSize - delta);
        settings.setDetailHeight (detailHeight);
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
    menuModel.onThemeChosen = [this] (const juce::String& id) {
        settings.setTheme (id);
        ThemeManager::get().setCurrent (id);
    };
    menuModel.recentProjects = [this] { return settings.recentProjects(); };
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

MainComponent::~MainComponent()
{
    stopTimer();
#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
#endif
    ThemeManager::get().removeChangeListener (this);
    engine.getKnownPlugins().removeChangeListener (this);
    model.root.removeListener (this);
    removeKeyListener (router.get());
    editorWindows.clear();
    scannerWindow.reset();
    keyboard.releaseAll();
}

//==============================================================================
juce::String MainComponent::windowTitle() const
{
    const auto name = model.name().isEmpty() ? Strings::windowUntitled : model.name();
    return dirty ? name + juce::String::fromUTF8 (" \xe2\x80\xa2") : name;
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

void MainComponent::markClean()
{
    dirty = false;
    updateTitle();
}

void MainComponent::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property)
{
    if (property != ids::selectedTrack)
        dirty = true;
}

void MainComponent::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int)
{
    dirty = true;
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
    commands.addArray ({ newProject,  openProject,    saveProject,    saveProjectAs, saveTemplate,        versionHistory, exportAudio, showInFinder,
                         scanPlugins, quitApp,        undo,           redo,          cut,                 copy,           paste,       duplicate,
                         deleteItem,  renameItem,         newTrack,       newScene,      captureScene,        newClip,        toggleBrowser, toggleDetail,
                         toggleMixer, switchView,     zoomIn,         zoomOut,       openSettings,        playStop,       toggleComputerKeys, focusSearch });
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
        // File: everything except quit waits for the project files (1.7) and the export (04)
        case newProject: set (M::newProject, fileCategory, false, key ('n', cmd)); break;
        case openProject: set (M::open, fileCategory, false, key ('o', cmd)); break;
        case saveProject: set (M::save, fileCategory, false, key ('s', cmd)); break;
        case saveProjectAs: set (M::saveAs, fileCategory, false, key ('s', cmd | shift)); break;
        case saveTemplate: set (M::saveTemplate, fileCategory, false); break;
        case versionHistory: set (M::versionHistory, fileCategory, false); break;
        case exportAudio: set (M::exportAudio, fileCategory, false, key ('r', cmd | shift)); break;
        case showInFinder: set (M::showInFinder, fileCategory, false); break;
        case scanPlugins: set (M::scanPlugins, fileCategory, true); break;
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
        case cut: set (M::cut, editCategory, editCommands.canCopy(), key ('x', cmd)); break;
        case copy: set (M::copy, editCategory, editCommands.canCopy(), key ('c', cmd)); break;
        case paste: set (M::paste, editCategory, editCommands.canPaste(), key ('v', cmd)); break;
        case duplicate: set (M::duplicate, editCategory, editCommands.canDuplicate(), key ('d', cmd)); break;
        case deleteItem:
            set (M::remove, editCategory, editCommands.canDelete(), juce::KeyPress (juce::KeyPress::deleteKey));
            info.addDefaultKeypress (juce::KeyPress::backspaceKey, juce::ModifierKeys());
            break;
        case renameItem: set (M::rename, editCategory, editCommands.canRename(), key ('r', cmd)); break;

        case newTrack: set (M::newTrack, createCategory, true, key ('t', cmd)); break;
        case newScene: set (M::newScene, createCategory, true, key ('i', cmd)); break;
        case captureScene: set (M::captureScene, createCategory, true, key ('i', cmd | shift)); break;
        case newClip: set (M::newClip, createCategory, model.numTracks() > 0, key ('m', cmd | shift)); break;

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
        case switchView: set (M::switchView, viewCategory, false, juce::KeyPress (juce::KeyPress::tabKey)); break;
        case zoomIn:
            set (M::zoomIn, viewCategory, settings.uiScale() < 1.49, key ('=', cmd));
            info.addDefaultKeypress ('+', juce::ModifierKeys (cmd | shift));
            break;
        case zoomOut: set (M::zoomOut, viewCategory, settings.uiScale() > 0.81, key ('-', cmd)); break;
        case openSettings: set (M::settings, M::view, false, key (',', cmd)); break;

        case playStop: set (M::playStop, M::view, true, juce::KeyPress (juce::KeyPress::spaceKey)); break;
        case toggleComputerKeys:
            set (M::computerKeys, M::view, true, key ('m', 0));
            info.setTicked (computerKeysOn);
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
        case scanPlugins: showPluginScanner(); return true;
        case quitApp: juce::JUCEApplication::getInstance()->systemRequestedQuit(); return true;

        case undo: model.undo.undo(); return true;
        case redo: model.undo.redo(); return true;
        case cut: editCommands.cutSelection(); return true;
        case copy: editCommands.copySelection(); return true;
        case paste: editCommands.pasteSelection(); return true;
        case duplicate: editCommands.duplicateSelection(); return true;
        case deleteItem: editCommands.deleteSelection(); return true;
        case renameItem: session->startRename(); return true;

        case newTrack: editCommands.newTrack(); return true;
        case newScene: editCommands.newScene(); return true;
        case captureScene: editCommands.captureScene(); return true;
        case newClip: editCommands.newClip(); return true;

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
        case toggleComputerKeys:
            computerKeysOn = ! computerKeysOn;
            if (! computerKeysOn)
                keyboard.releaseAll();
            topBar->setComputerKeysOn (computerKeysOn);
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
bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (computerKeysOn && ! ui::textFieldHasFocus())
        return keyboard.handleKeyPressed (key);
    return false;
}

bool MainComponent::keyStateChanged (bool)
{
    return computerKeysOn && ! ui::textFieldHasFocus() && keyboard.handleKeyStateChanged();
}

void MainComponent::focusLost (FocusChangeType)
{
    keyboard.releaseAll();
}

//==============================================================================
void MainComponent::timerCallback()
{
    topBar->tick();
    session->tick();
    if (detailVisible)
        detail->tick();
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

    if (detailVisible)
    {
        const int h = juce::jmin (detailHeight, juce::jmax (ui::DetailPanel::kMinHeight, r.getHeight() - 200));
        detail->setBounds (r.removeFromBottom (h));
    }
    detail->setVisible (detailVisible);
    const int detailTop = r.getBottom();

    if (browserVisible)
        browser->setBounds (r.removeFromLeft (browserWidth));
    browser->setVisible (browserVisible);
    const int browserRight = r.getX();
    session->setBounds (r);

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
                        [safe, deviceId, trackId, mine, instruments, kSave, kFactory, kUser, kPrismaInstrument, kPlugin] (int result) {
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
    editorWindows[deviceId] = std::make_unique<PluginWindow> (editor, processor->getName(), [this, deviceId] {
        // Defer deletion: this runs from the window's own close button.
        juce::MessageManager::callAsync ([this, alive = juce::Component::SafePointer<MainComponent> (this), deviceId] {
            if (alive != nullptr)
                editorWindows.erase (deviceId);
        });
    });
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (engine.getDeviceManager(), 0, 2, 0, 2, true, false, true, false);
    selector->setSize (520, 480);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector);
    options.dialogTitle = Strings::windowAudioSettings;
    options.dialogBackgroundColour = theme().panel;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

void MainComponent::showPluginScanner()
{
    if (scannerWindow != nullptr)
    {
        scannerWindow->toFront (true);
        return;
    }

    auto& props = engine.getSettings().getPropertiesFile();
    auto deadMansPedal = props.getFile().getSiblingFile ("RecentlyCrashedPluginsList");
    auto* list = new juce::PluginListComponent (engine.getFormatManager(), engine.getKnownPlugins(), deadMansPedal, &props, true);
    list->setSize (760, 520);

    struct ScannerWindow final : juce::DocumentWindow
    {
        ScannerWindow (juce::Component* content, std::function<void()> onClose)
            : DocumentWindow (Strings::windowPlugins, theme().panel, DocumentWindow::closeButton), close (std::move (onClose))
        {
            setUsingNativeTitleBar (true);
            setContentOwned (content, true);
            setResizable (true, false);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { close(); }
        std::function<void()> close;
    };

    scannerWindow = std::make_unique<ScannerWindow> (list, [this] {
        juce::MessageManager::callAsync ([this] { scannerWindow.reset(); });
    });
}
