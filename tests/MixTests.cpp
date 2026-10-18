// The mix round after the demo song (docs/demo/README.md): the master seen as a track with its chain and automation,
// built-in effects dropped on the master, and the defaults an envelope opens with.
#include "TestSupport.h"

#include "../app/Source/browser/BrowserActions.h"
#include "../app/Source/library/UserLibrary.h"
#include "../app/Source/model/Automation.h"

using namespace tests;

namespace
{
constexpr int kBlock = 256;
constexpr double kRate = 48000.0;

// One audio track that monitors input 1 (a constant 0.5 on the left), through the master to the output.
struct MixRig
{
    MixRig() : dev (kBlock, kRate)
    {
        dev.numInputs = 2;
        f.model.insertScene (-1);
        track = f.model.addTrack ("audio");
        track.setInput ("audio:0", nullptr);
        track.setMonitor ("in", nullptr);
        f.engine.audioDeviceAboutToStart (&dev);
        f.engine.rebuildGraphIfNeeded();
        in.setSize (2, kBlock);
        out.setSize (2, kBlock);
        for (int i = 0; i < kBlock; ++i)
            in.setSample (0, i, 0.5f);
        f.model.undo.clearUndoHistory();
    }
    ~MixRig() { f.engine.audioDeviceStopped(); }

    void blocks (int n)
    {
        for (int b = 0; b < n; ++b)
        {
            in.clear (1, 0, kBlock);
            f.engine.audioDeviceIOCallbackWithContext (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, kBlock, {});
        }
    }
    float left() const { return out.getSample (0, kBlock - 1); }

    Fixture f;
    FakeDevice dev;
    TrackRef track;
    juce::AudioBuffer<float> in, out;
};

void testMasterAsTrack()
{
    std::printf ("[mix] the master is a track for the chain and the automation, never for clips\n");
    auto model = ProjectModel::createDefault();
    const auto master = model.findTrackOrMaster (ProjectModel::kMasterTrackId);
    check (master.isValid() && master.isMaster() && master.type() == "master" && ! master.hasInstrument(), "findTrackOrMaster (0) is the master");
    check (! model.findTrack (ProjectModel::kMasterTrackId).isValid(), "findTrack does not find it: clip code never meets the master");
    check (model.findTrackOrMaster (model.trackAt (0).id()) == model.trackAt (0), "other ids are tracks as before");
    check (model.trackAt (0).hasInstrument() && ! model.trackAt (0).isMaster(), "a MIDI track has an instrument");

    // A file saved before the master had an id gets it when loaded.
    auto tree = ProjectModel::createDefault().root.createCopy();
    tree.getChildWithName (ids::MASTER).removeProperty (ids::id, nullptr);
    ProjectModel loaded (tree);
    check (loaded.masterTrack().id() == ProjectModel::kMasterTrackId, "an older file's master gets id 0");
}

void testMasterEffectDrop()
{
    std::printf ("[mix] built-in effects dropped on the master go into its chain; instruments and files do not\n");
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("daw-mix-lib");
    root.deleteRecursively();
    Fixture f;
    library::UserLibrary lib (root);
    browser::BrowserActions actions (f.model, f.engine, lib);
    f.model.insertScene (-1);
    addPrismaTrack (f.model, 0);
    f.model.undo.clearUndoHistory();

    browser::DragItem comp;
    comp.kind = browser::kinds::builtinFx;
    comp.id = "comp";
    comp.name = "Comp";
    comp.instrument = false;
    browser::DragItem prisma;
    prisma.kind = browser::kinds::prisma;
    prisma.name = "Prisma";
    prisma.instrument = true;
    browser::DragItem wav;
    wav.kind = browser::kinds::audioFile;
    wav.path = "/x.wav";

    const auto m = ProjectModel::kMasterTrackId;
    check (actions.accepts (comp, browser::DropTarget::chainGap, m) && actions.accepts (comp, browser::DropTarget::track, m), "an effect is accepted by the master");
    check (! actions.accepts (prisma, browser::DropTarget::track, m) && ! actions.accepts (wav, browser::DropTarget::track, m), "an instrument and a sound file are not");
    const auto r = actions.drop (comp, browser::DropTarget::chainGap, m, -1, 0);
    check (r.ok && f.model.masterTrack().numDevices() == 1 && f.model.masterTrack().device (0).kind() == "fx", "the compressor is in the master chain");
    check (f.model.numTracks() == 1, "no track was made for it");
    const auto r2 = actions.useOnTrack (comp, m);
    check (r2.ok && f.model.masterTrack().numDevices() == 2, "double-click in the browser with the master selected adds to the master");
    f.model.undo.undo();
    check (f.model.masterTrack().numDevices() == 1, "each drop is one undo step");
    root.deleteRecursively();
}

void testMasterAutomation()
{
    std::printf ("[mix] master automation: a volume lane sets the level of the whole mix, a master effect follows its lane\n");
    MixRig rig;
    rig.blocks (4);
    check (std::abs (rig.left() - 0.5f) < 2.0e-3f, "the track reaches the output at its level: " + juce::String (rig.left()));

    auto master = rig.f.model.masterTrack();
    auto lane = automation::getOrAddLane (rig.f.model, master, automation::volumeTarget);
    automation::addPoint (rig.f.model, lane, { 0.0, automation::normalisedVolume (-6.0206f), 0.0f });
    auto fx = rig.f.model.addDevice (rig.f.model.masterDevicesNode(), "fx", "Comp");
    fx.setPluginDesc ("comp", nullptr);
    rig.f.engine.rebuildGraphIfNeeded();
    auto* p = rig.f.engine.findProcessor (fx.id());
    check (p != nullptr, "the master effect has a processor");
    const int outputIndex = p != nullptr ? p->getParameters().size() - 1 : 0;
    auto plane = automation::getOrAddLane (rig.f.model, master, automation::deviceTarget (fx.id(), outputIndex));
    automation::addPoint (rig.f.model, plane, { 0.0, 0.5f, 0.0f });
    automation::addPoint (rig.f.model, plane, { 4.0, 0.5f, 0.0f });
    rig.f.engine.rebuildGraphIfNeeded();

    if (p != nullptr)
        p->getParameters()[outputIndex]->setValue (0.5f);
    rig.blocks (40);
    const float reference = rig.left(); // through the compressor, the lanes not applied while stopped
    check (reference > 0.05f, "stopped, the mix still sounds at the fader level: " + juce::String (reference));
    rig.f.engine.play();
    rig.blocks (8);
    check (std::abs (rig.left() - 0.5f * reference) < 3.0e-3f, "playing, the lane at -6 dB halves the mix: " + juce::String (rig.left()));
    check (p != nullptr && std::abs (p->getParameters()[outputIndex]->getValue() - 0.5f) < 0.01f, "the master compressor's output knob follows its lane");

    // Master pan is a balance: hard right silences the left and leaves the right as it was.
    auto pan = automation::getOrAddLane (rig.f.model, master, automation::panTarget);
    automation::addPoint (rig.f.model, pan, { 0.0, 1.0f, 0.0f });
    rig.f.engine.rebuildGraphIfNeeded();
    rig.blocks (4);
    check (std::abs (rig.left()) < 1.0e-4f, "panned hard right, the left is silent");
    rig.f.engine.stop();
    rig.blocks (2);
}
} // namespace

namespace tests
{
void runMixTests()
{
    testMasterAsTrack();
    testMasterEffectDrop();
    testMasterAutomation();
}
} // namespace tests
