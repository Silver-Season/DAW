#pragma once

#include "Controls.h"

#include "../engine/WarpService.h"
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
    static constexpr int kPropsWidth = 200, kRulerHeight = 30, kWarpLane = 14, kStripHeight = 28, kMargin = 8;
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

    // Warp markers (docs/spec/07-warp.md, task 7.3). Each edit is one undo transaction. The map is the clip's (or a
    // line from its start at its tempo); "materialized" markers are its markers, or the two ends of the file on that
    // line when it has none, which is where the first marker edit starts from.
    daw::engine::WarpMap warpMap() const;
    std::vector<daw::engine::WarpMarker> materializedMarkers() const;
    double fileFrames() const;
    // Pins the audio under `beat` with a marker (and the ends of the file, the first time); returns its index.
    int addWarpMarker (double beat);
    // Moves marker `index` to `beat` (the audio stretches on both sides) or, with moveAudio, gives it the frame that
    // played at `beat` in `before` (the audio under the marker moves). Clamped between its neighbours. No transaction of its own.
    void moveWarpMarker (int index, double beat, bool moveAudio, const daw::engine::WarpMap& before);
    void removeWarpMarker (int index);
    void clearWarpMarkers();
    // The moment at `beat` becomes the nearest bar line (the whole grid of the sound shifts by less than half a bar).
    void setBarOneAt (double beat);
    // The section from marker `index` to the next plays at `bpm`; the markers after it move with it.
    void tempoFromMarker (int index, double bpm);
    void quantizeMarkers();
    void addMarkersAtOnsets();
    // The project takes the tempo of the clip (its average between the markers).
    void matchProjectTempo();
    double clipTempo() const;
    // An arrangement clip is cut into clips at the onsets of its audio or on the grid of the editor.
    int sliceAtOnsets();
    int sliceOnGrid();
    int selectedMarker() const { return selected; }
    void selectMarker (int index);
    // Onsets and beats of the file from the analysis, in content beats (empty until the analysis is there).
    std::vector<double> onsetBeats() const;
    std::vector<double> uncertainBeats() const; // beats of the sound that the current grid misses

    // View state, for the chips, the tests and the screenshot script.
    struct View
    {
        bool snap = true;
        double step = 0.25; // beats; 1/16 of a whole note
    };
    View& view() { return v; }
    double snapBeat (double beat, bool bypass) const;
    juce::String gridText() const;

    // Geometry of the waveform part (its own coordinates): at zoom 1 the whole content fits the width; zoomed in, the
    // width shows viewBeats() / zoom beats from scrollBeats on.
    double viewBeats() const;
    double beatToX (double beat) const;
    double xToBeat (double x) const;
    // The wheel (or a pinch) zooms around x, the horizontal wheel or Shift + wheel scrolls; a new clip shows all again.
    void zoomAround (double x, double factor);
    void scrollBy (double beats);
    double zoomFactor() const { return zoom; }
    double firstVisibleBeat() const { return scrollBeats; }

    class Wave;
    class Props;
    class Strip;
    juce::Component& wave();
    juce::String helpAt (juce::Point<int>) const override { return {}; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    friend class Wave;
    friend class Props;
    friend class Strip;

    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { dirty = true; }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { dirty = true; }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { dirty = true; }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { dirty = true; }
    void valueTreeRedirected (juce::ValueTree&) override { dirty = true; }
    void timerCallback() override { tick(); }

    ClipRef clip() const;
    AudioFileRef file() const;
    std::shared_ptr<const WarpService::Analysis> analysis() const;
    void scaleTempoBy (double factor);
    void setMarkers (std::vector<daw::engine::WarpMarker>, const juce::String& undoName);

    AudioEngine& engine;
    ProjectModel& model;
    ProjectActions& actions;
    int64_t currentClip = 0;
    bool dirty = true;
    int64_t shownPeaks = -1;
    View v;
    double zoom = 1.0, scrollBeats = 0.0;
    double frozenView = 0.0; // the width of the view in beats while a marker is dragged, so it does not rescale under the mouse

    std::unique_ptr<Props> props;
    std::unique_ptr<Wave> waveView;
    std::unique_ptr<Strip> strip;
    int selected = -1;
    bool shownAnalysis = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioClipEditor)
};
} // namespace ui
