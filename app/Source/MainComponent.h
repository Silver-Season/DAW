#pragma once

#include "../../plugins/prisma/Source/PrismaEditor.h"
#include "engine/AudioEngine.h"
#include "model/ProjectModel.h"

class PluginWindow;

// Main window content: transport bar, Session View grid, mixer and an on-screen keyboard.
// It reads and writes only the ProjectModel; sound comes from the engine that listens to the model.
class MainComponent final : public juce::Component, private juce::Timer, private juce::ValueTree::Listener
{
public:
    MainComponent (AudioEngine&, ProjectModel&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool) override { return keyboard.handleKeyStateChanged(); }
    void focusLost (FocusChangeType) override { keyboard.releaseAll(); }

private:
    class TransportBar;
    class SessionGrid;
    class MixerStrip;
    class MasterStrip;

    void timerCallback() override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&) override { structureChanged (parent); }
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int) override;

    void structureChanged (const juce::ValueTree& parent);
    void rebuildStrips();
    void showInstrumentMenu (int64_t trackId);
    void setPrismaInstrument (int64_t trackId, const juce::String& presetName);
    void removeInstrument (int64_t trackId);
    void loadPluginInstrument (int64_t trackId, const juce::PluginDescription&);
    void openInstrumentEditor (int64_t deviceId);
    void closeInstrumentEditor (int64_t deviceId);
    void closeEditorsOfMissingDevices();
    void showAudioSettings();
    void showPluginScanner();

    AudioEngine& engine;
    ProjectModel& model;
    prisma_ui::PrismaLookAndFeel lnf;

    std::unique_ptr<TransportBar> transportBar;
    std::unique_ptr<SessionGrid> sessionGrid;
    std::vector<std::unique_ptr<MixerStrip>> strips;
    std::unique_ptr<MasterStrip> masterStrip;
    prisma_ui::KeyboardPanel keyboard;

    std::map<int64_t, std::unique_ptr<PluginWindow>> editorWindows; // by device id
    std::unique_ptr<juce::DocumentWindow> scannerWindow;
    bool layoutDirty = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
