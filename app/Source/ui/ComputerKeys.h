#pragma once

#include "../../../plugins/prisma/Source/PrismaEditor.h"

#include <functional>

namespace ui
{
// The computer keyboard as a MIDI keyboard (key M switches it on). It owns the panel that turns the keys into
// notes and tells the window what happened to it: the panel itself is usually off screen, so without these
// messages Z / X / C / V would change the octave and the velocity invisibly.
class ComputerKeys
{
public:
    explicit ComputerKeys (juce::MidiKeyboardState&);

    prisma_ui::KeyboardPanel& panel() { return keyboard; }
    bool isOn() const { return on; }
    void setOn (bool);
    void toggle() { setOn (! on); }

    // Forwarded from the window. keyPressed returns true if the key was taken.
    bool keyPressed (const juce::KeyPress&);
    bool keyStateChanged();
    void releaseAll() { keyboard.releaseAll(); }
    void releaseKeysNotDown() { keyboard.releaseKeysNotDown(); }

    // The scale mode: while the project has a scale, the ten keys of the A row play its degrees, from the tonic
    // upwards, and the black-key row is silent. Without a scale (or switched off) the keys are chromatic.
    struct ScaleInfo
    {
        int root = 0;
        juce::String mode; // "" = no scale
    };
    std::function<ScaleInfo()> projectScale;
    bool inScale() const { return scaleMode; }
    void setInScale (bool value) { scaleMode = value; }

    // "C3": the octave of the A key, for the lamp in the top bar.
    juce::String octaveText() const;

    // A line for the info bar ("Octave: C3–E4").
    std::function<void (const juce::String&)> onMessage;
    // On/off or octave changed: the lamp refreshes itself.
    std::function<void()> onStateChanged;

private:
    prisma_ui::KeyboardPanel keyboard;
    bool on = false, scaleMode = true;
};
} // namespace ui
