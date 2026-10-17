#pragma once

#include "ComputerKeys.h"
#include "UiCommon.h"

namespace ui
{
// The slide-out on-screen keyboard (Cmd+Alt+K, hidden by default). It shows the panel of the ComputerKeys:
// the keys, the octave buttons and the octave / velocity caption, plus the "in scale" switch.
class KeyboardStrip final : public juce::Component
{
public:
    static constexpr int kHeight = 96;

    explicit KeyboardStrip (ComputerKeys&);
    ~KeyboardStrip() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    ComputerKeys& keys;
    juce::ToggleButton inScale;
};
} // namespace ui
