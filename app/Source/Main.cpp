#include "DemoProject.h"
#include "MainComponent.h"
#include "Settings.h"
#include "engine/AudioEngine.h"
#include "engine/NullAudioDriver.h"
#include "ui/ThemeLookAndFeel.h"

class DawApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "DAW"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        settings = std::make_unique<Settings>();

        // Themes: the built-in ones plus User Library/Themes/*.json. DAW_THEME overrides the saved choice (screenshots).
        const auto userThemes = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Prisma DAW/User Library/Themes");
        for (const auto& problem : ThemeManager::get().reload (userThemes))
            juce::ignoreUnused (problem);
        const auto themeOverride = juce::SystemStats::getEnvironmentVariable ("DAW_THEME", {});
        ThemeManager::get().setCurrent (themeOverride.isNotEmpty() ? themeOverride : settings->theme());
        lookAndFeel = std::make_unique<ThemeLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());
        juce::Desktop::getInstance().setGlobalScaleFactor ((float) settings->uiScale());

        // The demo project of phase 0; real project files come with task 1.7.
        model.reset (new ProjectModel (ProjectModel::createEmpty()));
        populateDemoProject (*model);

        const bool nullAudio = juce::SystemStats::getEnvironmentVariable ("DAW_NULL_AUDIO", {}).isNotEmpty();
        engine = std::make_unique<AudioEngine> (*settings, ! nullAudio);
        engine->setModel (model.get());
        if (nullAudio)
            nullDriver = std::make_unique<NullAudioDriver> (*engine); // no sound card: tick the engine from a timer thread
        mainWindow = std::make_unique<MainWindow> (*engine, *model, *settings);
    }

    void shutdown() override
    {
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
        if (mainWindow != nullptr)
            settings->setWindowState (mainWindow->getWindowStateAsString());
        quit();
    }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow (AudioEngine& engine, ProjectModel& model, Settings& s)
            : DocumentWindow ("DAW", theme().background, DocumentWindow::allButtons), settings (s)
        {
            setUsingNativeTitleBar (true);
            auto* content = new MainComponent (engine, model);
            content->onTitleChanged = [this] (const juce::String& title) { setName (title); };
            setName (content->windowTitle());
            setContentOwned (content, true);
            setResizable (true, true);
            setResizeLimits (1100, 700, 8000, 6000);
            if (const auto state = settings.windowState(); state.isNotEmpty())
                restoreWindowStateFromString (state);
            else
                centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

    private:
        Settings& settings;
    };

    std::unique_ptr<Settings> settings;
    std::unique_ptr<ThemeLookAndFeel> lookAndFeel;
    std::unique_ptr<ProjectModel> model;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<NullAudioDriver> nullDriver;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (DawApplication)
