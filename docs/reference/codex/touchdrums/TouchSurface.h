#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>
#include <cstdint>

class TouchSurface final
#if JUCE_MAC
    : public juce::NSViewComponent
#else
    : public juce::Component
#endif
{
public:
    TouchSurface();
    ~TouchSurface() override;
    void setCapturing (bool);
    static constexpr bool supported = JUCE_MAC != 0;
    // phase: 0 began, 1 moved, 2 ended. AppKit normalized coordinates, bottom-left origin.
    std::function<void (uint64_t, float, float, int)> onTouch;
    std::function<void()> onCancel;
private:
    void* native = nullptr;
};
