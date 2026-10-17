#include "ProjectPropertiesWindow.h"

#include "ScalePicker.h"

#include "../Strings.h"

namespace ui
{
namespace
{
constexpr int kLabelWidth = 120, kRow = 30, kGap = 10, kPad = 18;
}

class ProjectPropertiesWindow::Content final : public juce::Component, private juce::Timer, private juce::TextEditor::Listener
{
public:
    Content (ProjectModel& m, ProjectActions& a) : model (m), actions (a)
    {
        namespace S = Strings::projectProps;
        tempo.setRange (20.0, 999.0, 0.01);
        tempo.setDecimals (2);
        tempo.setDragSensitivity (0.5);
        tempo.setWheelStep (0.1);
        tempo.onGestureStart = [this] { model.undo.beginNewTransaction (Strings::undoTempo); };
        tempo.onValueChange = [this] (double) { model.setTempo (tempo.getValue()); };

        sigNum.setRange (1.0, 32.0, 1.0);
        sigNum.setDecimals (0);
        sigNum.setDragSensitivity (0.1);
        sigNum.onGestureStart = [this] { model.undo.beginNewTransaction (Strings::undoSignature); };
        sigNum.onValueChange = [this] (double) { model.setSignature ((int) sigNum.getValue(), model.sigDen()); };
        for (int i = 0; i < 5; ++i)
            sigDen.addItem (juce::String (1 << i), i + 1);
        sigDen.onChange = [this] {
            const int den = 1 << juce::jmax (0, sigDen.getSelectedId() - 1);
            if (den != model.sigDen())
                actions.setProjectSignature (model.sigNum(), den);
        };

        scaleButton.onClick = [this] {
            showScalePicker (scaleButton, model.scaleRoot(), model.scaleMode(), [this] (int root, const juce::String& mode) { actions.setProjectScale (root, mode); });
        };
        scaleButton.setHelpText (Strings::help::scaleButton);

        tuning.setRange (ProjectModel::kMinTuning, ProjectModel::kMaxTuning, 0.1);
        tuning.setDecimals (1);
        tuning.setSuffix (S::tuningSuffix);
        tuning.setDragSensitivity (0.1);
        tuning.setWheelStep (0.1);
        tuning.onGestureStart = [this] { model.undo.beginNewTransaction (Strings::undoTuning); };
        tuning.onValueChange = [this] (double) { model.setTuningA4 (tuning.getValue()); };
        tuning.setHelpText (S::tuningHint);
        resetTuning.setButtonText (S::reset);
        resetTuning.onClick = [this] { actions.setProjectTuning (ProjectModel::kDefaultTuning); };

        author.setText (model.author(), false);
        author.addListener (this);
        notes.setMultiLine (true, true);
        notes.setReturnKeyStartsNewLine (true);
        notes.setText (model.projectNotes(), false);
        notes.addListener (this);

        for (auto* c : std::initializer_list<juce::Component*> { &tempo, &sigNum, &sigDen, &scaleButton, &tuning, &resetTuning, &author, &notes })
            addAndMakeVisible (c);
        refresh();
        setSize (440, 400);
        startTimerHz (10);
    }

    void refresh()
    {
        if (! tempo.isEditing())
            tempo.setValue (model.tempo());
        if (! sigNum.isEditing())
            sigNum.setValue (model.sigNum());
        sigDen.setSelectedId (1 + (int) std::round (std::log2 ((double) model.sigDen())), juce::dontSendNotification);
        scaleButton.setButtonText (scaleLabel (model.scaleRoot(), model.scaleMode()));
        if (! tuning.isEditing())
            tuning.setValue (model.tuningA4());
        if (! author.hasKeyboardFocus (true) && author.getText() != model.author())
            author.setText (model.author(), false);
        if (! notes.hasKeyboardFocus (true) && notes.getText() != model.projectNotes())
            notes.setText (model.projectNotes(), false);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        g.fillAll (t.panel);
        g.setColour (t.dimText);
        g.setFont (t.uiFont (12.5f));
        namespace S = Strings::projectProps;
        const juce::String names[] = { S::tempo, S::signature, S::scale, S::tuning, S::author, S::notes };
        const int ys[] = { rowY (0), rowY (1), rowY (2), rowY (3), rowY (5), rowY (6) };
        for (int i = 0; i < 6; ++i)
            g.drawText (names[i], kPad, ys[i], kLabelWidth, kRow, juce::Justification::centredLeft, false);
        g.setFont (t.uiFont (11.0f));
        g.drawFittedText (S::tuningHint, kPad + kLabelWidth, rowY (4) - 2, getWidth() - kPad * 2 - kLabelWidth, 30, juce::Justification::topLeft, 2);
    }

    void resized() override
    {
        const int x = kPad + kLabelWidth, w = getWidth() - x - kPad;
        tempo.setBounds (x, rowY (0), 100, kRow);
        sigNum.setBounds (x, rowY (1), 56, kRow);
        sigDen.setBounds (x + 70, rowY (1), 70, kRow);
        scaleButton.setBounds (x, rowY (2), 160, kRow);
        tuning.setBounds (x, rowY (3), 100, kRow);
        resetTuning.setBounds (x + 110, rowY (3), 50, kRow);
        author.setBounds (x, rowY (5), w, kRow);
        notes.setBounds (x, rowY (6), w, getHeight() - rowY (6) - kPad);
    }

private:
    static int rowY (int i) { return kPad + i * (kRow + kGap) + (i >= 5 ? 14 : 0); }

    void timerCallback() override { refresh(); }
    void textEditorFocusLost (juce::TextEditor& e) override { commit (e); }
    void textEditorReturnKeyPressed (juce::TextEditor& e) override
    {
        if (&e == &author)
            commit (e);
    }
    void commit (juce::TextEditor& e)
    {
        if (&e == &author)
            actions.setProjectAuthor (author.getText());
        else
            actions.setProjectNotes (notes.getText());
    }

    ProjectModel& model;
    ProjectActions& actions;
    NumberField tempo, sigNum, tuning;
    juce::ComboBox sigDen;
    juce::TextButton scaleButton, resetTuning;
    juce::TextEditor author, notes;
};

ProjectPropertiesWindow::ProjectPropertiesWindow (ProjectModel& m, ProjectActions& a, std::function<void()> onClosed)
    : juce::DocumentWindow (Strings::projectProps::title, theme().panel, juce::DocumentWindow::closeButton), closed (std::move (onClosed))
{
    setUsingNativeTitleBar (true);
    setContentOwned (new Content (m, a), true);
    setResizable (false, false);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

void ProjectPropertiesWindow::closeButtonPressed()
{
    if (closed)
        closed();
}
} // namespace ui
