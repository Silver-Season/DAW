#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace ui
{
// Small question windows drawn by the app itself (not the system's), all asynchronous.

// Asks for a line of text. `done` gets the trimmed text, or is not called when cancelled (or empty, unless `allowEmpty`).
void promptText (const juce::String& title, const juce::String& message, const juce::String& initial, std::function<void (const juce::String&)> done,
                 bool allowEmpty = false);

// OK / Cancel. `done` is called with true for OK.
void confirm (const juce::String& title, const juce::String& message, const juce::String& okText, std::function<void (bool)> done,
              const juce::String& cancelText = {});

// Three answers, like "Save changes?" : 0 = first button, 1 = second, 2 = cancel.
void askThree (const juce::String& title, const juce::String& message, const juce::String& first, const juce::String& second,
               const juce::String& cancel, std::function<void (int)> done);

void showMessage (const juce::String& title, const juce::String& message);
} // namespace ui
