#pragma once

#include "Controls.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <optional>
#include <vector>

class BuiltInEffectProcessor;

namespace ui
{
// The faces of the built-in effects (docs/spec/06-effects-mixing.md, task 6.1): one compact view per effect type for the
// device card, with its own picture (EQ curve, transfer curve and gain reduction, decay, echoes, filter response, LFO
// shape), and an expanded form that shows every parameter in groups. The same view fills the effect's own window.
//
// Every control is bound to one plugin parameter of the BuiltInEffectProcessor: a gesture is wrapped in
// beginChangeGesture / endChangeGesture, and tick() reads the values back (30 Hz), so automation and the window stay in step.

// A control bound to one parameter.
class ParamControl : public juce::Component
{
public:
    explicit ParamControl (juce::RangedAudioParameter& p) : param (p) {}

    juce::RangedAudioParameter& parameter() const { return param; }
    const juce::String& paramId() const { return param.paramID; }
    // Reads the parameter back; called at 30 Hz.
    virtual void refresh() = 0;
    // Width it needs in a row of the compact view (choices and switches), 0 = takes what it gets.
    virtual int idealWidth() const { return 0; }

protected:
    // One gesture: begin, set, end.
    void setNormalised (float normalised);
    int choiceIndex() const;

    juce::RangedAudioParameter& param;
};

// A knob with a caption under it; the caption shows the value while the mouse is over it or it is dragged.
class ParamKnob final : public ParamControl
{
public:
    ParamKnob (juce::RangedAudioParameter&, const juce::String& caption, juce::Colour ring);
    void refresh() override;
    void resized() override { knob.setBounds (getLocalBounds()); }
    Knob& getKnob() { return knob; }

private:
    Knob knob;
};

// A choice parameter as a dropdown: the current value and a chevron; a click opens the list.
class ChoiceBox final : public ParamControl
{
public:
    ChoiceBox (juce::RangedAudioParameter&, const juce::String& label);
    void refresh() override;
    int idealWidth() const override;
    // What a pick from the list does (also for tests).
    void choose (int index);
    int selectedIndex() const { return shown; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    juce::String label;
    int shown = -1;
};

// A choice of a few short values as one row of segments (voices 1 2 3 4).
class SegmentedChoice final : public ParamControl
{
public:
    SegmentedChoice (juce::RangedAudioParameter&, const juce::String& label);
    void refresh() override;
    int idealWidth() const override;
    void choose (int index);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> segment (int index) const;
    int labelWidth() const;
    juce::String label;
    juce::StringArray items;
    int shown = -1;
};

// An Off/On parameter as a switch with a lamp and a label.
class ParamToggle final : public ParamControl
{
public:
    ParamToggle (juce::RangedAudioParameter&, const juce::String& label);
    void refresh() override;
    int idealWidth() const override;
    bool isOn() const { return on; }
    void toggle();
    void setLampColour (juce::Colour c) { lamp = c; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { toggle(); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    juce::String label;
    std::optional<juce::Colour> lamp;
    bool on = false;
};

// A continuous parameter as a small box with a caption and the value: drag up/down (Shift: x0.1), wheel, double-click
// returns the default. Takes less room than a knob: the EQ band row and the delay times.
class ValueBox final : public ParamControl
{
public:
    ValueBox (juce::RangedAudioParameter&, const juce::String& caption);
    void refresh() override;
    int idealWidth() const override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    void change (float normalised);
    juce::String caption;
    float value = 0.0f;
    bool gestureOpen = false;
    int lastY = 0;
};

//==============================================================================
class EffectView final : public juce::Component
{
public:
    // Insets of the card body around the view.
    static constexpr int kKnobWidth = 62, kKnobHeight = 54, kRowHeight = 22, kGap = 8;

    EffectView (BuiltInEffectProcessor&, juce::Colour accent, bool expanded);
    ~EffectView() override;

    const juce::String& type() const { return effectType; }
    bool isExpanded() const { return expanded; }
    // The width this view wants at the given height (the expanded form wraps its groups into columns).
    int preferredWidth (int height) const;
    // A short live note for the device header: gain reduction, number of bands, decay time.
    juce::String headerNote() const;

    void tick();

    // The control of a parameter, or null if this form does not show it right now (tests, screenshots).
    ParamControl* controlFor (const juce::String& paramId) const;
    // Every parameter has a control in the expanded form; the compact one shows the main ones.
    int numControls() const { return (int) controls.size(); }
    // The parameter ids that have a control in this form, shown or waiting for their condition (sync on/off).
    juce::StringArray controlIds() const
    {
        juce::StringArray ids;
        for (const auto& c : controls)
            ids.addIfNotAlreadyThere (c->paramId());
        return ids;
    }

    // The EQ band whose controls the compact EQ shows (0-based).
    int selectedBand() const { return band; }
    void selectBand (int b);

    void paint (juce::Graphics&) override;
    void resized() override;

    // Used by the pictures.
    BuiltInEffectProcessor& processor() const { return proc; }
    juce::RangedAudioParameter* param (const char* id) const;
    float value (const char* id) const;     // in the parameter's units (choice: index)
    juce::Colour accentColour() const { return accent; }

    struct Item;
    struct Group;
    struct Spec;
    class Display;

private:
    struct Placed;
    void build();
    ParamControl* make (const Item&);
    bool shown (const Item&) const;
    bool dimmed (const Item&) const;
    void layoutCompact (juce::Rectangle<int> area);
    int layoutExpanded (juce::Rectangle<int> area, bool apply) const;
    juce::String conditionState() const;

    BuiltInEffectProcessor& proc;
    juce::String effectType;
    juce::Colour accent;
    bool expanded;
    int band = 2;
    std::unique_ptr<Spec> spec;
    std::unique_ptr<Display> display;
    std::vector<std::unique_ptr<ParamControl>> controls;
    std::vector<Placed> placed;   // which item each control came from
    juce::String lastConditions;
    // Group titles and the lines between groups of the expanded form, written by its layout.
    mutable std::vector<std::pair<juce::Rectangle<int>, juce::String>> titles;
    mutable std::vector<int> separators;
};

// The window of a built-in effect: the expanded view, themed, instead of a generic list of sliders.
class EffectEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EffectEditor (BuiltInEffectProcessor&);
    ~EffectEditor() override;

    EffectView& view() { return effectView; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { effectView.tick(); }
    EffectView effectView;
};
} // namespace ui
