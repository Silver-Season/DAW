#include "QuantiseWindow.h"

#include "../Strings.h"

namespace ui
{
class QuantiseWindow::Content final : public juce::Component
{
public:
    explicit Content (Hooks h) : hooks (std::move (h))
    {
        const auto q = hooks.settings();
        for (int i = 0; i < Strings::quantiseUi::numGrids; ++i)
            gridBox.addItem (Strings::quantiseUi::gridNames[i], i + 1);
        int selected = 4;
        for (int i = 0; i < Strings::quantiseUi::numGrids; ++i)
            if (std::abs (Strings::quantiseUi::gridBeats[i] - q.grid) < 1.0e-6)
                selected = i + 1;
        gridBox.setSelectedId (selected, juce::dontSendNotification);
        gridBox.setHelpText (Strings::help::editorQuantise);
        strength.setRange (0.0, 100.0, 1.0);
        strength.setSuffix (" %");
        strength.setDecimals (0);
        strength.setValue (q.strength * 100.0);
        swing.setRange (0.0, 75.0, 1.0);
        swing.setSuffix (" %");
        swing.setDecimals (0);
        swing.setValue (q.swing * 100.0);
        ends.setButtonText (Strings::quantiseUi::ends);
        ends.setToggleState (q.ends, juce::dontSendNotification);
        applyButton.setButtonText (Strings::quantiseUi::apply);
        closeButton.setButtonText (Strings::quantiseUi::close);
        applyButton.onClick = [this] {
            ProjectModel::QuantiseSettings s;
            s.grid = Strings::quantiseUi::gridBeats[juce::jlimit (0, Strings::quantiseUi::numGrids - 1, gridBox.getSelectedId() - 1)];
            s.strength = strength.getValue() / 100.0;
            s.swing = swing.getValue() / 100.0;
            s.ends = ends.getToggleState();
            hooks.apply (s);
        };
        closeButton.onClick = [this] {
            if (auto* w = findParentComponentOfClass<QuantiseWindow>())
                w->closeButtonPressed();
        };
        for (auto* c : std::initializer_list<juce::Component*> { &gridBox, &strength, &swing, &ends, &applyButton, &closeButton })
            addAndMakeVisible (c);
        setSize (300, 214);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        g.fillAll (t.panel);
        g.setColour (t.dimText);
        g.setFont (t.uiFont (12.0f));
        const juce::String* names[3] = { &Strings::quantiseUi::grid, &Strings::quantiseUi::strength, &Strings::quantiseUi::swing };
        for (int i = 0; i < 3; ++i)
            g.drawText (*names[i], 16, 16 + i * 38, 110, 28, juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        const auto row = [&] (int i) { return juce::Rectangle<int> (130, 16 + i * 38, getWidth() - 146, 28); };
        gridBox.setBounds (row (0));
        strength.setBounds (row (1));
        swing.setBounds (row (2));
        ends.setBounds (16, 16 + 3 * 38, getWidth() - 32, 24);
        auto buttons = r.removeFromBottom (28);
        applyButton.setBounds (buttons.removeFromRight (110));
        buttons.removeFromRight (8);
        closeButton.setBounds (buttons.removeFromRight (90));
    }

private:
    Hooks hooks;
    juce::ComboBox gridBox;
    NumberField strength, swing;
    juce::ToggleButton ends;
    juce::TextButton applyButton, closeButton;
};

QuantiseWindow::QuantiseWindow (Hooks h)
    : juce::DocumentWindow (Strings::quantiseUi::title, theme().panel, juce::DocumentWindow::closeButton), hooks (std::move (h))
{
    setUsingNativeTitleBar (true);
    auto content = std::make_unique<Content> (hooks);
    setContentOwned (content.release(), true);
    setResizable (false, false);
    setAlwaysOnTop (true);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

void QuantiseWindow::closeButtonPressed()
{
    if (hooks.closed)
        hooks.closed();
    setVisible (false);
}
} // namespace ui
