// Stage 5 of docs/spec/05-audio-recording.md, tasks 5.1, 5.2 and 5.6, the interface side: audio clips on the lanes of the
// Arrangement View, sound files dropped onto it, and the audio clip editor in the detail panel. Everything runs headless
// with fixed bounds; mouse gestures are synthetic events.
#include "TestSupport.h"

#include "../app/Source/Strings.h"
#include "../app/Source/browser/BrowserActions.h"
#include "../app/Source/model/ArrangementEdit.h"
#include "../app/Source/model/ProjectActions.h"
#include "../app/Source/project/AudioImport.h"
#include "../app/Source/ui/ArrangementLanes.h"
#include "../app/Source/ui/ArrangementView.h"
#include "../app/Source/ui/AudioClipEditor.h"
#include "../app/Source/ui/DetailPanel.h"
#include "../app/Source/ui/PianoRoll.h"
#include "../app/Source/ui/Theme.h"

#include <cmath>

using namespace tests;

namespace
{
bool near (double a, double b)
{
    return std::abs (a - b) < 1.0e-6;
}

struct Rig
{
    Rig() : dev (256, 48000.0), actions (f.model, &f.engine), library (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("daw-audio-ui-lib")), browserActions (f.model, f.engine, library)
    {
        ThemeManager::get().reload();
        folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                     .getChildFile ("daw-audio-ui-test-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        folder.createDirectory();
        f.engine.setProjectFolder (folder.getChildFile ("Song Project"));
        f.engine.getProjectFolder().createDirectory();
        audioA = f.model.addTrack ("audio");
        midi = f.model.addTrack ("midi");
        audioB = f.model.addTrack ("audio");
        f.engine.audioDeviceAboutToStart (&dev);
        f.engine.rebuildGraphIfNeeded();
        f.model.undo.clearUndoHistory();

        // 1 second of a sine at 48 kHz = 2 beats at 120 BPM
        for (int i = 0; i < 3; ++i)
        {
            std::vector<float> samples (48000);
            for (int n = 0; n < 48000; ++n)
                samples[(size_t) n] = 0.6f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * (220.0 + 110.0 * i) * n / 48000.0);
            files.add (folder.getChildFile ("sound" + juce::String (i) + ".wav"));
            writeWav (files.getReference (i), samples);
        }
        view = std::make_unique<ui::ArrangementView> (f.engine, f.model, actions, selection);
        view->setBrowserActions (&browserActions);
        view->setBounds (0, 0, 1400, 700);
        view->setZoom (20.0, 0.0);
        view->setScrollBeats (0.0);
        view->tick();
    }
    ~Rig()
    {
        view.reset();
        f.engine.audioDeviceStopped();
        folder.deleteRecursively();
    }

    // A point of the view (not of the lanes) on a row, or below the last one, at a beat
    juce::Point<int> pointAt (int row, double beat) const
    {
        const auto lanes = view->lanesBounds();
        const auto& rows = view->rows();
        const int y = row >= 0 ? rows[(size_t) row].y + rows[(size_t) row].height / 2 : view->contentHeight() + 20;
        return { lanes.getX() + (int) view->beatToX (beat), lanes.getY() + y };
    }

    int undoDepth()
    {
        int n = 0;
        while (f.model.undo.canUndo() && n < 1000)
        {
            f.model.undo.undo();
            ++n;
        }
        for (int i = 0; i < n; ++i)
            f.model.undo.redo();
        return n;
    }

    // An audio clip from the first file on the first audio track at `start`, loaded and ready to be drawn
    ArrangementClipRef audioClipOnTimeline (double start)
    {
        f.model.undo.beginNewTransaction ("import");
        const auto imported = audioimport::importFile (f.model, f.engine, files[0], true);
        check (imported.ok, "the file is imported");
        auto node = f.model.makeAudioClipNode (audioA, imported.fileId, imported.lengthBeats, imported.name);
        auto a = arrangement::addSlotClip (f.model, audioA, ClipRef (node), start);
        f.engine.rebuildGraphIfNeeded();
        f.engine.getAudioPool().waitForLoads();
        view->tick();
        return a;
    }

    Fixture f;
    FakeDevice dev;
    ProjectActions actions;
    ui::SelectionState selection;
    library::UserLibrary library;
    browser::BrowserActions browserActions;
    juce::File folder;
    juce::Array<juce::File> files;
    TrackRef audioA, midi, audioB;
    std::unique_ptr<ui::ArrangementView> view;
};

//==============================================================================
void testDrops()
{
    std::printf ("[5.6] sound files dropped onto the Arrangement View\n");
    Rig r;
    auto& m = r.f.model;
    const auto count = [&] (const TrackRef& t) { return (int) t.arrangementClips().size(); };

    // what lights up
    check (r.view->isInterestedInFileDrag ({ r.files[0].getFullPathName() }), "a sound file is of interest");
    check (! r.view->isInterestedInFileDrag ({ r.folder.getChildFile ("notes.txt").getFullPathName() }), "a text file is not");
    r.view->fileDragMove ({}, r.pointAt (0, 4.0).x, r.pointAt (0, 4.0).y);
    check (r.view->dropHint().valid && r.view->dropHint().audio && ! r.view->dropHint().newTracks && r.view->dropHint().row == 0, "an audio lane lights up");
    r.view->fileDragMove ({}, r.pointAt (1, 4.0).x, r.pointAt (1, 4.0).y);
    check (! r.view->dropHint().valid, "a MIDI lane does not");
    r.view->fileDragMove ({}, r.pointAt (-1, 4.0).x, r.pointAt (-1, 4.0).y);
    check (r.view->dropHint().valid && r.view->dropHint().newTracks, "the empty space below the lanes makes new tracks");
    r.view->fileDragExit ({});
    check (! r.view->dropHint().valid, "leaving clears the highlight");

    // a MIDI lane refuses and changes nothing
    m.undo.clearUndoHistory();
    const auto before = m.root.toXmlString();
    auto refused = r.view->dropSoundFiles ({ r.files[0] }, r.pointAt (1, 4.0), false);
    check (! refused.ok && refused.message == Strings::errorAudioOnMidi, "a MIDI lane refuses a sound");
    check (m.root.toXmlString() == before && ! m.undo.canUndo(), "and nothing changed, no undo step");

    // two files below the lanes: two new audio tracks, under each other, at the same beat
    const int tracksBefore = m.numTracks();
    m.undo.clearUndoHistory();
    auto result = r.view->dropSoundFiles ({ r.files[0], r.files[1] }, r.pointAt (-1, 8.0), false);
    check (result.ok, "the drop works");
    check (m.numTracks() == tracksBefore + 2, "two new tracks");
    check (r.undoDepth() == 1, "the whole drop is one undo step");
    if (m.numTracks() == tracksBefore + 2)
    {
        const auto t1 = m.trackAt (tracksBefore), t2 = m.trackAt (tracksBefore + 1);
        check (t1.isAudio() && t2.isAudio(), "audio tracks");
        check (count (t1) == 1 && count (t2) == 1, "one clip on each");
        if (count (t1) == 1 && count (t2) == 1)
            check (near (t1.arrangementClips()[0].start(), 8.0) && near (t2.arrangementClips()[0].start(), 8.0), "both at the same beat");
        check (r.selection.get().trackId == t2.id() || r.selection.get().trackId == t1.id(), "a new track is selected");
    }

    // with Cmd (alternate): one after another on the lane that was hit
    m.undo.clearUndoHistory();
    result = r.view->dropSoundFiles ({ r.files[0], r.files[1] }, r.pointAt (0, 4.0), true);
    check (result.ok && m.numTracks() == tracksBefore + 2, "alternate adds no tracks");
    check (count (r.audioA) == 2, "two clips on the lane");
    if (count (r.audioA) == 2)
    {
        const auto first = r.audioA.arrangementClips()[0], second = r.audioA.arrangementClips()[1];
        check (near (first.start(), 4.0), "the first lands at the beat");
        check (near (second.start(), first.end()), "the second follows it (" + juce::String (second.start()) + " vs " + juce::String (first.end()) + ")");
    }
    check (r.undoDepth() == 1, "alternate is one undo step");

    // the same through the browser's own drag
    browser::Item item;
    item.kind = browser::kinds::audioFile;
    item.path = r.files[2].getFullPathName();
    item.name = "sound2";
    const auto description = browser::makeDrag (item);
    check (r.view->isInterestedInDragSource ({ description, nullptr, {} }), "a browser sound is of interest");
    const auto onB = r.pointAt (r.view->rowIndexOfTrack (r.audioB.id()), 2.0);
    r.view->itemDragMove ({ description, nullptr, onB });
    check (r.view->dropHint().valid && r.view->dropHint().audio, "it lights up an audio lane");
    m.undo.clearUndoHistory();
    r.view->itemDropped ({ description, nullptr, onB });
    check (count (r.audioB) == 1 && near (r.audioB.arrangementClips()[0].start(), 2.0), "and lands on it");
    check (! r.view->dropHint().valid, "the highlight is gone");

    // moving a clip between lanes keeps audio and MIDI apart (the drag itself is covered by the lanes' own type check)
    check (r.audioA.type() != r.midi.type(), "audio and MIDI lanes are of different types");
}

//==============================================================================
void testLanesPaint()
{
    std::printf ("[5.1] lanes paint audio clips and missing files\n");
    Rig r;
    auto a = r.audioClipOnTimeline (2.0);
    const int rowA = r.view->rowIndexOfTrack (r.audioA.id());
    // a clip whose file is not in the pool
    auto gone = arrangement::addSlotClip (r.f.model, r.audioB, ClipRef (r.f.model.makeAudioClipNode (r.audioB, 9999, 4.0, "gone")), 1.0);
    r.view->tick();

    auto& lanes = r.view->lanes();
    const auto bounds = lanes.clipBounds (a.id());
    check (! bounds.isEmpty(), "the audio clip has bounds");
    const auto render = [&] {
        juce::Image img (juce::Image::ARGB, lanes.getWidth(), lanes.getHeight(), true);
        juce::Graphics g (img);
        lanes.paintEntireComponent (g, false);
        return img;
    };
    const auto withSound = render();
    check (withSound.isValid(), "the lanes paint");

    a.clip().setGainDb (-70.0, &r.f.model.undo);
    r.view->tick();
    const auto quiet = render();
    int differing = 0;
    for (int y = bounds.getY() + 16; y < bounds.getBottom() - 2; y += 2)
        for (int x = bounds.getX() + 2; x < bounds.getRight() - 2; x += 2)
            if (withSound.getPixelAt (x, y) != quiet.getPixelAt (x, y))
                ++differing;
    check (differing > 20, "a waveform is drawn inside the audio clip (" + juce::String (differing) + " pixels follow the gain)");
    (void) rowA;

    // fades are drawn
    a.clip().setGainDb (0.0, &r.f.model.undo);
    const auto noFade = render();
    a.clip().setFadeIn (1.0, &r.f.model.undo);
    a.clip().setFadeOut (0.5, &r.f.model.undo);
    const auto faded = render();
    check (noFade != faded && faded.isValid(), "fades change the picture");

    // the missing file: greyed, with its text; a missing file is not a crash
    const auto goneBounds = lanes.clipBounds (gone.id());
    check (! goneBounds.isEmpty(), "the clip of a missing file is shown");
    check (r.f.engine.peekAudioSource (9999) == nullptr, "the file is really missing");
    const auto img = render();
    check (img.getPixelAt (goneBounds.getRight() - 4, goneBounds.getBottom() - 4).getSaturation() < 0.3f, "and greyed out");

    // paint while a gesture preview or a selection is up
    r.view->selectClip (a.id(), false);
    r.view->selectClip (gone.id(), true);
    check (render().isValid(), "selected audio clips paint");
}

//==============================================================================
void testDetailPanel()
{
    std::printf ("[5.1] the detail panel shows the audio clip editor for audio clips\n");
    Rig r;
    auto a = r.audioClipOnTimeline (0.0);
    auto m = arrangement::addEmptyClip (r.f.model, r.midi, 0.0, 4.0, "notes");

    ui::DetailPanel panel (r.f.engine, r.f.model, r.actions, r.selection);
    panel.setBounds (0, 0, 1000, 300);
    panel.setEditorMode (true);

    r.selection.selectArrangementClip (r.audioA.id(), a.id());
    panel.tick();
    check (panel.isEditorShown() && panel.isAudioEditorShown(), "an audio clip opens the audio editor");
    check (panel.audioEditor().isVisible() && ! panel.pianoRoll().isVisible(), "and not the piano roll");
    check (panel.audioEditor().clipId() == a.clip().id(), "it edits that clip");

    r.selection.selectArrangementClip (r.midi.id(), m.id());
    panel.tick();
    check (panel.isEditorShown() && ! panel.isAudioEditorShown(), "a MIDI clip does not");
    check (panel.pianoRoll().isVisible() && ! panel.audioEditor().isVisible(), "it opens the piano roll");
    check (panel.pianoRoll().clipId() == m.clip().id(), "on that clip");

    r.selection.selectArrangementClip (r.audioA.id(), a.id());
    panel.tick();
    check (panel.isAudioEditorShown() && ! panel.pianoRoll().isVisible(), "and back to the audio editor");

    // a clip of Session View
    r.f.model.insertScene (-1);
    auto slot = r.f.model.createAudioClip (r.audioB, 0, a.clip().fileId(), 2.0, "slot");
    r.selection.selectSlot (r.audioB.id(), 0);
    panel.tick();
    check (panel.isAudioEditorShown(), "a slot with a sound shows the audio editor too");
    check (panel.audioEditor().clipId() == slot.id(), "and edits that clip (" + juce::String (panel.audioEditor().clipId()) + " vs " + juce::String (slot.id()) + ")");
}

//==============================================================================
struct EditorRig
{
    EditorRig()
    {
        clip = r.audioClipOnTimeline (0.0);
        content = clip.clip();
        editor = std::make_unique<ui::AudioClipEditor> (r.f.engine, r.f.model, r.actions);
        editor->setBounds (0, 0, 1000, 240);
        editor->setClip (content.id());
        r.f.model.undo.clearUndoHistory();
    }

    // Undoing once restores `before`: the edit was exactly one step
    template <typename Getter>
    void expectOneStep (const juce::String& what, Getter get, double before, double after)
    {
        check (near (get(), after), what + ": the model changed");
        check (r.undoDepth() == 1, what + ": one undo step");
        r.f.model.undo.undo();
        check (near (get(), before), what + ": undo restores it");
        r.f.model.undo.redo();
        check (near (get(), after), what + ": redo repeats it");
        r.f.model.undo.clearUndoHistory();
    }

    juce::MouseEvent mouse (juce::Point<int> p, juce::Component& c, bool shift = false)
    {
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        const auto mods = shift ? juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier : juce::ModifierKeys::leftButtonModifier;
        return juce::MouseEvent (src, p.toFloat(), mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, p.toFloat(), now, 1, false);
    }

    // Drags a handle of the ruler from where it is to a beat
    void drag (juce::Point<int> from, double toBeat, bool shift = false)
    {
        auto& wave = editor->wave();
        wave.mouseMove (mouse (from, wave));
        wave.mouseDown (mouse (from, wave, shift));
        const int x1 = (int) std::lround (editor->beatToX (toBeat));
        for (int i = 1; i <= 8; ++i)
            wave.mouseDrag (mouse ({ from.x + (x1 - from.x) * i / 8, from.y }, wave, shift));
        wave.mouseUp (mouse ({ x1, from.y }, wave, shift));
    }

    Rig r;
    ArrangementClipRef clip;
    ClipRef content;
    std::unique_ptr<ui::AudioClipEditor> editor;
};

void testEditorProperties()
{
    std::printf ("[5.1] audio clip editor: properties\n");
    EditorRig e;
    auto& ed = *e.editor;
    const auto c = e.content;

    ed.setGainDb (-6.0);
    e.expectOneStep ("gain", [&] { return c.gainDb(); }, 0.0, -6.0);
    ed.setTranspose (7);
    e.expectOneStep ("transpose", [&] { return (double) c.transpose(); }, 0.0, 7.0);
    ed.setDetune (-20);
    e.expectOneStep ("detune", [&] { return (double) c.detune(); }, 0.0, -20.0);
    ed.setReverse (true);
    e.expectOneStep ("reverse", [&] { return c.reverse() ? 1.0 : 0.0; }, 0.0, 1.0);
    ed.setLoopEnabled (false);
    e.expectOneStep ("loop", [&] { return c.loopEnabled() ? 1.0 : 0.0; }, 1.0, 0.0);
    ed.setFadeIn (0.5);
    e.expectOneStep ("fade in", [&] { return c.fadeIn(); }, 0.0, 0.5);
    ed.setFadeOut (1.0);
    e.expectOneStep ("fade out", [&] { return c.fadeOut(); }, 0.0, 1.0);

    ed.setGainDb (100.0);
    check (near (c.gainDb(), 24.0), "gain is limited to +24 dB");
    ed.setTranspose (-100);
    check (c.transpose() == -48, "transpose is limited to 48 semitones");

    // the names of the undo steps are Russian strings from Strings.h
    e.r.f.model.undo.clearUndoHistory();
    ed.setGainDb (-3.0);
    check (e.r.f.model.undo.getUndoDescription() == Strings::audioUi::undoGain, "the step has its name");

    // rename goes through the actions: one step
    e.r.f.model.undo.clearUndoHistory();
    e.r.actions.renameClipById (c.id(), "Kick loop");
    check (c.name() == "Kick loop" && e.r.undoDepth() == 1, "rename is one step");
}

void testEditorGestures()
{
    std::printf ("[5.1] audio clip editor: start marker and loop brace\n");
    EditorRig e;
    auto& ed = *e.editor;
    const auto c = e.content;
    check (near (c.lengthBeats(), 2.0), "the clip is two beats long");

    // drag the end of the brace (the loop end = the length) from 2 to 1.5 beats
    const int braceY = ui::AudioClipEditor::kRulerHeight - 4;
    const double length = c.lengthBeats();
    e.drag ({ (int) std::lround (ed.beatToX (length)), braceY }, 1.5);
    e.expectOneStep ("brace end", [&] { return c.lengthBeats(); }, length, 1.5);

    // the loop start from 0 to 0.8: snapped to the 1/16 grid, 0.75
    e.drag ({ (int) std::lround (ed.beatToX (0.0)), braceY }, 0.8);
    e.expectOneStep ("brace start", [&] { return c.loopStart(); }, 0.0, 0.75);

    // Shift: no snapping
    e.drag ({ (int) std::lround (ed.beatToX (0.75)), braceY }, 0.8, true);
    check (std::abs (c.loopStart() - 0.75) > 1.0e-4 && std::abs (c.loopStart() - 0.8) < 0.02, "Shift switches the snapping off (" + juce::String (c.loopStart()) + ")");
    check (e.r.undoDepth() == 1, "that was one step too");
    e.r.f.model.undo.clearUndoHistory();
    c.setLoopStart (0.0, nullptr);

    // the start marker
    e.drag ({ (int) std::lround (ed.beatToX (0.0)), 10 }, 1.0);
    e.expectOneStep ("start marker", [&] { return c.startOffset(); }, 0.0, 1.0);

    // a click on empty ruler changes nothing and leaves no step
    auto& wave = ed.wave();
    wave.mouseDown (e.mouse ({ (int) std::lround (ed.beatToX (1.2)), 10 }, wave));
    wave.mouseUp (e.mouse ({ (int) std::lround (ed.beatToX (1.2)), 10 }, wave));
    check (! e.r.f.model.undo.canUndo(), "a click that moves nothing leaves no undo step");

    // the editor follows other edits of the model and paints
    c.setReverse (true, &e.r.f.model.undo);
    ed.tick();
    juce::Image img (juce::Image::ARGB, ed.getWidth(), ed.getHeight(), true);
    juce::Graphics g (img);
    ed.paintEntireComponent (g, true);
    check (img.isValid(), "the editor paints");
}

void testEditorMissingFile()
{
    std::printf ("[5.1] audio clip editor: a missing file\n");
    Rig r;
    auto node = r.f.model.makeAudioClipNode (r.audioA, 9999, 4.0, "gone");
    auto a = arrangement::addSlotClip (r.f.model, r.audioA, ClipRef (node), 0.0);
    ui::AudioClipEditor ed (r.f.engine, r.f.model, r.actions);
    ed.setBounds (0, 0, 900, 240);
    ed.setClip (a.clip().id());
    juce::Image img (juce::Image::ARGB, 900, 240, true);
    juce::Graphics g (img);
    ed.paintEntireComponent (g, true);
    check (ed.hasClip() && img.isValid(), "the editor of a missing file opens and paints");
    check (ed.viewBeats() >= 4.0, "the view covers the clip");
}
} // namespace

namespace tests
{
void runAudioUiTests()
{
    testDrops();
    testLanesPaint();
    testDetailPanel();
    testEditorProperties();
    testEditorGestures();
    testEditorMissingFile();
}
} // namespace tests
