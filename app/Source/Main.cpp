#include "../../plugins/prisma/Source/PrismaProcessor.h"
#include "DemoProject.h"
#include "MainComponent.h"
#include "Settings.h"
#include "engine/AudioEngine.h"
#include "engine/NullAudioDriver.h"
#include "library/UserLibrary.h"
#include "model/ArrangementEdit.h"
#include "model/ProjectActions.h"
#include "project/ProjectFile.h"
#include "ui/ExportWindow.h"
#include "ui/ThemeLookAndFeel.h"

class DawApplication final
  : public juce::JUCEApplication
  , private juce::Timer
{
public:
    const juce::String getApplicationName() override { return "DAW"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    // Snapshot runs may happen next to a running copy of the app.
    bool moreThanOneInstanceAllowed() override
    {
        return juce::SystemStats::getEnvironmentVariable ("DAW_UI_SNAPSHOTS", {}).isNotEmpty ();
    }

    void initialise (const juce::String& commandLine) override
    {
        const auto output = juce::SystemStats::getEnvironmentVariable ("DAW_UI_SNAPSHOTS", {});
        if (output.isNotEmpty ())
        {
            snapshotDirectory = juce::File (output);
            if (snapshotDirectory.createDirectory ().failed ())
            {
                setApplicationReturnValue (1);
                quit ();
                return;
            }
            settings = std::make_unique<Settings> (snapshotDirectory.getChildFile ("snapshot.settings"));
            settings->setLibraryPath (snapshotDirectory.getChildFile ("Library").getFullPathName ());
            settings->setWindowState ({});
            settings->setUiScale (1.0);
        }
        else
            settings = std::make_unique<Settings> ();

        // Themes: the built-in ones plus User Library/Themes/*.json. DAW_THEME overrides the saved choice (screenshots).
        const auto userThemes = (settings->libraryPath().isNotEmpty() ? juce::File (settings->libraryPath()) : library::UserLibrary::defaultRoot()).getChildFile ("Themes");
        for (const auto& problem : ThemeManager::get().reload (userThemes))
            juce::ignoreUnused (problem);
        const auto themeOverride = juce::SystemStats::getEnvironmentVariable ("DAW_THEME", {});
        Appearance appearance;
        if (Appearance::fromJson (settings->appearance (), appearance))
            ThemeManager::get ().setAppearance (appearance);
        ThemeManager::get().setCurrent (themeOverride.isNotEmpty() ? themeOverride : settings->theme());
        lookAndFeel = std::make_unique<ThemeLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());
        juce::Desktop::getInstance().setGlobalScaleFactor ((float) settings->uiScale());

        // The window's startup code fills the model: recovery, the first-run demo or a new project.
        model.reset (new ProjectModel (ProjectModel::createEmpty()));

        if (snapshotDirectory != juce::File ())
        {
            populateDemoProject (*model);
            {
                // more scenes with clips, so the captures show clips passing under the glass mixer
                ProjectActions actions (*model);
                for (int scene = 0; scene < 3; ++scene)
                    actions.duplicateScene (scene * 2);
                for (const int scene : { 5, 3, 4, 1 })
                    actions.duplicateScene (scene);
            }
            for (const auto& track : model->tracks ())
                for (int scene = 0; scene < juce::jmin (3, model->numScenes ()); ++scene)
                    if (const auto clip = track.clipAt (scene); clip.isValid ())
                        arrangement::addSlotClip (*model, track, clip, (double)scene * 16.0);
            model->undo.clearUndoHistory ();
        }

        const bool nullAudio = snapshotDirectory != juce::File () ||
                               juce::SystemStats::getEnvironmentVariable ("DAW_NULL_AUDIO", {}).isNotEmpty ();
        engine = std::make_unique<AudioEngine> (*settings, ! nullAudio);
        engine->setModel (model.get());
        if (nullAudio)
            nullDriver = std::make_unique<NullAudioDriver> (*engine); // no sound card: tick the engine from a timer thread
        mainWindow = std::make_unique<MainWindow> (*engine, *model, *settings);
        if (snapshotDirectory == juce::File ())
            mainWindow->startup (project::projectFileFromCommandLine (commandLine));
        else
        {
            engine->rebuildGraph ();
            mainWindow->getContentComponent ()->setSize (1440, 940);
            startTimer (180);
        }
    }

    // A second launch with a file, or a .prisma opened in Finder while the app runs (macOS hands documents over this way).
    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        if (mainWindow != nullptr)
            mainWindow->openFromSystem (project::projectFileFromCommandLine (commandLine));
    }

    void shutdown() override
    {
        stopTimer ();
        snapshotWindow.reset ();
        mainWindow.reset();
        nullDriver.reset();
        engine.reset(); // detaches from the model first
        model.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        lookAndFeel.reset();
        settings.reset();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow == nullptr)
        {
            quit();
            return;
        }
        settings->setWindowState (mainWindow->getWindowStateAsString());
        mainWindow->requestQuit ([] { juce::JUCEApplication::getInstance ()->quit (); });
    }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
      MainWindow (AudioEngine& audio, ProjectModel& project, Settings& s)
        : DocumentWindow ("DAW", theme ().background, DocumentWindow::allButtons)
        , settings (s)
      {
          setUsingNativeTitleBar (true);
          content = new MainComponent (audio, project);
          content->onTitleChanged = [this] (const juce::String& title) { setName (title); };
          setName (content->windowTitle ());
          setContentOwned (content, true);
          content->attachKeyListeners (*this);
          setResizable (true, true);
          setResizeLimits (1210, 700, 8000, 6000);
          if (const auto state = settings.windowState (); state.isNotEmpty ())
              restoreWindowStateFromString (state);
          else
              centreWithSize (getWidth (), getHeight ());
          setVisible (true);
      }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
        void activeWindowStatusChanged() override
        {
            // Give the keyboard to the content when the window comes back without a focused component in it.
            if (isActiveWindow() && content != nullptr && ! content->hasKeyboardFocus (true))
                content->grabKeyboardFocus();
        }
        void startup (const juce::File& file) { content->startup (file); }
        void openFromSystem (const juce::File& file) { content->openFromSystem (file); }
        void requestQuit (std::function<void()> quitNow) { content->requestQuit (std::move (quitNow)); }

    private:
        Settings& settings;
        MainComponent* content = nullptr; // owned by the window
    };

    // Opt-in native component captures. Uses its own settings and null audio; no user's project is opened.
    void timerCallback () override
    {
        static const char* themes[]{ "prism", "neon", "studio", "paper", "glass-light", "glass-dark" };
        auto* content = dynamic_cast<MainComponent*> (mainWindow->getContentComponent ());
        if (snapshotStep >= 56)
        {
            stopTimer ();
            quit ();
            return;
        }
        const int capture = snapshotStep / 2;
        if (snapshotStep % 2 == 0)
        {
            snapshotWindow.reset ();
            if (capture < 18)
            {
                ThemeManager::get ().setCurrent (themes[capture / 3]);
                if (content->isArrangementShown ())
                    content->getCommandManager ().invokeDirectly (ui::commands::switchView, false);
                content->getDetailPanel ().setEditorMode (false);
                if (capture % 3 == 1)
                    content->openClipEditor (model->trackAt (0).id (), 0);
                if (capture % 3 == 2)
                    content->getCommandManager ().invokeDirectly (ui::commands::switchView, false);
            }
            else
            {
                ThemeManager::get ().setCurrent (capture % 2 == 0 ? "glass-light" : "glass-dark");
                lookAndFeel->refresh (); // the theme's change message arrives later; new windows need its colours now
                if (capture >= 26)
                {
                    snapshotWindow.reset ();
                    content->showSessionView ();
                    content->getDetailPanel ().setEditorMode (false);
                    model->setSelectedTrack (model->trackAt (0).id ()); // the drum track: its pads in the bottom panel
                }
                else if (capture >= 24)
                    snapshotWindow = std::make_unique<ui::ExportWindow> (*settings, *model, *engine, snapshotDirectory, "Afterlight", [] {});
                else if (capture >= 22)
                    content->showMixView ();
                else if (capture < 20)
                {
                    auto window =
                      std::make_unique<ui::SettingsWindow> (*engine, *settings, ui::SettingsWindow::Hooks ());
                    window->showTab (ui::SettingsWindow::appearance);
                    snapshotWindow = std::move (window);
                }
            }
        }
        else
        {
            const auto save = [this] (juce::Component& component, const juce::String& name)
            {
                const auto scale = juce::jlimit (1.0f, 3.0f,
                  juce::SystemStats::getEnvironmentVariable ("DAW_UI_SNAPSHOT_SCALE", "1").getFloatValue ());
                auto image = component.createComponentSnapshot (component.getLocalBounds (), true, scale);
                const auto file = snapshotDirectory.getChildFile (name + ".png");
                file.deleteFile ();
                juce::FileOutputStream stream (file);
                if (!stream.openedOk () || !juce::PNGImageFormat ().writeImageToStream (image, stream))
                    setApplicationReturnValue (1);
            };
            if (capture < 18)
            {
                const char* views[]{ "session", "midi", "arrangement" };
                save (*content, juce::String (themes[capture / 3]) + "-" + views[capture % 3]);
            }
            else if (capture < 20)
                save (*snapshotWindow->getContentComponent (), theme ().id + "-appearance");
            else if (capture >= 26)
                save (*content, theme ().id + "-drums");
            else if (capture >= 24)
                save (*snapshotWindow->getContentComponent (), theme ().id + "-export");
            else if (capture >= 22)
                save (*content, theme ().id + "-mix");
            else
            {
                PrismaProcessor processor;
                std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor ());
                editor->setLookAndFeel (lookAndFeel.get ());
                save (*editor, theme ().id + "-synth");
                editor->setLookAndFeel (nullptr);
            }
        }
        ++snapshotStep;
    }

    juce::File snapshotDirectory;
    std::unique_ptr<juce::DocumentWindow> snapshotWindow;
    int snapshotStep = 0;

    std::unique_ptr<Settings> settings;
    std::unique_ptr<ThemeLookAndFeel> lookAndFeel;
    std::unique_ptr<ProjectModel> model;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<NullAudioDriver> nullDriver;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (DawApplication)
