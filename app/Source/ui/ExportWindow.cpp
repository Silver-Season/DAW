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
        label (whatLabel, E::what);
        label (rangeLabel, E::range);
        label (formatLabel, E::format);
        label (bitsLabel, E::bits);
        label (normLabel, E::normalise);

        what.addItem (E::master, 1);
        what.addItem (E::masterAndStems, 2);
        what.addItem (E::stemsOnly, 3);
        what.setSelectedId (1, juce::dontSendNotification);
        what.onChange = [this] { updateVisibility(); };
        range.addItem (E::wholeSong, 1);
        range.addItem (E::loop, 2);
        range.setSelectedId (1, juce::dontSendNotification);
        format.addItem ("WAV", 1);
        format.addItem ("AIFF", 2);
        format.addItem ("FLAC", 3);
        format.addItem ("MP3", 4);
        format.setSelectedId (1, juce::dontSendNotification);
        format.onChange = [this] { updateVisibility(); };
        bits.addItem ("16", 16);
        bits.addItem ("24", 24);
        bits.addItem (Strings::audioRec::float32, 32);
        bits.setSelectedId (24, juce::dontSendNotification);
        mp3.addItem ("320 kbps", 320);
        mp3.addItem ("256 kbps", 256);
        mp3.addItem ("192 kbps", 192);
        mp3.addItem ("128 kbps", 128);
        mp3.addItem (E::vbrBest, 1000);
        mp3.addItem (E::vbrGood, 1002);
        mp3.setSelectedId (320, juce::dontSendNotification);
        normalise.addItem (E::none, 1);
        normalise.addItem (E::peak01, 2);
        normalise.addItem (E::peak1, 3);
        normalise.addItem (E::lufs14, 14);
        normalise.addItem (E::lufs16, 16);
        normalise.addItem (E::lufs9, 9);
        normalise.setSelectedId (1, juce::dontSendNotification);
        tail.setButtonText (E::tail);
        tail.setToggleState (true, juce::dontSendNotification);
        stemsMuted.setButtonText (E::stemsMuted);
        for (auto* c : std::initializer_list<juce::Component*> { &what, &range, &format, &bits, &normalise, &tail, &exportButton, &cancelButton, &result })
            addAndMakeVisible (c);
        addChildComponent (mp3);
        addChildComponent (stemsMuted);
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
        showButton.onClick = [this] {
            if (what.getSelectedId() == 3 && lastStems.isDirectory())
                lastStems.revealToUser();
            else
                lastFile.revealToUser();
        };

        if (arrangement::songLengthBeats (model) <= 0.0 || ! hasClips())
            result.setText (E::emptyArrangement, juce::dontSendNotification);
        updateVisibility();
        setSize (460, 400);
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
        row (whatLabel, what);
        row (rangeLabel, range);
        row (formatLabel, format);
        row (bitsLabel, bits);
        mp3.setBounds (bits.getBounds());
        row (normLabel, normalise);
        tail.setBounds (r.removeFromTop (26).withTrimmedLeft (150));
        stemsMuted.setBounds (r.removeFromTop (26).withTrimmedLeft (150));
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
    bool isMp3() const { return format.getSelectedId() == 4; }

    void updateVisibility()
    {
        bits.setVisible (! isMp3());
        mp3.setVisible (isMp3());
        bitsLabel.setText (isMp3() ? Strings::exportUi::mp3Quality : Strings::exportUi::bits, juce::dontSendNotification);
        stemsMuted.setVisible (what.getSelectedId() != 1);
    }

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
        const int f = format.getSelectedId();
        const juce::String ext = f == 2 ? ".aiff" : (f == 3 ? ".flac" : (f == 4 ? ".mp3" : ".wav"));
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
        const int f = format.getSelectedId();
        o.format = f == 2 ? "aiff" : (f == 3 ? "flac" : (f == 4 ? "mp3" : "wav"));
        o.bitDepth = bits.getSelectedId();
        if (mp3.getSelectedId() >= 1000)
            o.mp3Vbr = mp3.getSelectedId() - 1000;
        else
            o.mp3Kbps = mp3.getSelectedId();
        const int n = normalise.getSelectedId();
        o.normalisePeakDb = n == 2 ? -0.1 : (n == 3 ? -1.0 : 1.0);
        o.normaliseLufs = n == 9 || n == 14 || n == 16;
        o.lufsTarget = -(double) n;
        o.stems = what.getSelectedId() != 1;
        o.stemsIncludeMaster = what.getSelectedId() == 2;
        o.stemsIncludeMuted = stemsMuted.getToggleState();
        o.stemsFolder = file.getParentDirectory().getNonexistentChildFile (file.getFileNameWithoutExtension() + Strings::exportUi::stemsFolderSuffix, {}, false);
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
        namespace E = Strings::exportUi;
        const bool masterWritten = what.getSelectedId() != 3;
        juce::String text;
        if (masterWritten)
        {
            text = E::done (lastFile.getFileName(), r.seconds, r.peakDb) + "\n" + E::loudness (r.lufs, r.rangeLu, r.truePeakDb);
            if (r.limitedByPeak)
                text += "\n" + E::limitedByPeak;
            if (r.peakDb > 0.0)
                text += "\n" + E::clipping (r.peakDb);
        }
        if (! r.stemFiles.isEmpty())
        {
            lastStems = juce::File (r.stemFiles[0]).getParentDirectory();
            text += (text.isEmpty() ? "" : "\n") + E::stemsWritten (r.stemFiles.size(), lastStems.getFileName());
        }
        result.setText (text, juce::dontSendNotification);
        showButton.setVisible (true);
    }

    ExportWindow& window;
    Settings& settings;
    ProjectModel& model;
    AudioEngine& engine;
    juce::File defaultFolder, lastFile, lastStems;
    juce::String defaultName;
    juce::Label whatLabel, rangeLabel, formatLabel, bitsLabel, normLabel, result;
    juce::ComboBox what, range, format, bits, mp3, normalise;
    juce::ToggleButton tail, stemsMuted;
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
