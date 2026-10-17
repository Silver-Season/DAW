#pragma once

#include "Controls.h"

class AudioEngine;
class ProjectModel;

namespace ui
{
// The 52 px bar on top of the window: logo, tempo, time signature, project scale, transport, version, CPU. Elements
// that belong to later stages (metronome, record, loop, "10 min" ribbon, morph) are drawn inactive so that the
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
    // The octave of the A key, shown in the KLAV lamp ("КЛАВ C3").
    void setKeysOctave (const juce::String& octave);

    std::function<void()> onCpuClicked;
    // The version button: shows "v14 · 5 мин" and opens the history.
    std::function<void()> onVersionClicked;
    void setVersionText (const juce::String&);

    // The Session / Arrangement tabs. The window switches the views; this only draws which one is shown.
    std::function<void (bool arrangement)> onViewSwitched;
    void setArrangementShown (bool);

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
    std::unique_ptr<IconButton> metronome, play, stop, record, loop, capture, version, scaleButton;
    std::unique_ptr<FakeSlider> morph;
    std::unique_ptr<Tabs> tabs;
    std::unique_ptr<Lamp> midiLamp, keysLamp;
    std::unique_ptr<CpuMeter> cpu;

    juce::Rectangle<int> logoArea, signatureArea, positionArea, ribbonArea;
    juce::String scaleText;
    juce::String positionText;
    uint32_t lastMidiCount = 0;
    juce::uint32 midiFlashUntil = 0;
    bool wasPlaying = false;
};
} // namespace ui
