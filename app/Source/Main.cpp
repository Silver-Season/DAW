#include "AudioEngine.h"
#include "MainComponent.h"

class DawApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "DAW"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        engine = std::make_unique<AudioEngine>();
        mainWindow = std::make_unique<MainWindow> (*engine);
    }

    void shutdown() override
    {
        mainWindow.reset();
        engine.reset();
    }

    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (AudioEngine& engine)
            : DocumentWindow ("DAW", juce::Colour (0xff121419), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            auto* content = new MainComponent (engine);
            content->populateDemoSession();
            setContentOwned (content, true);
            setResizable (true, true);
            setResizeLimits (1000, 700, 4000, 3000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (DawApplication)
