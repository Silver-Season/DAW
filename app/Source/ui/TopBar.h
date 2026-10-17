#pragma once

#include "Controls.h"

#include <vector>

class AudioEngine;
class ProjectModel;
class SessionRecorder;

namespace ui
{
// The 52 px bar on top of the window: logo, tempo, time signature, project scale, metronome, transport with record
// and overdub, the loop of the arrangement, follow-the-playhead and "Back to arrangement", the "10 min" capture ribbon
// and its button, version, CPU. The morph slider belongs to a later stage and is drawn inactive.
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

    // The recorder gives the ribbon its notes and the record lamp its state; the window decides what the buttons do
    // (it knows the selection): record = Session Record from the selected scene, capture = the last phrase.
    void setRecorder (SessionRecorder*);
    std::function<void()> onRecordClicked;
    // The items of the record button's menu (right mouse button) that choose where the recording goes (task 3.4):
    // the Arrangement timeline (F9) or a slot of Session View.
    std::function<void()> onRecordToArrangement, onRecordToSlot;
    std::function<void()> onCaptureClicked;

    std::function<void()> onCpuClicked;
    // The version button: shows "v14 · 5 мин" and opens the history.
    std::function<void()> onVersionClicked;
    void setVersionText (const juce::String&);

    // The Session / Arrangement tabs. The window switches the views; this only draws which one is shown.
    std::function<void (bool arrangement)> onViewSwitched;
    void setArrangementShown (bool);

    void paint (juce::Graphics&) override;
    void resized() override;

    // The strip of the ribbon, for tests and the screenshot script (empty when the window is too narrow).
    juce::Rectangle<int> ribbonStrip() const { return ribbonStripArea; }

private:
    class IconButton;
    class Lamp;
    class CpuMeter;
    class FakeSlider;
    class Tabs;

    AudioEngine& engine;
    ProjectModel& model;

    NumberField tempo;
    std::unique_ptr<IconButton> metronome, play, stop, record, overdub, loop, punchIn, punchOut, follow, backToArrangement, capture, version, scaleButton;
    std::unique_ptr<FakeSlider> morph;
    std::unique_ptr<Tabs> tabs;
    std::unique_ptr<Lamp> midiLamp, keysLamp;
    std::unique_ptr<CpuMeter> cpu;

    juce::Rectangle<int> logoArea, signatureArea, positionArea, ribbonArea, ribbonStripArea;
    bool compact = false;
    void paintRibbon (juce::Graphics&);
    void updateRibbon();
    juce::String scaleText;
    juce::String positionText;
    uint32_t lastMidiCount = 0;
    juce::uint32 midiFlashUntil = 0;
    bool wasPlaying = false;

    SessionRecorder* recorder = nullptr;
    std::vector<int64_t> markTracks; // the ribbon's note-ons: engine sample, velocity and track, copied when the capture buffer changed
    std::vector<int64_t> markSamples;
    std::vector<int> markVelocities;
    uint32_t ribbonVersion = 0;
    int64_t ribbonPixel = -1;
    int tickCounter = 0;
    bool recordLit = false;
};
} // namespace ui
