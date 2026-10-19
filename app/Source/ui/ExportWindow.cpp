#include "ExportWindow.h"

#include "../Strings.h"
#include "../engine/AudioEngine.h"
#include "../model/ArrangementEdit.h"
#include "GlassMaterial.h"
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
        exportButton.getProperties().set ("primary", true); // glass: the accent-filled main action
        for (auto* box : { &what, &range, &format, &bits, &mp3 }) // the details card follows the form
        {
            auto previous = box->onChange;
            box->onChange = [this, previous] {
                if (previous)
                    previous();
                repaint();
            };
        }
        setSize (theme().glass ? 720 : 460, 400);
    }

    ~Content() override { renderer.reset(); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (18, 16);
        details = {};
        if (theme().glass && getWidth() > 600)
        {
            details = r.removeFromRight (250).withTrimmedBottom (46);
            r.removeFromRight (16);
        }
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
        if (! details.isEmpty()) // glass: the buttons sit under the details card too, at the right edge
            buttons = buttons.withRight (details.getRight());
        cancelButton.setBounds (buttons.removeFromRight (110));
        buttons.removeFromRight (8);
        exportButton.setBounds (buttons.removeFromRight (130));
        buttons.removeFromRight (8);
        showButton.setBounds (buttons.removeFromRight (140));
        r.removeFromBottom (8);
        bar.setBounds (r.removeFromTop (18));
        result.setBounds (r);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        g.fillAll (t.panel);
        if (! t.glass || details.isEmpty())
            return;
        // Glass: a card with what will be written: the name, the folder, the length and the size to expect.
        namespace E = Strings::exportUi;
        const auto card = details.toFloat();
        glass::surface (g, card, t.chrome, 12.0f);
        g.setColour (t.line.withAlpha (0.7f));
        g.drawRoundedRectangle (card.reduced (0.5f), 12.0f, 1.0f);
        auto r = details.reduced (16, 14);
        g.setColour (t.text);
        g.setFont (t.uiFont (14.0f, 600));
        g.drawText (E::fileDetails, r.removeFromTop (22), juce::Justification::centredLeft, false);
        r.removeFromTop (8);
        const auto field = [&] (const juce::String& label, const juce::String& value) {
            g.setColour (t.dimText);
            g.setFont (t.uiFont (11.5f));
            g.drawText (label, r.removeFromTop (16), juce::Justification::centredLeft, false);
            auto box = r.removeFromTop (28).toFloat();
            g.setColour (t.panel);
            g.fillRoundedRectangle (box, 7.0f);
            g.setColour (t.line.withAlpha (0.7f));
            g.drawRoundedRectangle (box.reduced (0.5f), 7.0f, 1.0f);
            g.setColour (t.text);
            g.setFont (t.uiFont (12.5f));
            g.drawText (value, box.reduced (10.0f, 0.0f), juce::Justification::centredLeft, true);
            r.removeFromTop (8);
        };
        const int f = format.getSelectedId();
        const juce::String ext = f == 2 ? "aiff" : (f == 3 ? "flac" : (f == 4 ? "mp3" : "wav"));
        field (E::fileName, defaultName + "." + ext);
        field (E::destination, defaultFolder.getFileName());
        r.removeFromTop (4);
        // length of what will render, and its size: PCM exactly, FLAC about 60 % of it, MP3 from the bit rate
        double startBeat = 0.0, endBeat = 0.0;
        if (range.getSelectedId() == 2)
        {
            startBeat = model.arrangementLoopStart();
            endBeat = model.arrangementLoopEnd();
        }
        else
            for (const auto& tr : model.tracks())
                for (const auto& c : tr.arrangementClips())
                    endBeat = juce::jmax (endBeat, c.end());
        const double seconds = juce::jmax (0.0, endBeat - startBeat) * 60.0 / juce::jmax (1.0, model.tempo());
        const double rate = engine.getSampleRate() > 0.0 ? engine.getSampleRate() : 48000.0;
        const int depth = bits.getSelectedId();
        double bytes = seconds * rate * 2.0 * (double) depth / 8.0;
        if (f == 3)
            bytes *= 0.6;
        if (f == 4)
            bytes = seconds * (mp3.getSelectedId() >= 1000 ? 245.0 : (double) mp3.getSelectedId()) * 1000.0 / 8.0;
        const auto line = [&] (const juce::String& label, const juce::String& value) {
            auto row = r.removeFromTop (20);
            g.setColour (t.dimText);
            g.setFont (t.uiFont (12.0f));
            g.drawText (label, row.removeFromLeft (row.getWidth() / 2), juce::Justification::centredLeft, false);
            g.setColour (t.text);
            g.drawText (value, row, juce::Justification::centredLeft, false);
        };
        if (seconds <= 0.0)
        {
            g.setColour (t.dimText);
            g.setFont (t.uiFont (12.0f));
            g.drawText (E::nothingToExport, r.removeFromTop (20), juce::Justification::centredLeft, true);
            return;
        }
        const int total = juce::roundToInt (seconds);
        line (E::estimatedLength, juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2));
        line (E::estimatedSize, Strings::exportUi::sizeText (bytes));
        g.setColour (t.dimText);
        g.setFont (t.uiFont (11.5f));
        g.drawText (ext.toUpperCase() + ", " + juce::String (rate / 1000.0, rate == 44100.0 ? 1 : 0) + " kHz" + (f == 4 ? juce::String() : ", " + (depth == 32 ? juce::String ("32 float") : juce::String (depth) + " bit")),
                    r.removeFromTop (20), juce::Justification::centredLeft, true);
    }

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
    juce::Rectangle<int> details; // glass: the file details card
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
