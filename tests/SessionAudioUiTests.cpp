// Stage 5, the Session View side of audio: dropping sound files, the new-audio-track command, painting audio clips.
#include "TestSupport.h"

#include "../app/Source/Strings.h"
#include "../app/Source/browser/BrowserActions.h"
#include "../app/Source/model/ProjectActions.h"
#include "../app/Source/project/AudioImport.h"
#include "../app/Source/ui/EditCommands.h"
#include "../app/Source/ui/SessionView.h"
#include "../app/Source/ui/Theme.h"

#include <cmath>
#include <set>

using namespace tests;

namespace
{
constexpr double kRate = 48000.0;

bool writeWav (const juce::File& file, int frames, double hz)
{
    file.deleteFile();
    file.getParentDirectory().createDirectory();
    std::unique_ptr<juce::OutputStream> out (file.createOutputStream().release());
    if (out == nullptr)
        return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (out.get(), kRate, 1u, 32, {}, 0));
    if (writer == nullptr)
        return false;
    out.release();
    juce::AudioBuffer<float> buffer (1, frames);
    for (int i = 0; i < frames; ++i)
        buffer.setSample (0, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / kRate));
    return writer->writeFromAudioSampleBuffer (buffer, 0, frames);
}

browser::DragItem soundDrag (const juce::File& f)
{
    browser::DragItem d;
    d.kind = browser::kinds::audioFile;
    d.path = f.getFullPathName();
    d.name = f.getFileNameWithoutExtension();
    return d;
}

juce::File makeTempFolder()
{
    auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("daw-audioui-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
    folder.createDirectory();
    return folder;
}

struct UiAudioRig
{
    UiAudioRig() : folder (makeTempFolder()), library (folder.getChildFile ("lib")), actions (f.model, f.engine, library)
    {
        f.engine.setProjectFolder (folder.getChildFile ("Song"));
        f.engine.getProjectFolder().createDirectory();
        for (int i = 0; i < 4; ++i)
            f.model.insertScene (-1);
        midi = addPrismaTrack (f.model, 0);
        audio = f.model.addTrack ("audio");
        for (int i = 0; i < 3; ++i)
        {
            sounds.add (folder.getChildFile ("in").getChildFile ("loop" + juce::String (i + 1) + ".wav"));
            writeWav (sounds.getReference (i), 24000, 220.0 * (i + 1));
        }
        f.model.undo.clearUndoHistory();
        f.engine.rebuildGraphIfNeeded();
    }
    ~UiAudioRig() { folder.deleteRecursively(); }

    juce::String xml() const { return f.model.root.toXmlString(); }
    int audioTracks() const
    {
        int n = 0;
        for (const auto& t : f.model.tracks())
            n += t.isAudio() ? 1 : 0;
        return n;
    }

    // Runs the drop and checks that it was exactly one undo step that undo and redo bring back and forth.
    template <typename F>
    void oneStep (const juce::String& what, F&& action)
    {
        const auto before = xml();
        action();
        const auto after = xml();
        check (before != after, what + ": the project changed");
        f.model.undo.undo();
        check (xml() == before, what + ": one undo restores the project");
        f.model.undo.redo();
        check (xml() == after, what + ": redo repeats it");
        f.model.undo.undo();
        check (xml() == before, what + ": and undo again");
        f.model.undo.clearUndoHistory();
    }

    juce::File folder;
    Fixture f;
    library::UserLibrary library;
    browser::BrowserActions actions;
    TrackRef midi, audio;
    juce::Array<juce::File> sounds;
};

void testSoundDrops()
{
    std::printf("[audio ui test] sound files dropped on the Session View\n");
    UiAudioRig r;
    auto& model = r.f.model;
    using browser::DropTarget;

    check (r.actions.accepts (soundDrag (r.sounds[0]), DropTarget::newTrack), "a sound is accepted by empty space");
    check (r.actions.accepts (soundDrag (r.sounds[0]), DropTarget::slot, r.audio.id()) && r.actions.accepts (soundDrag (r.sounds[0]), DropTarget::track, r.audio.id()),
           "and by a slot and the header of an audio track");
    check (! r.actions.accepts (soundDrag (r.sounds[0]), DropTarget::slot, r.midi.id()) && ! r.actions.accepts (soundDrag (r.sounds[0]), DropTarget::track, r.midi.id()),
           "but not by a MIDI track");

    // three files on empty space: one audio track, one file per scene
    r.oneStep ("three sounds on empty space", [&] {
        const int tracksBefore = model.numTracks();
        const auto res = r.actions.dropAudioFiles (r.sounds, DropTarget::newTrack, -1, -1, false);
        check (res.ok && model.numTracks() == tracksBefore + 1, "one new track appears");
        const auto track = model.findTrack (res.trackId);
        check (track.isValid() && track.isAudio() && track.numDevices() == 0, "an audio track without devices");
        for (int s = 0; s < 3; ++s)
            check (track.clipAt (s).isValid() && track.clipAt (s).isAudio(), "scene " + juce::String (s) + " holds a sound");
        check (! track.clipAt (3).isValid(), "and the fourth scene is empty");
        check (model.undo.getUndoDescription() == Strings::undoAddAudioClips (3), "the transaction is named after the number of sounds");
    });

    // with Cmd (alternate): each on its own track
    r.oneStep ("three sounds with Cmd", [&] {
        const int tracksBefore = model.numTracks();
        const auto res = r.actions.dropAudioFiles (r.sounds, DropTarget::newTrack, -1, -1, true);
        check (res.ok && model.numTracks() == tracksBefore + 3, "three new tracks appear");
        int found = 0;
        for (int i = tracksBefore; i < model.numTracks(); ++i)
            found += model.trackAt (i).isAudio() && model.trackAt (i).clipAt (0).isValid() && ! model.trackAt (i).clipAt (1).isValid() ? 1 : 0;
        check (found == 3, "each holds one sound in the first scene");
    });

    // onto a slot of an existing audio track: from that scene down, and a scene is added when they do not fit
    r.oneStep ("sounds on a slot", [&] {
        const int scenes = model.numScenes();
        const auto res = r.actions.dropAudioFiles (r.sounds, DropTarget::slot, r.audio.id(), 2, false);
        check (res.ok && res.trackId == r.audio.id(), "the sounds go to the track that was hit");
        check (r.audio.clipAt (2).isValid() && r.audio.clipAt (3).isValid() && r.audio.clipAt (4).isValid() && ! r.audio.clipAt (1).isValid(), "in scenes 2, 3 and 4");
        check (model.numScenes() == scenes + 1, "a scene is added for the one that did not fit");
    });

    // onto the header: the first empty slot
    r.oneStep ("a sound on a header", [&] {
        const auto res = r.actions.dropAudioFiles ({ r.sounds[1] }, DropTarget::track, r.audio.id(), -1, false);
        check (res.ok && r.audio.clipAt (0).isValid(), "it takes the first empty slot");
    });

    // refusals: no change and no undo step
    {
        const auto before = r.xml();
        const auto onMidi = r.actions.dropAudioFiles (r.sounds, DropTarget::slot, r.midi.id(), 0, false);
        check (! onMidi.ok && onMidi.message.isNotEmpty() && r.xml() == before && ! model.undo.canUndo(), "a sound on a MIDI track is refused with a message and leaves no undo step");
        const auto viaDrop = r.actions.drop (soundDrag (r.sounds[0]), DropTarget::slot, r.midi.id(), 0);
        check (! viaDrop.ok && r.xml() == before, "also through the drag path");

        browser::DragItem preset;
        preset.kind = browser::kinds::prismaPreset;
        preset.id = "0";
        preset.name = "Init";
        preset.instrument = true;
        check (! r.actions.accepts (preset, DropTarget::track, r.audio.id()) && ! r.actions.accepts (preset, DropTarget::slot, r.audio.id()),
               "an instrument preset is not accepted by an audio track");
        const auto refused = r.actions.drop (preset, DropTarget::track, r.audio.id());
        check (! refused.ok && r.xml() == before && r.audio.numDevices() == 0 && ! model.undo.canUndo(), "and dropping it does nothing");
    }

    // double click / Enter in the browser
    r.oneStep ("a sound by double click with an audio track selected", [&] {
        const auto res = r.actions.useOnTrack (soundDrag (r.sounds[2]), r.audio.id());
        check (res.ok && res.trackId == r.audio.id(), "it goes to the selected audio track");
    });
    r.oneStep ("a sound by double click with a MIDI track selected", [&] {
        const int before = model.numTracks();
        const auto res = r.actions.useOnTrack (soundDrag (r.sounds[2]), r.midi.id());
        check (res.ok && model.numTracks() == before + 1 && model.findTrack (res.trackId).isAudio(), "it makes a new audio track");
    });
}

void testNewAudioTrackCommand()
{
    std::printf("[audio ui test] the new audio track command\n");
    Fixture f;
    ProjectActions actions (f.model, &f.engine);
    ui::SelectionState selection;
    ui::EditCommands commands (f.engine, f.model, actions, selection);
    f.model.insertScene (-1);
    f.model.insertScene (-1);
    commands.newTrack();
    f.model.undo.clearUndoHistory();
    const int before = f.model.numTracks();

    commands.newAudioTrack();
    check (f.model.numTracks() == before + 1, "a track appears");
    const auto track = f.model.trackAt (f.model.numTracks() - 1);
    check (track.type() == "audio" && track.isAudio() && track.numDevices() == 0, "of type audio, without devices");
    check (f.model.selectedTrack() == track.id() && selection.get().trackId == track.id(), "and it is selected");
    check (f.model.undo.getUndoDescription() == Strings::sessionAudio::undoNewAudioTrack, "the transaction has its own name");
    f.model.undo.undo();
    check (f.model.numTracks() == before, "one undo removes it");

    // after the selected track, like the MIDI command
    f.model.undo.clearUndoHistory();
    commands.newTrack();
    commands.newTrack();
    f.model.setSelectedTrack (f.model.trackAt (0).id());
    commands.newAudioTrack();
    check (f.model.trackAt (1).isAudio(), "it is inserted after the selected track");

    // an audio track takes no empty MIDI clips
    const auto audioId = f.model.trackAt (1).id();
    f.model.undo.clearUndoHistory();
    check (! actions.createEmptyClip (audioId, 0).isValid() && ! f.model.undo.canUndo(), "an empty MIDI clip is not made on an audio track");

    // clips do not cross between audio and MIDI tracks
    ProjectModel::AudioFileInfo info;
    info.path = "x.wav";
    info.lengthSamples = 1000;
    const auto file = f.model.addAudioFile (info);
    f.model.createAudioClip (f.model.findTrack (audioId), 0, file.id(), 2.0, "x");
    f.model.undo.clearUndoHistory();
    check (! actions.moveClip (audioId, 0, f.model.trackAt (0).id(), 0, false) && ! f.model.undo.canUndo(), "a sound cannot be moved to a MIDI track");
    actions.copyClip (audioId, 0);
    check (! actions.pasteClip (f.model.trackAt (0).id(), 0), "nor pasted there");
    check (actions.pasteClip (audioId, 1), "but it can be pasted to an audio track");
}

void testSessionViewPaintsAudio()
{
    std::printf("[audio ui test] the Session View paints audio clips and missing files\n");
    ThemeManager::get().reload();
    UiAudioRig r;
    auto& model = r.f.model;
    ProjectActions actions (model, &r.f.engine);
    ui::SelectionState selection;
    ui::EditCommands commands (r.f.engine, model, actions, selection);

    r.f.model.undo.beginNewTransaction ("fill");
    const auto a = audioimport::importFile (model, r.f.engine, r.sounds[0], true);
    const auto b = audioimport::importFile (model, r.f.engine, r.sounds[1], true);
    check (a.ok && b.ok, "two files are imported");
    model.createAudioClip (r.audio, 0, a.fileId, a.lengthBeats, a.name);
    model.createAudioClip (r.audio, 1, b.fileId, b.lengthBeats, b.name);
    r.f.engine.rebuildGraphIfNeeded();
    r.f.engine.getAudioPool().waitForLoads();

    ui::SessionView view (r.f.engine, model, actions, selection, commands);
    view.setBounds (0, 0, 1000, 520);
    view.tick();
    auto grab = [&] { return view.createComponentSnapshot (view.getLocalBounds(), false, 1.0f); };
    const auto column = [&] {
        for (int i = 0; i < view.numColumns(); ++i)
            if (model.trackAt (i).id() == r.audio.id())
                return view.columnBoundsInView (i);
        return juce::Rectangle<int>();
    }();
    check (! column.isEmpty(), "the audio track has a column");

    const auto withFiles = grab();
    check (withFiles.isValid(), "the view paints with sounds in it");
    const int cellTop = 43;
    auto distinctColours = [&] (const juce::Image& img, int scene) {
        std::set<juce::uint32> colours;
        const int y0 = cellTop + scene * (ui::SessionView::kSlotHeight + ui::SessionView::kGap);
        for (int y = y0 + 2; y < y0 + ui::SessionView::kSlotHeight - 2; ++y)
            for (int x = column.getX() + 28; x < column.getRight() - 4; ++x)
                colours.insert (img.getPixelAt (x, y).getARGB());
        return (int) colours.size();
    };
    check (distinctColours (withFiles, 0) > 3, "a sound cell shows a waveform (more than a flat fill)");

    // lose the first file: the clip is greyed and says so, and nothing crashes
    r.f.engine.resolveAudioFile (model.findAudioFile (a.fileId)).deleteFile();
    r.f.engine.setProjectFolder (r.folder.getChildFile ("Elsewhere")); // marks the graph dirty: the next rebuild forgets what the views remember
    r.f.engine.rebuildGraphIfNeeded();
    view.tick();
    check (r.f.engine.peekAudioSource (a.fileId) == nullptr && r.f.engine.peekAudioSource (b.fileId) != nullptr, "the engine reports the lost file and still has the other one");
    const auto withLoss = grab();
    check (withLoss.isValid(), "the view paints with a missing file");
    bool differs = false;
    for (int y = cellTop + 2; y < cellTop + ui::SessionView::kSlotHeight - 2 && ! differs; ++y)
        for (int x = column.getX() + 28; x < column.getRight() - 4 && ! differs; ++x)
            differs = withFiles.getPixelAt (x, y) != withLoss.getPixelAt (x, y);
    check (differs, "and the lost clip looks different from the one that plays");

    // Finder drops through the view: empty space makes an audio track; a MIDI-track slot refuses
    juce::StringArray paths;
    for (const auto& s : r.sounds)
        paths.add (s.getFullPathName());
    view.setBrowserActions (&r.actions);
    const int tracksBefore = model.numTracks();
    view.filesDropped (paths, 2, 2);
    check (model.numTracks() == tracksBefore + 1 && model.trackAt (model.numTracks() - 1).isAudio(), "files let go over empty space make one audio track");
    model.undo.undo();
    check (model.numTracks() == tracksBefore, "in one undo step");

    juce::String reported;
    view.onDropped = [&] (const browser::DropResult& res, const browser::DragItem&) { reported = res.message; };
    const auto before = r.xml();
    const int midiColumn = view.columnBoundsInView (0).getCentreX();
    view.filesDropped (paths, midiColumn, cellTop + 10);
    check (r.xml() == before && reported == Strings::errorAudioOnMidi, "files let go over a MIDI track change nothing and say why");
    view.filesDropped (paths, column.getCentreX(), cellTop + 3 * (ui::SessionView::kSlotHeight + ui::SessionView::kGap) + 10);
    check (r.audio.clipAt (3).isValid() && r.audio.clipAt (4).isValid(), "files let go over an audio slot fill it and the next scene");
}
} // namespace

void tests::runSessionAudioUiTests()
{
    testSoundDrops();
    testNewAudioTrackCommand();
    testSessionViewPaintsAudio();
}
