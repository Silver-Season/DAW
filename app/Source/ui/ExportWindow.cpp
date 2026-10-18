#include "ExportWindow.h"

#include "../Strings.h"
#include "../model/ArrangementEdit.h"
#include "Theme.h"

namespace ui
{
class ExportWindow::Content final : public juce::Component, private juce::Timer
{
public:
    Content (ExportWindow& w, Settings& s, ProjectModel& m, AudioEngine& e, juce::File folder, juce::String name)
        : window (w), settings (s), model (m), engine (e), defaultFolder (std::move (folder)), defaultName (std::move (name))
    {
        namespace E = Strings::exportUi;
        const auto label = [this] (juce::Label& l, const juce::String& text) {
            l.setText (text, juce::dontSendNotification);
            l.setColour (juce::Label::textColourId, theme().dimText);
            l.setFont (theme().uiFont (12.5f));
            addAndMakeVisible (l);
        };
        label (rangeLabel, E::range);
        label (formatLabel, E::format);
        label (bitsLabel, E::bits);
        label (normLabel, E::normalise);

        range.addItem (E::wholeSong, 1);
        range.addItem (E::loop, 2);
        range.setSelectedId (1, juce::dontSendNotification);
        format.addItem ("WAV", 1);
        format.addItem ("AIFF", 2);
        format.addItem ("FLAC", 3);
        format.setSelectedId (1, juce::dontSendNotification);
        bits.addItem ("16", 16);
        bits.addItem ("24", 24);
        bits.addItem (Strings::audioRec::float32, 32);
        bits.setSelectedId (24, juce::dontSendNotification);
        normalise.addItem (E::none, 1);
        normalise.addItem (E::peak01, 2);
        normalise.addItem (E::peak1, 3);
        normalise.setSelectedId (1, juce::dontSendNotification);
        tail.setButtonText (E::tail);
        tail.setToggleState (true, juce::dontSendNotification);
        for (auto* c : std::initializer_list<juce::Component*> { &range, &format, &bits, &normalise, &tail, &exportButton, &cancelButton, &result })
            addAndMakeVisible (c);
        addChildComponent (bar);
        addChildComponent (showButton);
        result.setJustificationType (juce::Justification::topLeft);
        result.setFont (theme().uiFont (12.5f));
        result.setColour (juce::Label::textColourId, theme().text);
        exportButton.setButtonText (E::exportButton);
        exportButton.onClick = [this] { chooseFile(); };
        cancelButton.setButtonText (E::close);
        cancelButton.onClick = [this] {
            if (renderer != nullptr && renderer->isRendering())
                renderer->cancel();
            else
                window.closeButtonPressed();
        };
        showButton.setButtonText (E::showInFinder);
        showButton.onClick = [this] { lastFile.revealToUser(); };

        if (arrangement::songLengthBeats (model) <= 0.0 || ! hasClips())
            result.setText (E::emptyArrangement, juce::dontSendNotification);
        setSize (440, 330);
    }

    ~Content() override { renderer.reset(); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (18, 16);
        const auto row = [&] (juce::Label& l, juce::Component& c) {
            auto line = r.removeFromTop (28);
            l.setBounds (line.removeFromLeft (150));
            c.setBounds (line.removeFromLeft (200).reduced (0, 2));
            r.removeFromTop (6);
        };
        row (rangeLabel, range);
        row (formatLabel, format);
        row (bitsLabel, bits);
        row (normLabel, normalise);
        tail.setBounds (r.removeFromTop (26).withTrimmedLeft (150));
        r.removeFromTop (10);
        auto buttons = r.removeFromBottom (30);
        cancelButton.setBounds (buttons.removeFromRight (110));
        buttons.removeFromRight (8);
        exportButton.setBounds (buttons.removeFromRight (130));
        buttons.removeFromRight (8);
        showButton.setBounds (buttons.removeFromRight (140));
        r.removeFromBottom (8);
        bar.setBounds (r.removeFromTop (18));
        result.setBounds (r);
    }

    void paint (juce::Graphics& g) override { g.fillAll (theme().panel); }

private:
    bool hasClips() const
    {
        for (const auto& t : model.tracks())
            if (t.numArrangementClips() > 0)
                return true;
        return false;
    }

    void chooseFile()
    {
        if (! hasClips())
            return;
        const juce::String ext = format.getSelectedId() == 2 ? ".aiff" : (format.getSelectedId() == 3 ? ".flac" : ".wav");
        defaultFolder.createDirectory();
        chooser = std::make_unique<juce::FileChooser> (Strings::exportUi::title, defaultFolder.getChildFile (defaultName + ext), "*" + ext);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe = juce::Component::SafePointer<Content> (this), ext] (const juce::FileChooser& fc) {
                                  if (safe != nullptr && fc.getResult() != juce::File())
                                      safe->startExport (fc.getResult().withFileExtension (ext));
                              });
    }

    void startExport (const juce::File& file)
    {
        OfflineRenderer::Options o;
        if (range.getSelectedId() == 2)
        {
            o.startBeat = model.arrangementLoopStart();
            o.endBeat = model.arrangementLoopEnd();
        }
        else
        {
            o.startBeat = 0.0;
            double end = 0.0;
            for (const auto& t : model.tracks())
                for (const auto& c : t.arrangementClips())
                    end = juce::jmax (end, c.end());
            o.endBeat = end;
        }
        o.file = file;
        o.format = format.getSelectedId() == 2 ? "aiff" : (format.getSelectedId() == 3 ? "flac" : "wav");
        o.bitDepth = bits.getSelectedId();
        o.normalisePeakDb = normalise.getSelectedId() == 2 ? -0.1 : (normalise.getSelectedId() == 3 ? -1.0 : 1.0);
        o.tail = tail.getToggleState();
        lastFile = file;

        renderer = std::make_unique<OfflineRenderer> (settings, model, engine, o);
        exportButton.setEnabled (false);
        showButton.setVisible (false);
        bar.setVisible (true);
        cancelButton.setButtonText (Strings::exportUi::cancel);
        result.setText (Strings::exportUi::rendering, juce::dontSendNotification);
        startTimerHz (15);
        renderer->start ([safe = juce::Component::SafePointer<Content> (this)] (const OfflineRenderer::Result& r) {
            if (safe != nullptr)
                safe->finished (r);
        });
    }

    void timerCallback() override
    {
        if (renderer != nullptr)
            progress = renderer->getProgress();
        bar.repaint();
    }

    void finished (const OfflineRenderer::Result& r)
    {
        stopTimer();
        bar.setVisible (false);
        exportButton.setEnabled (true);
        cancelButton.setButtonText (Strings::exportUi::close);
        if (! r.ok)
        {
            result.setText (r.error == "cancelled" ? Strings::exportUi::cancelled : Strings::exportUi::failed + r.error, juce::dontSendNotification);
            return;
        }
        auto text = Strings::exportUi::done (lastFile.getFileName(), r.seconds, r.peakDb);
        if (r.peakDb > 0.0)
            text += "\n" + Strings::exportUi::clipping (r.peakDb);
        result.setText (text, juce::dontSendNotification);
        showButton.setVisible (true);
    }

    ExportWindow& window;
    Settings& settings;
    ProjectModel& model;
    AudioEngine& engine;
    juce::File defaultFolder, lastFile;
    juce::String defaultName;
    juce::Label rangeLabel, formatLabel, bitsLabel, normLabel, result;
    juce::ComboBox range, format, bits, normalise;
    juce::ToggleButton tail;
    juce::TextButton exportButton, cancelButton, showButton;
    double progress = 0.0;
    juce::ProgressBar bar { progress };
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<OfflineRenderer> renderer;
};

ExportWindow::ExportWindow (Settings& s, ProjectModel& m, AudioEngine& e, juce::File folder, juce::String name, std::function<void()> closed)
    : juce::DocumentWindow (Strings::exportUi::title, theme().panel, juce::DocumentWindow::closeButton), onClosed (std::move (closed))
{
    setUsingNativeTitleBar (true);
    setContentOwned (new Content (*this, s, m, e, std::move (folder), std::move (name)), true);
    setResizable (false, false);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

ExportWindow::~ExportWindow() = default;

void ExportWindow::closeButtonPressed()
{
    if (onClosed)
        onClosed();
}
} // namespace ui
