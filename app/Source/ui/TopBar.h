#pragma once

#include "Controls.h"

class AudioEngine;
class ProjectModel;

namespace ui
{
// The 52 px bar on top of the window: logo, tempo, transport, version, CPU. Elements that belong to later
// stages (metronome, record, loop, "10 min" ribbon, morph, Arrangement) are drawn inactive so that the
// layout does not move when they arrive.
class TopBar final : public juce::Component
{
public:
    static constexpr int kHeight = 52;

    TopBar (AudioEngine&, ProjectModel&);
    ~TopBar() override;

    // Called by the window's 30 Hz timer: refreshes the position, tempo, CPU and the lamps.
    void tick();
    void setComputerKeysOn (bool on);

    std::function<void()> onCpuClicked;
    // The version button: shows "v14 · 5 мин" and opens the history.
    std::function<void()> onVersionClicked;
    void setVersionText (const juce::String&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class IconButton;
    class Lamp;
    class CpuMeter;
    class FakeSlider;
    class Tabs;

    AudioEngine& engine;
    ProjectModel& model;

    NumberField tempo;
    std::unique_ptr<IconButton> metronome, play, stop, record, loop, capture, version;
    std::unique_ptr<FakeSlider> morph;
    std::unique_ptr<Tabs> tabs;
    std::unique_ptr<Lamp> midiLamp, keysLamp;
    std::unique_ptr<CpuMeter> cpu;

    juce::Rectangle<int> logoArea, signatureArea, positionArea, ribbonArea;
    juce::String positionText;
    uint32_t lastMidiCount = 0;
    juce::uint32 midiFlashUntil = 0;
    bool wasPlaying = false;
};
} // namespace ui
