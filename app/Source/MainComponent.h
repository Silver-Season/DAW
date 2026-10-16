#pragma once

#include "AudioEngine.h"
#include "../../plugins/prisma/Source/PrismaEditor.h"

class PluginWindow;

// Main window content: transport bar, Session View grid, mixer and an on-screen keyboard.
class MainComponent final : public juce::Component, private juce::Timer
{
public:
    explicit MainComponent (AudioEngine&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool) override { return keyboard.handleKeyStateChanged(); }
    void focusLost (FocusChangeType) override { keyboard.releaseAll(); }

    void populateDemoSession();

private:
    class TransportBar;
    class SessionGrid;
    class MixerStrip;
    class MasterStrip;

    void timerCallback() override;
    void showInstrumentMenu (int track);
    void openInstrumentEditor (int track);
    void closeInstrumentEditor (int track);
    void setBuiltInInstrument (int track, int preset);
    void showAudioSettings();
    void showPluginScanner();

    AudioEngine& engine;
    prisma_ui::PrismaLookAndFeel lnf;

    std::unique_ptr<TransportBar> transportBar;
    std::unique_ptr<SessionGrid> sessionGrid;
    std::vector<std::unique_ptr<MixerStrip>> strips;
    std::unique_ptr<MasterStrip> masterStrip;
    prisma_ui::KeyboardPanel keyboard;

    std::array<std::unique_ptr<PluginWindow>, AudioEngine::kNumTracks> editorWindows;
    std::unique_ptr<juce::DocumentWindow> scannerWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
