// Stage 2A of docs/spec/02-midi-recording.md: the clip editor and scales.
//   test 11  piano roll model: moving, resizing and copying notes keeps the clip sorted; a gesture is one transaction
//   test 15  transposition: the scale step (+1 in C major: E -> F, F -> G), TRACK.transpose on live and clip notes
// plus the other model and engine pieces of tasks 2.4, 2.7 and 2.8: quantise, split and join, project scale and
// tuning, the loop of a clip in the engine.
#include "TestSupport.h"

#include "../app/Source/Strings.h"
#include "../app/Source/model/ClipEdit.h"
#include "../app/Source/model/ProjectActions.h"

#include <array>
#include <atomic>

using namespace tests;

namespace
{
bool sortedClip (const ClipRef& clip)
{
    for (int i = 1; i < clip.numNotes(); ++i)
    {
        const auto a = clip.note (i - 1), b = clip.note (i);
        if (b.start < a.start || (! (a.start < b.start) && b.pitch < a.pitch))
            return false;
    }
    return true;
}

// Instrument that remembers the notes it was sent (fixed storage: the audio thread must not allocate).
class RecordingProcessor final : public juce::AudioProcessor
{
public:
    RecordingProcessor() : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)) {}

    using AudioProcessor::processBlock;
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        for (const auto meta : midi)
        {
            const auto status = meta.data[0] & 0xF0;
            const bool on = status == 0x90 && meta.data[2] > 0;
            const bool off = status == 0x80 || (status == 0x90 && meta.data[2] == 0);
            if (! on && ! off)
                continue;
            const int n = count.load (std::memory_order_relaxed);
            if (n < (int) pitch.size())
            {
                pitch[(size_t) n] = meta.data[1];
                isOn[(size_t) n] = on ? 1 : 0;
                count.store (n + 1, std::memory_order_relaxed);
            }
        }
        buffer.clear();
    }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "Recorder"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    void reset() override { count.store (0); }
    // The note-ons so far, in order.
    std::vector<int> ons() const
    {
        std::vector<int> out;
        for (int i = 0; i < count.load(); ++i)
            if (isOn[(size_t) i] != 0)
                out.push_back (pitch[(size_t) i]);
        return out;
    }
    std::vector<int> offs() const
    {
        std::vector<int> out;
        for (int i = 0; i < count.load(); ++i)
            if (isOn[(size_t) i] == 0)
                out.push_back (pitch[(size_t) i]);
        return out;
    }

    std::atomic<int> count { 0 };
    std::array<uint8_t, 4096> pitch {}, isOn {};
};

//==============================================================================
void testPianoRollModel()
{
    std::printf ("[test 11] piano roll model: move, resize, copy keep the clip sorted; a gesture is one transaction\n");
    auto model = ProjectModel::createEmpty();
    model.undo.beginNewTransaction ("setup");
    model.insertScene (-1);
    const auto track = model.addTrack ("midi");
    auto clip = model.createClip (track, 0, 8.0, "Riff");
    std::vector<juce::ValueTree> nodes;
    for (int i = 0; i < 16; ++i)
        nodes.push_back (clip.addNote ({ i * 0.5, 0.25, 48 + (i * 5) % 12, 80 }, &model.undo));
    check (sortedClip (clip) && clip.numNotes() == 16, "setup: 16 sorted notes");

    model.undo.clearUndoHistory();
    const auto before = model.root.toXmlString();

    // Gesture of 50 steps: drag the first four notes right and up in 50 small steps, one transaction.
    const clipedit::Nodes dragged { nodes[0], nodes[1], nodes[2], nodes[3] };
    std::vector<daw::engine::Note> originals;
    for (const auto& n : dragged)
        originals.push_back (ClipRef::readNote (n));
    model.undo.beginNewTransaction ("drag");
    bool sortedDuring = true;
    for (int step = 1; step <= 50; ++step)
    {
        for (size_t i = 0; i < dragged.size(); ++i)
        {
            auto n = originals[i];
            n.start += step * 0.1;
            n.pitch += step % 7;
            clip.updateNote (dragged[i], n, &model.undo);
        }
        sortedDuring = sortedDuring && sortedClip (clip);
    }
    check (sortedDuring, "the clip is sorted after every step of the drag");
    check (clip.numNotes() == 16, "no note was lost or duplicated by the drag");
    check (std::abs (ClipRef::readNote (dragged[0]).start - 5.0) < 1e-9, "the node followed the drag (identity kept)");
    check (model.undo.getNumActionsInCurrentTransaction() > 0, "the drag was recorded");
    check (model.undo.undo(), "one undo");
    check (model.root.toXmlString() == before, "one undo undoes the whole 50-step gesture");
    check (! model.undo.canUndo(), "and nothing else was in the history");
    model.undo.redo();
    check (std::abs (ClipRef::readNote (dragged[0]).start - 5.0) < 1e-9 && sortedClip (clip), "redo puts the gesture back");
    model.undo.undo();

    // resize
    model.undo.beginNewTransaction ("resize");
    for (int step = 1; step <= 20; ++step)
        clipedit::resizeNotes (clip, dragged, 0.05, 1.0 / 16.0, &model.undo);
    check (std::abs (ClipRef::readNote (dragged[2]).length - 1.25) < 1e-9, "resize adds up");
    clipedit::resizeNotes (clip, dragged, -10.0, 1.0 / 16.0, &model.undo);
    check (std::abs (ClipRef::readNote (dragged[2]).length - 1.0 / 16.0) < 1e-9, "a note keeps a minimum length");
    model.undo.undo();
    check (model.root.toXmlString() == before, "the resize gesture undoes in one step");

    // copy (Alt+drag): the copies are new nodes, the originals stay, the clip stays sorted
    model.undo.beginNewTransaction ("copy");
    const auto copies = clipedit::duplicateNotes (clip, dragged, 0.3, &model.undo);
    check (copies.size() == 4 && clip.numNotes() == 20 && sortedClip (clip), "copies are added and sorted");
    check (clip.hasNote (dragged[0]) && std::abs (ClipRef::readNote (copies[0]).start - ClipRef::readNote (dragged[0]).start - 0.3) < 1e-9, "the original stays, the copy is offset");
    clipedit::shiftNotes (clip, copies, 4.0, 3, &model.undo);
    check (sortedClip (clip) && clip.numNotes() == 20, "moving the copies keeps the order");
    model.undo.undo();
    check (model.root.toXmlString() == before, "the copy gesture undoes in one step");

    // moves are limited to the legal range
    model.undo.beginNewTransaction ("limits");
    const int applied = clipedit::shiftNotes (clip, clipedit::allNodes (clip), -100.0, 200, &model.undo);
    check (applied <= 127 && clip.note (0).start >= 0.0, "notes stay inside 0..127 and not before the clip start");
    for (int i = 0; i < clip.numNotes(); ++i)
        check (clip.note (i).pitch >= 0 && clip.note (i).pitch <= 127, "pitch in range");
    model.undo.undo();

    // delete, split, join
    model.undo.beginNewTransaction ("split");
    const auto parts = clipedit::splitNotes (clip, { nodes[8] }, ClipRef::readNote (nodes[8]).start + 0.1, &model.undo);
    check (parts.size() == 1 && clip.numNotes() == 17 && sortedClip (clip), "split makes two notes");
    check (std::abs (ClipRef::readNote (nodes[8]).length - 0.1) < 1e-9 && std::abs (ClipRef::readNote (parts[0]).length - 0.15) < 1e-9, "split lengths add up");
    check (clipedit::splitNotes (clip, { nodes[8] }, 100.0, &model.undo).empty(), "splitting outside a note does nothing");
    const auto joined = clipedit::joinNotes (clip, { nodes[8], parts[0] }, &model.undo);
    check (joined.size() == 1 && clip.numNotes() == 16 && std::abs (ClipRef::readNote (joined[0]).length - 0.25) < 1e-9, "join restores the note");
    model.undo.undo();
    check (model.root.toXmlString() == before, "split and join undo in one step");

    model.undo.beginNewTransaction ("delete");
    clipedit::deleteNotes (clip, { nodes[0], nodes[1] }, &model.undo);
    check (clip.numNotes() == 14 && ! clip.hasNote (nodes[0]), "delete removes the notes");
    check (clipedit::onlyValid (clip, { nodes[0], nodes[5] }).size() == 1, "a deleted node is no longer valid");
    model.undo.undo();
    check (clip.numNotes() == 16, "undo brings them back");

    // velocity
    model.undo.beginNewTransaction ("velocity");
    clipedit::setVelocity (clip, nodes[4], 300, &model.undo);
    check (ClipRef::readNote (nodes[4]).velocity == 127, "velocity is limited to 127");
    clipedit::setVelocity (clip, nodes[4], -5, &model.undo);
    check (ClipRef::readNote (nodes[4]).velocity == 1, "and to 1");

    // a note beyond the end lengthens the clip to whole bars
    check (std::abs (clipedit::lengthToFit (clip, 8.0, 4.0) - 8.0) < 1e-9, "fits: unchanged");
    check (std::abs (clipedit::lengthToFit (clip, 8.1, 4.0) - 12.0) < 1e-9, "a note past the end adds a bar");
    check (std::abs (clipedit::lengthToFit (clip, 8.1, 3.0) - 9.0) < 1e-9, "bars of 3 beats");
}

void testClipEditOperations()
{
    std::printf ("[test] clip editing: quantise, transpose along the scale, scale of a clip\n");
    auto model = ProjectModel::createEmpty();
    model.undo.beginNewTransaction ("setup");
    model.insertScene (-1);
    const auto track = model.addTrack ("midi");
    auto clip = model.createClip (track, 0, 4.0, "Q");
    const auto a = clip.addNote ({ 0.06, 0.2, 60, 100 }, &model.undo);
    const auto b = clip.addNote ({ 0.55, 0.2, 64, 100 }, &model.undo);
    const auto c = clip.addNote ({ 1.1, 0.2, 65, 100 }, &model.undo);

    // quantise acts on the selection, or on every note when nothing is selected
    model.undo.beginNewTransaction ("quantise");
    clipedit::quantizeNotes (clip, clipedit::selectionOrAll (clip, { b }), 0.5, 1.0, 0.0, false, &model.undo);
    check (std::abs (ClipRef::readNote (b).start - 0.5) < 1e-9 && std::abs (ClipRef::readNote (a).start - 0.06) < 1e-9, "only the selected note was quantised");
    check (clipedit::selectionOrAll (clip, {}).size() == 3, "an empty selection means all notes");
    clipedit::quantizeNotes (clip, clipedit::selectionOrAll (clip, {}), 0.5, 1.0, 0.5, false, &model.undo);
    check (std::abs (ClipRef::readNote (a).start) < 1e-9 && std::abs (ClipRef::readNote (b).start - 0.625) < 1e-9
               && std::abs (ClipRef::readNote (c).start - 1.0) < 1e-9,
           "all notes: the second eighth moved by the swing");
    model.undo.undo();

    // transposition along the scale (spec test 15, the clip half): +1 in C major moves E to F and F to G
    clipedit::Scale cMajor { 0, daw::music::findScale ("major") };
    auto e = clip.addNote ({ 2.0, 1.0, 64, 100 }, &model.undo), f = clip.addNote ({ 3.0, 1.0, 65, 100 }, &model.undo);
    model.undo.beginNewTransaction ("transpose");
    clipedit::transposeNotes (clip, { e, f }, 1, cMajor, &model.undo);
    check (ClipRef::readNote (e).pitch == 65 && ClipRef::readNote (f).pitch == 67, "+1 in C major: E -> F, F -> G");
    clipedit::transposeNotes (clip, { e, f }, -1, cMajor, &model.undo);
    check (ClipRef::readNote (e).pitch == 64 && ClipRef::readNote (f).pitch == 65, "-1 in C major: F -> E, G -> F");
    clipedit::transposeNotes (clip, { e, f }, 12, cMajor, &model.undo);
    check (ClipRef::readNote (e).pitch == 76 && ClipRef::readNote (f).pitch == 77, "+12 is an octave");
    clipedit::transposeNotes (clip, { e, f }, -12, cMajor, &model.undo);
    clipedit::transposeNotes (clip, { e, f }, 1, clipedit::Scale {}, &model.undo);
    check (ClipRef::readNote (e).pitch == 65 && ClipRef::readNote (f).pitch == 66, "without a scale +1 is one semitone");
    model.undo.undo();
    check (ClipRef::readNote (e).pitch == 64, "the transposition is one transaction");

    // notes that cannot move stay
    auto top = clip.addNote ({ 3.5, 0.2, 127, 100 }, &model.undo);
    clipedit::transposeNotes (clip, { top }, 12, cMajor, &model.undo);
    check (ClipRef::readNote (top).pitch == 127, "a note at the top does not wrap");

    // the scale of a project, and of a clip that follows it
    model.undo.beginNewTransaction ("scale");
    model.setScale (9, "minor");
    check (model.scaleRoot() == 9 && model.scaleMode() == "minor", "project scale is stored");
    auto fresh = model.createClip (model.addTrack ("midi"), 0, 4.0, "New");
    check (! fresh.ownScale() && fresh.scaleRoot() == 9 && fresh.scaleMode() == "minor", "a new clip starts with the project's scale");
    auto eff = clipedit::effectiveScale (fresh, model.scaleRoot(), model.scaleMode());
    check (eff.root == 9 && eff.scale != nullptr && juce::String (eff.scale->key) == "minor", "a clip without its own scale shows the project's");
    model.setScale (2, "dorian");
    eff = clipedit::effectiveScale (fresh, model.scaleRoot(), model.scaleMode());
    check (eff.root == 2 && juce::String (eff.scale->key) == "dorian", "and follows it when it changes");
    fresh.setOwnScale (true, &model.undo);
    fresh.setScale (5, "major", &model.undo);
    eff = clipedit::effectiveScale (fresh, model.scaleRoot(), model.scaleMode());
    check (eff.root == 5 && juce::String (eff.scale->key) == "major", "own scale wins");
    model.setScale (0, "");
    check (clipedit::effectiveScale (clip, model.scaleRoot(), model.scaleMode()).scale == nullptr, "scale off");

    // tuning and the quantise settings live in the project
    check (std::abs (model.tuningA4() - 440.0) < 1e-9, "tuning defaults to 440 Hz");
    model.setTuningA4 (432.0);
    check (std::abs (model.tuningA4() - 432.0) < 1e-9, "tuning is stored");
    model.setTuningA4 (1000.0);
    check (std::abs (model.tuningA4() - 466.0) < 1e-9, "and limited to 415..466");
    model.setTuningA4 (100.0);
    check (std::abs (model.tuningA4() - 415.0) < 1e-9, "lower limit");
    auto q = model.quantiseSettings();
    check (std::abs (q.grid - 0.25) < 1e-9 && std::abs (q.strength - 1.0) < 1e-9 && std::abs (q.swing) < 1e-9 && ! q.ends, "quantise defaults");
    q.grid = 0.5;
    q.swing = 0.5;
    q.ends = true;
    model.setQuantiseSettings (q);
    const auto q2 = model.quantiseSettings();
    check (std::abs (q2.grid - 0.5) < 1e-9 && std::abs (q2.swing - 0.5) < 1e-9 && q2.ends, "quantise settings are remembered");
    model.setSignature (6, 8);
    check (std::abs (model.beatsPerBar() - 3.0) < 1e-9, "6/8 is three beats");

    // the file round trip keeps all of it
    const auto copy = juce::ValueTree::fromXml (model.root.toXmlString());
    ProjectModel loaded { copy };
    check (std::abs (loaded.tuningA4() - 415.0) < 1e-9 && std::abs (loaded.quantiseSettings().swing - 0.5) < 1e-9 && loaded.sigNum() == 6, "saved and loaded");
    check (loaded.findClip (fresh.id()).ownScale() && loaded.findClip (fresh.id()).scaleRoot() == 5, "the scale of a clip is saved");
}

//==============================================================================
double scriptedMs = 1000.0;
double scriptedClock() { return scriptedMs; }

void testTransposeAndTuningInEngine()
{
    std::printf ("[test 15] TRACK.transpose moves live notes and clip notes; Prisma gets the project tuning\n");
    Fixture f;
    auto& model = f.model;
    auto& engine = f.engine;

    RecordingProcessor* recorder = nullptr;
    engine.setDeviceFactory ([&recorder] (const DeviceRef&) -> std::unique_ptr<juce::AudioProcessor> {
        auto p = std::make_unique<RecordingProcessor>();
        recorder = p.get();
        return p;
    });
    model.insertScene (-1);
    const auto track = model.addTrack ("midi");
    model.addDevice (track, "prisma", "Recorder");
    auto clip = model.createClip (track, 0, 4.0, "Notes");
    clip.addNote ({ 0.0, 0.5, 60, 100 }, nullptr);
    clip.addNote ({ 1.0, 0.5, 64, 100 }, nullptr);
    model.setSelectedTrack (track.id());

    constexpr int kBlock = 256;
    FakeDevice fake (kBlock, 48000.0);
    engine.audioDeviceAboutToStart (&fake);
    engine.rebuildGraphIfNeeded();
    engine.setClockForTesting (&scriptedClock);
    Runner runner { engine, kBlock };
    check (recorder != nullptr, "the recorder is the instrument");

    // live note, no transposition
    const uint8_t on60[3] { 0x90, 60, 100 }, off60[3] { 0x80, 60, 0 };
    scriptedMs = 1000.0;
    runner.run (1);
    engine.pushLiveMidi (on60, 3, true, scriptedMs + 1.0);
    scriptedMs += 6.0;
    runner.run (1);
    engine.pushLiveMidi (off60, 3, true, scriptedMs + 1.0);
    scriptedMs += 6.0;
    runner.run (1);
    check (recorder->ons() == std::vector<int> { 60 } && recorder->offs() == std::vector<int> { 60 }, "no transposition: C3 plays C3");

    // +12
    model.undo.beginNewTransaction ("transpose");
    track.setTranspose (12, &model.undo);
    recorder->reset();
    engine.pushLiveMidi (on60, 3, true, scriptedMs + 1.0);
    scriptedMs += 6.0;
    runner.run (1);
    // the transposition changes while the key is held: the note-off must still end the note that began
    track.setTranspose (-7, &model.undo);
    engine.pushLiveMidi (off60, 3, true, scriptedMs + 1.0);
    scriptedMs += 6.0;
    runner.run (1);
    check (recorder->ons() == std::vector<int> { 72 }, "transpose +12: the live C3 sounds an octave higher");
    check (recorder->offs() == std::vector<int> { 72 }, "the note-off follows the note, even though the transposition changed in between");

    // clip notes with +12
    track.setTranspose (12, &model.undo);
    recorder->reset();
    engine.launchClip (track.id(), 0);
    runner.run (200); // 200 blocks of 256 = 1.07 s = 2.1 beats at 120 BPM
    const auto ons = recorder->ons();
    check (ons.size() >= 2 && ons[0] == 72 && ons[1] == 76, "transpose +12: the clip's notes play an octave higher (72, 76)");
    check (recorder->offs().size() >= 1 && recorder->offs()[0] == 72, "and end on the same pitch");

    // limits: -48..+48 and the range of MIDI notes
    track.setTranspose (48, &model.undo);
    recorder->reset();
    runner.run (400);
    bool allValid = true;
    for (int i = 0; i < recorder->count.load(); ++i)
        allValid = allValid && recorder->pitch[(size_t) i] <= 127;
    check (allValid, "notes are never sent outside 0..127");
    track.setTranspose (0, &model.undo);
    engine.stop();
    runner.run (4);

    // a clip with the loop off plays once and lets go of the slot
    {
        model.undo.beginNewTransaction ("one shot");
        clip.setLoopEnabled (false, &model.undo);
        clip.setLengthBeats (2.0, &model.undo);
        engine.rebuildGraphIfNeeded();
        recorder->reset();
        engine.launchClip (track.id(), 0);
        runner.run (400); // 2.1 s = 4.3 beats: more than one pass of 2 beats
        const auto* rt = engine.findRuntime (track.id());
        check (recorder->ons().size() == 2, "loop off: the two notes play once, got " + juce::String ((int) recorder->ons().size()));
        check (rt != nullptr && rt->playingSlot.load() == -1, "loop off: the slot stops playing after the clip's end");
        check (recorder->ons().size() == recorder->offs().size(), "no hanging notes");
        engine.stop();
        runner.run (4);

        // loop on with a brace and a start point
        model.undo.beginNewTransaction ("loop");
        clip.setLoopEnabled (true, &model.undo);
        clip.setLengthBeats (4.0, &model.undo);
        clip.setLoopStart (1.0, &model.undo);
        clip.setStartOffset (1.0, &model.undo);
        engine.rebuildGraphIfNeeded();
        recorder->reset();
        engine.launchClip (track.id(), 0);
        runner.run (700); // 3.7 s = 7.5 beats
        const auto ons2 = recorder->ons();
        // starts at beat 1 of the clip (note 64), then the brace 1..4 repeats: 64 every 3 beats
        check (ons2.size() >= 3 && ons2[0] == 64 && ons2[1] == 64 && ons2[2] == 64, "start point and brace: only the note inside the brace repeats");
        engine.stop();
        runner.run (4);
    }

    // tuning reaches the Prisma instances
    engine.setDeviceFactory (nullptr);
    Fixture g;
    g.model.insertScene (-1);
    auto t2 = addPrismaTrack (g.model, 0);
    g.engine.rebuildGraphIfNeeded();
    auto* prisma = dynamic_cast<PrismaProcessor*> (g.engine.findProcessor (t2.device (0).id()));
    check (prisma != nullptr && std::abs (prisma->getTuneA4() - 440.0f) < 1e-3f, "Prisma starts at 440 Hz");
    g.model.undo.beginNewTransaction ("tuning");
    g.model.setTuningA4 (432.0);
    check (prisma != nullptr && std::abs (prisma->getTuneA4() - 432.0f) < 1e-3f, "the project tuning is given to every Prisma at once");
    auto t3 = addPrismaTrack (g.model, 1);
    g.engine.rebuildGraphIfNeeded();
    auto* second = dynamic_cast<PrismaProcessor*> (g.engine.findProcessor (t3.device (0).id()));
    check (second != nullptr && std::abs (second->getTuneA4() - 432.0f) < 1e-3f, "a Prisma created later starts with the project tuning");
    g.model.undo.undo();
    check (prisma != nullptr && std::abs (prisma->getTuneA4() - 440.0f) < 1e-3f, "undo restores 440 Hz");

    // the tuning is not part of the plugin state and not a parameter
    juce::MemoryBlock state;
    prisma->setTuneA4 (415.0f);
    prisma->getStateInformation (state);
    PrismaProcessor other;
    other.setStateInformation (state.getData(), (int) state.getSize());
    check (std::abs (other.getTuneA4() - 440.0f) < 1e-3f, "tuneA4 is not saved in the state of the plugin");
    engine.audioDeviceStopped();
}
} // namespace

void tests::runEditorTests()
{
    testPianoRollModel();
    testClipEditOperations();
    testTransposeAndTuningInEngine();
}
