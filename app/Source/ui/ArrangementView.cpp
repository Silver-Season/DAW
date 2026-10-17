#include "ArrangementView.h"

#include "../Strings.h"
#include "../model/ProjectModel.h"

namespace ui
{
ArrangementView::ArrangementView (ProjectModel& m) : model (m)
{
    setHelpText (Strings::help::arrangementView);
}

void ArrangementView::paint (juce::Graphics& g)
{
    const auto& t = theme();
    auto r = getLocalBounds();
    const auto ruler = r.removeFromTop (kRulerHeight);

    fillCell (g, ruler.toFloat().reduced (10.0f, 0.0f).withTrimmedTop (8.0f), t.panel);
    g.setColour (t.dimText);
    g.setFont (t.numFont (11.0f));
    // one bar every 80 px, as many as fit
    const int left = 10;
    int bar = 1;
    for (int x = left; x < getWidth() - 10; x += 80, ++bar)
    {
        g.setColour (t.line);
        g.fillRect (x, ruler.getY() + 14, 1, kRulerHeight - 14);
        g.setColour (t.dimText);
        g.drawText (juce::String (bar), x + 4, ruler.getY() + 8, 60, 16, juce::Justification::centredLeft, false);
    }

    g.setColour (t.line);
    for (int x = left; x < getWidth() - 10; x += 80)
        g.fillRect (x, r.getY(), 1, r.getHeight());

    g.setColour (t.text);
    g.setFont (t.uiFont (16.0f, 600));
    g.drawText (Strings::arrangement::placeholder, r.withTrimmedBottom (20), juce::Justification::centred, false);
    g.setColour (t.dimText);
    g.setFont (t.uiFont (12.5f));
    g.drawText (Strings::arrangement::hint, r.withTrimmedTop (r.getHeight() / 2 + 6), juce::Justification::centredTop, false);
    juce::ignoreUnused (model);
}
} // namespace ui
