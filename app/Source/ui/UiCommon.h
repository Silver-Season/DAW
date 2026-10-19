#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "GlassMaterial.h"
#include "Theme.h"

namespace ui
{
// A custom-painted component with several clickable parts (header strip, slot grid) tells the info bar
// what is under the mouse. Components with a single purpose just call setHelpText (Strings::help::...).
struct HelpProvider
{
    virtual ~HelpProvider() = default;
    virtual juce::String helpAt (juce::Point<int> localPosition) const = 0;
};

// Icons are drawn as paths: the embedded fonts do not carry the geometric symbols.
namespace icons
{
juce::Path play (juce::Rectangle<float> area);
juce::Path stop (juce::Rectangle<float> area);
juce::Path record (juce::Rectangle<float> area);
juce::Path loop (juce::Rectangle<float> area);
juce::Path metronome (juce::Rectangle<float> area);
juce::Path search (juce::Rectangle<float> area);
juce::Path chevronDown (juce::Rectangle<float> area);
juce::Path triangle (juce::Rectangle<float> area, int direction); // 0 up, 1 right, 2 down, 3 left
juce::Path cross (juce::Rectangle<float> area);
juce::Path openExternal (juce::Rectangle<float> area);
juce::Path headphones (juce::Rectangle<float> area);
juce::Path follow (juce::Rectangle<float> area);              // a playhead line with an arrow chasing it
juce::Path backToArrangement (juce::Rectangle<float> area);  // an arrow back to a bar
} // namespace icons

// Single-line text editor for renaming and typing numbers in place. Enter confirms, Esc cancels,
// losing focus confirms. Deletes itself through its owner.
class InlineEditor final : public juce::TextEditor
{
public:
    InlineEditor();

    std::function<void (const juce::String&)> onCommit;
    std::function<void()> onCancel;

    bool keyPressed (const juce::KeyPress&) override;
    void focusLost (FocusChangeType) override;
    void lookAndFeelChanged() override;

private:
    void finish (bool commit);
    bool finished = false;
};

// Draws the text of a help string (title\ndescription) is the info bar's business; this splits it.
inline juce::String helpTitle (const juce::String& help) { return help.upToFirstOccurrenceOf ("\n", false, false); }
inline juce::String helpBody (const juce::String& help) { return help.fromFirstOccurrenceOf ("\n", false, false); }

// True when focus is in a text field: global shortcuts must not take keys away from it.
bool textFieldHasFocus();

// Fills a rounded rectangle with the theme's corner radius (0 for the sharp Neon skin).
inline void fillCell (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float radiusScale = 1.0f)
{
    if (theme ().glass)
        glass::cell (g, r, c, theme ().radius * radiusScale);
    else
    {
        g.setColour (c);
        g.fillRoundedRectangle (r, theme ().radius * radiusScale);
    }
}
inline void strokeCell (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float thickness = 1.0f, float radiusScale = 1.0f)
{
    g.setColour (c);
    g.drawRoundedRectangle (r.reduced (thickness * 0.5f), theme().radius * radiusScale, thickness);
}
} // namespace ui
