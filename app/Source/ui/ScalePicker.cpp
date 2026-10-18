#include "ScalePicker.h"

#include "../Strings.h"

#include "daw/music/Scales.h"

namespace ui
{
juce::String scaleLabel (int root, const juce::String& mode)
{
    const auto* scale = daw::music::findScale (mode.toRawUTF8());
    if (scale == nullptr)
        return Strings::transport::scaleOff;
    return Strings::rootNames[juce::jlimit (0, 11, root)] + " " + Strings::tr (scale->name, scale->nameRu);
}

ScalePicker::ScalePicker (int r, const juce::String& m, Callback onChange) : root (r), mode (m), callback (std::move (onChange))
{
    const int rows = (numModeCells() + 1) / 2;
    setSize (kWidth, rootsBottom() + 6 + rows * (kRowHeight + 2) + kPad);
}

int ScalePicker::numModeCells() const
{
    return 1 + (int) daw::music::allScales().size();
}

juce::Rectangle<int> ScalePicker::rootCell (int i) const
{
    const int col = i % 6, row = i / 6;
    const int w = (kWidth - 2 * kPad - 5 * kGap) / 6;
    return { kPad + col * (w + kGap), kPad + 16 + row * (kCell / 2 + kGap), w, kCell / 2 };
}

juce::Rectangle<int> ScalePicker::modeCell (int i) const
{
    const int col = i % 2, row = i / 2;
    const int w = (kWidth - 2 * kPad - kGap) / 2;
    return { kPad + col * (w + kGap), rootsBottom() + 6 + row * (kRowHeight + 2), w, kRowHeight };
}

int ScalePicker::rootAt (juce::Point<int> p) const
{
    for (int i = 0; i < 12; ++i)
        if (rootCell (i).contains (p))
            return i;
    return -1;
}

int ScalePicker::modeAt (juce::Point<int> p) const
{
    for (int i = 0; i < numModeCells(); ++i)
        if (modeCell (i).contains (p))
            return i;
    return -1;
}

void ScalePicker::paint (juce::Graphics& g)
{
    const auto& t = theme();
    g.fillAll (t.panel);
    g.setColour (t.dimText);
    g.setFont (t.uiFont (10.5f, 500));
    g.drawText (Strings::scaleTonic.toUpperCase(), kPad, kPad - 2, 120, 14, juce::Justification::centredLeft, false);
    g.drawText (Strings::scaleMode.toUpperCase(), kPad, rootsBottom() - 8, 120, 14, juce::Justification::centredLeft, false);

    const auto cellLook = [&] (juce::Rectangle<int> r, const juce::String& text, bool on, bool hot) {
        const auto rf = r.toFloat();
        fillCell (g, rf, on ? t.accent : (hot ? t.panel2.brighter (0.1f) : t.panel2), 0.7f);
        g.setColour (on ? t.clipInk : t.text);
        g.setFont (t.uiFont (12.0f, on ? 600 : 400));
        g.drawText (text, r, juce::Justification::centred, false);
    };
    for (int i = 0; i < 12; ++i)
        cellLook (rootCell (i), Strings::rootNames[i], i == root, i == hoverRoot);

    const auto* current = daw::music::findScale (mode.toRawUTF8());
    for (int i = 0; i < numModeCells(); ++i)
    {
        const bool on = i == 0 ? current == nullptr : (current != nullptr && &daw::music::allScales()[(size_t) i - 1] == current);
        cellLook (modeCell (i), i == 0 ? Strings::scaleNone : Strings::tr (daw::music::allScales()[(size_t) i - 1].name, daw::music::allScales()[(size_t) i - 1].nameRu), on, i == hoverMode);
    }
}

void ScalePicker::mouseMove (const juce::MouseEvent& e)
{
    const int r = rootAt (e.getPosition()), m = modeAt (e.getPosition());
    if (r != hoverRoot || m != hoverMode)
    {
        hoverRoot = r;
        hoverMode = m;
        repaint();
    }
}

void ScalePicker::mouseExit (const juce::MouseEvent&)
{
    hoverRoot = hoverMode = -1;
    repaint();
}

void ScalePicker::mouseDown (const juce::MouseEvent& e)
{
    if (const int r = rootAt (e.getPosition()); r >= 0)
        root = r;
    else if (const int m = modeAt (e.getPosition()); m >= 0)
        mode = m == 0 ? juce::String() : juce::String (daw::music::allScales()[(size_t) m - 1].key);
    else
        return;
    repaint();
    if (callback)
        callback (root, mode);
}

void showScalePicker (juce::Component& target, int root, const juce::String& mode, ScalePicker::Callback onChange)
{
    auto picker = std::make_unique<ScalePicker> (root, mode, std::move (onChange));
    juce::CallOutBox::launchAsynchronously (std::move (picker), target.getScreenBounds(), nullptr);
}
} // namespace ui
