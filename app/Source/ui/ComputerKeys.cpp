#include "ComputerKeys.h"

#include "../Strings.h"
#include "UiCommon.h"

#include "daw/music/Scales.h"

namespace ui
{
ComputerKeys::ComputerKeys (juce::MidiKeyboardState& state) : keyboard (state)
{
    keyboard.captionFormatter = [] (int base, int velocity) { return Strings::keyboardUi::stripCaption (base, velocity); };
    keyboard.refreshCaption();
    keyboard.noteForKey = [this] (int keyIndex, int baseNote) {
        if (! scaleMode || ! projectScale)
            return prisma_ui::KeyboardPanel::kDefaultNote;
        const auto info = projectScale();
        const auto* scale = daw::music::findScale (info.mode.toRawUTF8());
        if (scale == nullptr)
            return prisma_ui::KeyboardPanel::kDefaultNote;
        const int degree = prisma_ui::KeyboardPanel::whiteKeyDegree (keyIndex);
        if (degree < 0)
            return prisma_ui::KeyboardPanel::kNoNote;
        // the tonic nearest above the base note, so that A is always the first note of the scale in this octave
        return daw::music::degreeToNote (degree, info.root, *scale, baseNote + 11);
    };
    keyboard.onChanged = [this] (bool velocityChanged) {
        if (onMessage)
            onMessage (velocityChanged ? Strings::keyboardUi::velocityMessage (keyboard.getVelocity())
                                       : Strings::keyboardUi::octaveMessage (keyboard.getBaseNote()));
        if (onStateChanged)
            onStateChanged();
    };
}

void ComputerKeys::setOn (bool value)
{
    if (value == on)
        return;
    on = value;
    if (! on)
        keyboard.releaseAll();
    if (onStateChanged)
        onStateChanged();
}

juce::String ComputerKeys::octaveText() const
{
    return Strings::keyboardUi::octaveName (keyboard.getBaseNote());
}

bool ComputerKeys::keyPressed (const juce::KeyPress& key)
{
    if (textFieldHasFocus())
        return false;
    if (on)
        return keyboard.handleKeyPressed (key);

    // Off: only a bare Z or X says why nothing happened; every other key does nothing, as before.
    if (key.getModifiers().getRawFlags() == 0)
    {
        const auto c = prisma_ui::KeyboardPanel::keyLetter (key);
        if ((c == 'z' || c == 'x') && onMessage)
            onMessage (Strings::keyboardUi::keysOffHint);
    }
    return false;
}

bool ComputerKeys::keyStateChanged()
{
    return on && ! textFieldHasFocus() && keyboard.handleKeyStateChanged();
}
} // namespace ui
