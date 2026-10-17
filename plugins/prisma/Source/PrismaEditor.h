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

// On-screen keyboard with octave shift and computer-keyboard playing, laid out like Ableton:
// A W S E D F T G Y H U J K O L P ; play notes, Z / X shift the octave, C / V change the velocity.
// The owner forwards its keyPressed / keyStateChanged calls here.
class KeyboardPanel : public juce::Component
{
public:
    explicit KeyboardPanel (juce::MidiKeyboardState&);
    ~KeyboardPanel() override;

    bool handleKeyPressed (const juce::KeyPress&);
    bool handleKeyStateChanged();
    void releaseAll();
    // Releases the notes whose keys are up, without starting any: for key-ups that went to another window.
    void releaseKeysNotDown();
    void shiftOctave (int delta);

    // The note the A key plays (always a C) and the velocity of C / V. The owner shows them: this panel is
    // often not on screen at all.
    int getBaseNote() const { return baseNote; }
    int getVelocity() const { return velocity; }
    // Called after Z / X / C / V (even when the value did not change because a limit was reached), with
    // true if the velocity was the thing that moved.
    std::function<void (bool velocityChanged)> onChanged;
    // Replaces the English caption beside the keys (base note, velocity).
    std::function<juce::String (int baseNote, int velocity)> captionFormatter;
    void refreshCaption() { updateLabel(); }
    // Which note a key plays. Key i is the i-th of "awsedftgyhujkolp;" (a semitone each, from the base note).
    // Return kDefaultNote to keep baseNote + i, or kNoNote to leave the key silent (the scale mode skips the black ones).
    static constexpr int kDefaultNote = -2, kNoNote = -1;
    std::function<int (int keyIndex, int baseNote)> noteForKey;
    static bool isWhiteKeyIndex (int i) { return i == 0 || i == 2 || i == 4 || i == 5 || i == 7 || i == 9 || i == 11 || i == 12 || i == 14 || i == 16; }
    // 0..9 for the ten white-row keys, -1 for the others
    static int whiteKeyDegree (int i);

    void resized() override;

private:
    void updateLabel();

    juce::MidiKeyboardState& state;
    juce::MidiKeyboardComponent keyboard;
    juce::TextButton octDown { "Oct -" }, octUp { "Oct +" };
    juce::Label info;
    int baseNote = 60; // the note played by the A key
    int velocity = 100;
    static constexpr const char* kKeys = "awsedftgyhujkolp;";
    std::array<int, 17> heldNote {};
};

} // namespace prisma_ui

class PrismaEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PrismaEditor (PrismaProcessor&);
    ~PrismaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& k) override { return keyboard.handleKeyPressed (k); }
    bool keyStateChanged (bool) override { return keyboard.handleKeyStateChanged(); }
    void focusLost (FocusChangeType) override { keyboard.releaseAll(); }

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
    prisma_ui::KeyboardPanel keyboard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrismaEditor)
};
