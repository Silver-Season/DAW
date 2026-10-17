#pragma once

#include "Controls.h"

#include "../model/ProjectActions.h"

class AudioEngine;

namespace ui
{
// The editor of an audio clip (docs/spec/05-audio-recording.md, tasks 5.1, 5.2 and 5.6): the properties of the clip on
// the left (name, file, gain, pitch, reverse, loop, fades) and the waveform of its content with a beat ruler on the
// right. The ruler carries the start marker and the loop brace, which are dragged with the mouse. It replaces the
// piano roll in the detail panel while an audio clip is selected. Every change is one undo transaction, a mouse gesture
// included; the engine hears the edits through the model.
class AudioClipEditor final : public juce::Component,
                              public HelpProvider,
                              private juce::ValueTree::Listener,
                              private juce::Timer
{
public:
    static constexpr int kPropsWidth = 200, kRulerHeight = 30, kMargin = 8;
    static constexpr double kMinBraceBeats = 1.0 / 64.0;

    AudioClipEditor (AudioEngine&, ProjectModel&, ProjectActions&);
    ~AudioClipEditor() override;

    // The clip to edit, by id; 0 or a clip that does not exist shows an empty page.
    void setClip (int64_t clipId);
    int64_t clipId() const { return currentClip; }
    bool hasClip() const;

    // 30 Hz while shown: applies model changes (and a file that is still loading) to the widgets.
    void tick();

    // The edits of the properties; each one is one undo transaction with a name from Strings::audioUi.
    void setGainDb (double db);
    void setTranspose (int semitones);
    void setDetune (int cents);
    void setReverse (bool on);
    void setLoopEnabled (bool on);
    void setFadeIn (double beats);
    void setFadeOut (double beats);

    // View state, for the chips, the tests and the screenshot script.
    struct View
    {
        bool snap = true;
        double step = 0.25; // beats; 1/16 of a whole note
    };
    View& view() { return v; }
    double snapBeat (double beat, bool bypass) const;
    juce::String gridText() const;

    // Geometry of the waveform part (its own coordinates): the whole content fits the width.
    double viewBeats() const;
    double beatToX (double beat) const;
    double xToBeat (double x) const;

    class Wave;
    class Props;
    juce::Component& wave();
    juce::String helpAt (juce::Point<int>) const override { return {}; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    friend class Wave;
    friend class Props;

    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { dirty = true; }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { dirty = true; }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { dirty = true; }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { dirty = true; }
    void valueTreeRedirected (juce::ValueTree&) override { dirty = true; }
    void timerCallback() override { tick(); }

    ClipRef clip() const;
    AudioFileRef file() const;

    AudioEngine& engine;
    ProjectModel& model;
    ProjectActions& actions;
    int64_t currentClip = 0;
    bool dirty = true;
    int64_t shownPeaks = -1;
    View v;
    double frozenView = 0.0; // the width of the view in beats while a marker is dragged, so it does not rescale under the mouse

    std::unique_ptr<Props> props;
    std::unique_ptr<Wave> waveView;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioClipEditor)
};
} // namespace ui
