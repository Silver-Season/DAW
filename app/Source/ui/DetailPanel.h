#pragma once

#include "AudioClipEditor.h"
#include "Controls.h"
#include "PianoRoll.h"
#include "Selection.h"

#include "../browser/BrowserActions.h"
#include "../model/ProjectActions.h"

class AudioEngine;

namespace ui
{
class DeviceChainView;
class RecordSourceSwitch;

// The bottom panel (232 px): a summary of the selected track on the left and its device chain on the right.
// In clip-editor mode (double-click on a clip, Shift+Tab) a selected clip shows the piano roll in their place.
class DetailPanel final : public juce::Component
{
public:
    static constexpr int kMinHeight = 176, kMaxHeight = 480, kDefaultHeight = 232;
    static constexpr int kInfoWidth = 236;

    DetailPanel (AudioEngine&, ProjectModel&, ProjectActions&, SelectionState&);
    ~DetailPanel() override;

    // 30 Hz while visible: follows the selected track, refreshes the knobs from the plugin parameters.
    void tick();

    // The "open in window" button of a device.
    void setOpenDeviceHandler (std::function<void (int64_t deviceId)> handler);
    DeviceChainView& chain() { return *chainView; }

    // Clip editor mode: the piano roll replaces the track summary and the chain while a clip is selected.
    void setEditorMode (bool on);
    bool editorMode() const { return editorOn; }
    bool isEditorShown() const { return editorShown; }
    PianoRoll& pianoRoll() { return *editor; }
    // An audio clip is edited by this instead of the piano roll.
    AudioClipEditor& audioEditor() { return *audioClipEditor; }
    bool isAudioEditorShown() const { return editorShown && audioShown; }
    // Fired when the editor appears or goes away, so the window can switch between the two panel heights.
    std::function<void()> onEditorShownChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    void updateEditor();
    // The rows of the summary card that open a menu (input, monitoring) and the input meter of an audio track.
    juce::Rectangle<int> cardRow (int index) const;
    juce::Rectangle<int> meterBounds() const;

    AudioEngine& engine;
    ProjectModel& model;
    ProjectActions& actions;
    SelectionState& selection;
    std::unique_ptr<DeviceChainView> chainView;
    std::unique_ptr<RecordSourceSwitch> recordSwitch;
    std::unique_ptr<PianoRoll> editor;
    std::unique_ptr<AudioClipEditor> audioClipEditor;
    NumberField transposeField;
    bool editorOn = false, editorShown = false, audioShown = false;
    int64_t shownTrack = -1;
    juce::String shownSummary;
    float inputLevel = 0.0f; // shown peak of the input, falls off between ticks
    bool inputClipShown = false;
};

// Devices of one track, left to right, with arrows between them and a drop zone at the end.
class DeviceChainView final : public juce::Component, private juce::ValueTree::Listener
{
public:
    DeviceChainView (AudioEngine&, ProjectModel&, ProjectActions&, SelectionState&);
    ~DeviceChainView() override;

    void setTrack (int64_t trackId);
    int64_t track() const { return trackId; }
    void tick();

    std::function<void (int64_t deviceId)> onOpenDevice;
    // The preset button (the triangle) of a device: asks for the menu at a screen position.
    std::function<void (int64_t deviceId, juce::Point<int> screenPosition)> onPresetMenu;

    // Drops from the browser: the chain is a drop target when these are set. `onDropped` hears every
    // drop (with the result) so that the window can report errors and note the recent item.
    void setBrowserActions (browser::BrowserActions* a) { dropActions = a; }
    std::function<void (const browser::DropResult&, const browser::DragItem&)> onDropped;

    // Number of device views currently shown; tests and the screenshot script read it.
    int numDeviceViews() const;

    void resized() override;

private:
    class Content;
    class DeviceView;
    friend class DeviceView;

    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { dirty = true; }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { dirty = true; }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { dirty = true; }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { dirty = true; }
    void valueTreeRedirected (juce::ValueTree&) override { dirty = true; }

    void rebuild();

    AudioEngine& engine;
    ProjectModel& model;
    ProjectActions& actions;
    SelectionState& selection;
    juce::Viewport viewport;
    std::unique_ptr<Content> content;
    int64_t trackId = -1;
    bool dirty = true;
    browser::BrowserActions* dropActions = nullptr;
};
} // namespace ui
