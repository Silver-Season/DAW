#pragma once

#include "PrismaProcessor.h"

namespace prisma_ui
{

// Shared colours for the Prisma editor and the DAW.
namespace colours
{
    const juce::Colour background { 0xff121419 };
    const juce::Colour panel { 0xff1c2027 };
    const juce::Colour panelEdge { 0xff2a303a };
    const juce::Colour text { 0xffd8dee9 };
    const juce::Colour dimText { 0xff7d8796 };
    const juce::Colour accent { 0xff4fd1c5 };
    const juce::Colour accent2 { 0xffa78bfa };
}

class PrismaLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PrismaLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
};

// Rotary knob with a caption, attached to a parameter.
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& caption);
    void resized() override;

    juce::Slider slider;

private:
    juce::Label label;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

class Choice : public juce::Component
{
public:
    Choice (juce::AudioProcessorValueTreeState& state, const juce::String& paramId);
    void resized() override { box.setBounds (getLocalBounds().reduced (2, 4)); }

    juce::ComboBox box;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

class Toggle : public juce::Component
{
public:
    Toggle (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& text);
    void resized() override { button.setBounds (getLocalBounds().reduced (2, 4)); }

    juce::ToggleButton button;

private:
    juce::AudioProcessorValueTreeState::ButtonAttachment attachment;
};

// A titled panel whose children are laid out in rows.
class Section : public juce::Component
{
public:
    explicit Section (juce::String title, juce::Colour accentColour = colours::accent);

    // Adds a row of controls; weights give relative widths (defaults to equal).
    void addRow (std::vector<juce::Component*> items, std::vector<float> weights = {}, float heightWeight = 1.0f);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Row
    {
        std::vector<juce::Component*> items;
        std::vector<float> weights;
        float height;
    };
    juce::String title;
    juce::Colour accentColour;
    std::vector<Row> rows;
};

// Serum-style view of the current wavetable: a stack of frames with the active one highlighted.
class WavetableView : public juce::Component, private juce::Timer
{
public:
    WavetableView (PrismaProcessor&, int oscIndex);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    PrismaProcessor& processor;
    std::atomic<float>* tableParam;
    std::atomic<float>* posParam;
    std::atomic<float>* onParam;
    float lastTable = -1, lastPos = -1, lastOn = -1;
};

} // namespace prisma_ui

class PrismaEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PrismaEditor (PrismaProcessor&);
    ~PrismaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    prisma_ui::Section& makeOscSection (int osc);

    template <typename T, typename... Args>
    T& make (Args&&... args)
    {
        auto c = std::make_unique<T> (std::forward<Args> (args)...);
        auto& ref = *c;
        owned.push_back (std::move (c));
        return ref;
    }

    PrismaProcessor& processor;
    prisma_ui::PrismaLookAndFeel lnf;
    std::vector<std::unique_ptr<juce::Component>> owned;

    juce::ComboBox presetBox;
    juce::Label voicesLabel;
    prisma_ui::Section* oscA = nullptr;
    prisma_ui::Section* oscB = nullptr;
    prisma_ui::Section* subNoise = nullptr;
    prisma_ui::Section* filter = nullptr;
    std::vector<prisma_ui::Section*> envs, lfos;
    prisma_ui::Section* matrix = nullptr;
    prisma_ui::Section* master = nullptr;
    juce::MidiKeyboardComponent keyboard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrismaEditor)
};
