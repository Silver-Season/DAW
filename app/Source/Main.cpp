#include "DemoProject.h"
#include "MainComponent.h"
#include "Settings.h"
#include "engine/AudioEngine.h"

class DawApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "DAW"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        settings = std::make_unique<Settings>();

        // The demo project of phase 0; real project files come with task 1.7.
        model.reset (new ProjectModel (ProjectModel::createEmpty()));
        populateDemoProject (*model);

        engine = std::make_unique<AudioEngine> (*settings);
        engine->setModel (model.get());
        mainWindow = std::make_unique<MainWindow> (*engine, *model);
    }

    void shutdown() override
    {
        mainWindow.reset();
        engine.reset(); // detaches from the model first
        model.reset();
        settings.reset();
    }

    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow (AudioEngine& engine, ProjectModel& model)
            : DocumentWindow ("DAW", juce::Colour (0xff121419), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (engine, model), true);
            setResizable (true, true);
            setResizeLimits (1000, 700, 4000, 3000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    std::unique_ptr<Settings> settings;
    std::unique_ptr<ProjectModel> model;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (DawApplication)
