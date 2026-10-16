// Tests 6, 7 and 8 of docs/spec/01-layout-project.md: the lock-free playback graph.
//   6. The audio thread runs 2000 blocks while another thread edits the project; with DAW_RT_CHECKS the audio
//      thread must not touch the heap, and the output stays finite.
//   7. A playing clip is not restarted and loses no notes when the graph is rebuilt around it.
//   8. Retired graphs, runtimes and devices are deleted: after 100 rebuilds at most 3 graphs are alive.
#include "TestSupport.h"

#include "../app/Source/engine/RtCheck.h"

#include <atomic>
#include <thread>

using namespace tests;

namespace
{
// Instrument that counts the MIDI events it receives and plays nothing.
class CountingProcessor final : public juce::AudioProcessor
{
public:
    CountingProcessor() : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)) {}

    using AudioProcessor::processBlock;
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        for (const auto meta : midi)
        {
            const auto status = meta.data[0] & 0xF0;
            if (status == 0x90 && meta.data[2] > 0)
                noteOns.fetch_add (1, std::memory_order_relaxed);
            else if (status == 0x80 || status == 0x90)
                noteOffs.fetch_add (1, std::memory_order_relaxed);
        }
        buffer.clear();
    }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "Counter"; }
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

    std::atomic<int> noteOns { 0 }, noteOffs { 0 };
};

//==============================================================================
// Test 7
struct ContinuityResult
{
    int noteOns = 0, noteOffs = 0;
    int playingSlotAtEnd = -2;
    int64_t nonFinite = 0;
};

// Plays the 4-bar pad clip for exactly 4 bars. With `rebuild` the project is edited around the playing clip.
ContinuityResult playPadForFourBars (bool rebuild)
{
    Fixture f;
    auto& model = f.model;
    auto& engine = f.engine;

    std::map<int64_t, CountingProcessor*> counters;
    engine.setDeviceFactory ([&counters] (const DeviceRef& d) -> std::unique_ptr<juce::AudioProcessor> {
        auto p = std::make_unique<CountingProcessor>();
        counters[d.id()] = p.get();
        return p;
    });

    for (int i = 0; i < 3; ++i)
        model.insertScene (-1);
    const auto track = model.addTrack ("midi");
    const auto device = model.addDevice (track, "prisma", "Counter");
    auto clip = model.createClip (track, 0, 16.0, "Pad");
    clip.setNotes (daw::engine::demo::padClip().notes, nullptr);
    const auto trackId = track.id(), deviceId = device.id();

    FakeDevice fake;
    engine.audioDeviceAboutToStart (&fake);
    engine.rebuildGraphIfNeeded();
    Runner runner { engine };

    engine.launchScene (0);
    // 120 BPM at 48 kHz: 24000 samples per beat, 16 beats = 750 blocks of 512
    for (int block = 0; block < 750; ++block)
    {
        if (rebuild && block == 100)
        {
            // another track with its own clip appears
            model.undo.beginNewTransaction ("add track");
            const auto other = model.addTrack ("midi");
            model.addDevice (other, "prisma", "Counter");
            model.createClip (other, 1, 4.0, "Other");
            engine.rebuildGraphIfNeeded();
        }
        if (rebuild && block == 300)
        {
            // a scene is inserted above the playing one: the clip moves from slot 0 to slot 1
            model.undo.beginNewTransaction ("insert scene");
            model.insertScene (0);
            engine.rebuildGraphIfNeeded();
        }
        if (rebuild && block == 400)
        {
            // an unrelated clip is edited, which publishes yet another graph
            model.undo.beginNewTransaction ("edit notes");
            model.createClip (model.trackAt (1), 3, 2.0, "Edited");
            engine.rebuildGraphIfNeeded();
        }
        if (rebuild && block == 500)
        {
            // the other track is deleted again
            model.undo.beginNewTransaction ("delete track");
            model.deleteTrack (model.trackAt (1).id());
            engine.rebuildGraphIfNeeded();
        }
        runner.run (1);
    }

    ContinuityResult r;
    r.noteOns = counters[deviceId]->noteOns.load();
    r.noteOffs = counters[deviceId]->noteOffs.load();
    if (auto* rt = engine.findRuntime (trackId))
        r.playingSlotAtEnd = rt->playingSlot.load();
    r.nonFinite = runner.nonFinite;
    engine.audioDeviceStopped();
    return r;
}

void testContinuity()
{
    std::printf ("[test 7] playing clip survives graph rebuilds\n");
    const auto baseline = playPadForFourBars (false);
    const auto rebuilt = playPadForFourBars (true);
    std::printf ("    note-ons %d / %d, note-offs %d / %d\n", baseline.noteOns, rebuilt.noteOns, baseline.noteOffs, rebuilt.noteOffs);
    check (baseline.noteOns == 12, "4 bars of the pad clip: 4 chords of 3 notes");
    check (rebuilt.noteOns == baseline.noteOns, "same number of note-ons with rebuilds in between (no restart)");
    check (rebuilt.noteOffs == baseline.noteOffs && rebuilt.noteOffs == baseline.noteOns, "no note is lost or left hanging");
    check (baseline.playingSlotAtEnd == 0 && rebuilt.playingSlotAtEnd == 1, "the clip follows its scene when a scene is inserted above it");
    check (baseline.nonFinite == 0 && rebuilt.nonFinite == 0, "no NaN or Inf");
}

//==============================================================================
// Test 8
void testDeletion()
{
    std::printf ("[test 8] retired graphs are deleted\n");
    const int base = PlaybackGraph::liveCount();
    {
        Fixture f;
        auto& model = f.model;
        auto& engine = f.engine;
        for (int i = 0; i < 4; ++i)
            model.insertScene (-1);
        for (int i = 0; i < 2; ++i)
            addPrismaTrack (model, i);
        putClip (model, model.trackAt (0), 0, daw::engine::demo::bassClip (true));

        FakeDevice fake;
        engine.audioDeviceAboutToStart (&fake);
        Runner runner { engine };
        engine.launchScene (0);

        int maxLive = 0;
        for (int i = 0; i < 100; ++i)
        {
            // structure changes: a track every 10th round, otherwise a clip edit
            if (i % 10 == 9)
            {
                if (model.numTracks() > 2 && i % 20 == 19)
                    model.deleteTrack (model.trackAt (model.numTracks() - 1).id());
                else
                    addPrismaTrack (model, 3);
            }
            else
                model.trackAt (0).clipAt (0).addNote ({ 4.0 + i * 0.01, 0.25, 40 + i % 12, 100 }, nullptr);
            engine.rebuildGraphIfNeeded();
            runner.run (2); // the audio thread notices the new graph
            maxLive = juce::jmax (maxLive, PlaybackGraph::liveCount() - base);
        }
        engine.collectGarbage();
        std::printf ("    most graphs alive at once: %d, now %d\n", maxLive, PlaybackGraph::liveCount() - base);
        check (engine.getPublishedGeneration() >= 100, "100 graphs were published");
        check (maxLive <= 3, "at most 3 graphs alive at any time while the device runs");
        check (PlaybackGraph::liveCount() - base <= 3, "at most 3 graphs alive after 100 rebuilds");
        check (engine.getNumRetiredObjects() <= 3, "retired runtimes and devices are collected too");

        // With the device stopped nobody can see old graphs: they go at once.
        engine.audioDeviceStopped();
        for (int i = 0; i < 10; ++i)
        {
            model.trackAt (0).clipAt (0).addNote ({ 1.0 + i, 0.25, 50, 100 }, nullptr);
            engine.rebuildGraphIfNeeded();
        }
        check (PlaybackGraph::liveCount() - base == 1 && engine.getNumRetiredObjects() == 0, "a stopped device keeps only the current graph");
        check (runner.nonFinite == 0, "no NaN or Inf");
    }
    check (PlaybackGraph::liveCount() == base, "no graph survives its engine");
}

//==============================================================================
// Test 6
void testLockFree()
{
    std::printf ("[test 6] audio thread: no heap use while the project is edited (DAW_RT_CHECKS=%d)\n", daw::rt::enabled ? 1 : 0);
    check (daw::rt::enabled, "the tests are built with DAW_RT_CHECKS");

    Fixture f;
    auto& model = f.model;
    auto& engine = f.engine;
    constexpr int kBlock = 256;

    for (int i = 0; i < 4; ++i)
        model.insertScene (-1);
    const auto keepA = addPrismaTrack (model, 0), keepB = addPrismaTrack (model, 2);
    putClip (model, keepA, 0, daw::engine::demo::arpClip());
    putClip (model, keepB, 0, daw::engine::demo::bassClip (false));
    putClip (model, keepA, 1, daw::engine::demo::padClip());

    FakeDevice fake (kBlock, 44100.0);
    engine.audioDeviceAboutToStart (&fake);
    engine.rebuildGraphIfNeeded();
    Runner runner { engine, kBlock };
    runner.run (4);
    engine.launchScene (0);

    // The "message thread": edits the model and publishes graphs until the audio loop is done.
    std::atomic<bool> stopEditing { false };
    std::atomic<int> rebuilds { 0 };
    std::thread editor ([&] {
        std::vector<int64_t> added;
        int step = 0;
        while (! stopEditing.load())
        {
            switch (step % 9)
            {
                case 0: // new track with an instrument and a clip
                {
                    auto t = addPrismaTrack (model, 1 + step % 5);
                    putClip (model, t, step % 4, daw::engine::demo::leadClip());
                    added.push_back (t.id());
                    break;
                }
                case 1: // clip on an existing track, replacing what is there
                    putClip (model, keepB, 2, daw::engine::demo::bassClip (true));
                    break;
                case 2: // notes change in a clip that may be playing
                    model.trackAt (0).clipAt (0).addNote ({ 15.0, 0.5, 60 + step % 12, 90 }, &model.undo);
                    break;
                case 3: // a second device on a track
                    model.addDevice (keepA, "prisma", "Prisma FX");
                    break;
                case 4: // ... and gone again
                    if (keepA.numDevices() > 1)
                        model.removeDevice (keepA.device (keepA.numDevices() - 1));
                    break;
                case 5: // delete the oldest added track
                    if (! added.empty())
                    {
                        model.deleteTrack (added.front());
                        added.erase (added.begin());
                    }
                    break;
                case 6: // scenes come and go
                    model.insertScene (1);
                    engine.launchScene (0);
                    break;
                case 7:
                    if (model.numScenes() > 4)
                        model.deleteScene (1);
                    keepA.setVolumeDb (-6.0f + (float) (step % 3), nullptr);
                    keepA.device (0).setEnabled (step % 2 == 0, &model.undo);
                    break;
                case 8:
                    putClip (model, keepA, 3, daw::engine::demo::arpClip());
                    engine.stopTrack (keepB.id());
                    break;
                default: break;
            }
            engine.rebuildGraphIfNeeded();
            engine.collectGarbage();
            ++rebuilds;
            ++step;
        }
    });

    daw::rt::resetViolations();
    int blocks = 0;
    while ((blocks < 2000 || rebuilds.load() < 40) && blocks < 400000)
    {
        runner.run (1);
        ++blocks;
    }
    const auto violations = daw::rt::violationCount();
    stopEditing = true;
    editor.join();

    std::printf ("    %d blocks, %d graph rebuilds, %lld heap operations on the audio thread, peak %.3f\n", blocks, rebuilds.load(),
                (long long) violations, (double) runner.peak);
    check (blocks >= 2000 && rebuilds.load() >= 40, "2000 blocks ran while at least 40 graphs were published");
    check (violations == 0, "no allocation or deallocation on the audio thread");
    check (runner.nonFinite == 0, "no NaN or Inf in the output");
    check (runner.peak > 0.01f, "the engine kept making sound");
    engine.audioDeviceStopped();
}
} // namespace

void tests::runGraphTests()
{
    testLockFree();
    testContinuity();
    testDeletion();
}
