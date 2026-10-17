#include "KeyboardStrip.h"

#include "../Strings.h"

namespace ui
{
KeyboardStrip::KeyboardStrip (ComputerKeys& k) : keys (k)
{
    setHelpText (Strings::help::keyboardStripHelp);
    addAndMakeVisible (keys.panel());
    inScale.setButtonText (Strings::keyboardUi::inScale);
    inScale.setToggleState (keys.inScale(), juce::dontSendNotification);
    inScale.setWantsKeyboardFocus (false);
    inScale.setHelpText (Strings::help::keyboardInScale);
    inScale.onClick = [this] { keys.setInScale (inScale.getToggleState()); };
    addAndMakeVisible (inScale);
}

KeyboardStrip::~KeyboardStrip()
{
    removeChildComponent (&keys.panel());
}

void KeyboardStrip::paint (juce::Graphics& g)
{
    const auto& t = theme();
    g.fillAll (t.chrome);
    g.setColour (t.line);
    g.fillRect (0, 0, getWidth(), 1);
}

void KeyboardStrip::resized()
{
    // laid out at full height whatever the slide has revealed; the strip clips what is not yet in view
    auto r = getLocalBounds().withHeight (kHeight).withTrimmedTop (1).reduced (8, 4);
    inScale.setBounds (r.removeFromRight (84).withHeight (24).withY (r.getY() + 4));
    keys.panel().setBounds (r);
}
} // namespace ui
