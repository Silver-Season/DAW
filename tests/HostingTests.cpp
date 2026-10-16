// Device state across undo, and hosting of a real third-party-style plugin.
//   - Deleting a device (or a track) stores its live state first, so undo brings the parameters back.
//   - The Prisma VST3 built in this same build is scanned, loaded through AudioEngine::loadPluginInstrument
//     (the path the UI uses), played, and released again.
#include "TestSupport.h"

using namespace tests;

namespace
{
const daw::synth::ParamDescriptor& cutoffDescriptor()
{
    for (const auto& d : daw::synth::parameterDescriptors())
        if (juce::String (d.id).containsIgnoreCase ("cutoff"))
            return d;
    return daw::synth::parameterDescriptors().front();
}

float readParam (juce::AudioProcessor* p, const std::string& id)
{
    auto* prisma = dynamic_cast<PrismaProcessor*> (p);
    return prisma != nullptr ? prisma->apvts.getRawParameterValue (id)->load() : -1.0f;
}

void setParam (juce::AudioProcessor* p, const std::string& id, float value)
{
    auto* prisma = dynamic_cast<PrismaProcessor*> (p);
    auto* param = prisma->apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

void testStateSurvivesDeleteAndUndo()
{
    std::printf ("[test] deleting a device and undoing keeps its live parameters\n");
    Fixture f;
    auto& model = f.model;
    auto& engine = f.engine;
    const auto& cutoff = cutoffDescriptor();
    const float target = cutoff.min + 0.37f * (cutoff.max - cutoff.min);

    const auto track = model.addTrack ("midi");
    const auto device = model.addDevice (track, "prisma", "Prisma");
    const auto other = model.addTrack ("midi");
    const auto otherDevice = model.addDevice (other, "prisma", "Prisma");
    engine.rebuildGraphIfNeeded();

    setParam (engine.findProcessor (device.id()), cutoff.id, target);
    setParam (engine.findProcessor (otherDevice.id()), cutoff.id, target);
    const float live = readParam (engine.findProcessor (device.id()), cutoff.id);
    check (std::abs (live - target) < 0.01f * (cutoff.max - cutoff.min), "the parameter was changed on the live processor");
    check (device.stateBase64().isEmpty(), "the model does not know the live change yet");

    // device
    model.undo.beginNewTransaction ("delete device");
    engine.removeDevice (device);
    engine.rebuildGraphIfNeeded();
    check (track.numDevices() == 0 && engine.findProcessor (device.id()) == nullptr, "the device and its processor are gone");
    model.undo.undo();
    engine.rebuildGraphIfNeeded();
    check (track.numDevices() == 1, "undo brings the DEVICE node back");
    check (std::abs (readParam (engine.findProcessor (device.id()), cutoff.id) - live) < 1e-3f, "the parameter is restored after undo");
    model.undo.redo();
    check (track.numDevices() == 0, "redo removes it again");
    model.undo.undo();

    // whole track
    model.undo.beginNewTransaction ("delete track");
    engine.deleteTrack (other.id());
    engine.rebuildGraphIfNeeded();
    check (model.numTracks() == 1 && engine.findProcessor (otherDevice.id()) == nullptr, "the track and its devices are gone");
    model.undo.undo();
    engine.rebuildGraphIfNeeded();
    check (model.numTracks() == 2, "undo brings the track back");
    check (std::abs (readParam (engine.findProcessor (otherDevice.id()), cutoff.id) - live) < 1e-3f,
           "the parameter of a device on a deleted track is restored after undo");
}

#ifdef DAW_TEST_VST3_DIR
void testHostVst3()
{
    std::printf ("[test] hosting: scan the Prisma VST3 from this build, load, play, remove\n");
    const juce::File dir (DAW_TEST_VST3_DIR);
    check (dir.isDirectory(), "the VST3 folder of the build exists: " + dir.getFullPathName());

    Fixture f;
    auto& engine = f.engine;
    auto& model = f.model;

    // scan
    juce::VST3PluginFormat vst3;
    juce::OwnedArray<juce::PluginDescription> found;
    for (const auto& file : dir.findChildFiles (juce::File::findFilesAndDirectories, false, "*.vst3"))
        engine.getKnownPlugins().scanAndAddFile (file.getFullPathName(), true, found, vst3);
    check (found.size() == 1, "the scanner finds the plugin (" + juce::String (found.size()) + " found)");
    if (found.isEmpty())
        return;
    const auto desc = *found[0];
    check (desc.isInstrument && desc.name == "Prisma", "it is an instrument called Prisma");
    check (engine.getKnownPlugins().getTypes().size() == 1, "it is in the known plugin list");

    // a track with Prisma built in, then replaced by the hosted plugin
    for (int i = 0; i < 2; ++i)
        model.insertScene (-1);
    const auto track = model.addTrack ("midi");
    model.addDevice (track, "prisma", "Prisma");
    track.setArm (true, nullptr);

    FakeDevice fake;
    engine.audioDeviceAboutToStart (&fake);
    engine.rebuildGraphIfNeeded();
    Runner runner { engine };
    const auto builtInId = track.device (0).id();

    juce::String error = "pending";
    int64_t deviceId = -1;
    engine.loadPluginInstrument (track.id(), desc, [&] (juce::String e, int64_t id) {
        error = e;
        deviceId = id;
    });
    for (int i = 0; i < 500 && error == "pending"; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    check (error.isEmpty(), "the plugin loaded: " + error);
    if (error.isNotEmpty())
        return;

    check (track.numDevices() == 1 && track.device (0).kind() == "plugin" && track.device (0).id() == deviceId, "the DEVICE node is kind=plugin");
    check (track.device (0).pluginDesc().contains ("Prisma"), "the plugin description is stored in the model");
    check (engine.findProcessor (builtInId) == nullptr, "the built-in Prisma was replaced");
    auto* hosted = engine.findProcessor (deviceId);
    check (hosted != nullptr && dynamic_cast<PrismaProcessor*> (hosted) == nullptr, "the processor is the hosted plugin, not the compiled-in class");

    runner.run (10);
    check (runner.run (10) < 1e-6, "silent before a note");
    engine.getKeyboardState().noteOn (1, 60, 1.0f);
    const double rms = runner.run (40);
    engine.getKeyboardState().noteOff (1, 60, 0.0f);
    std::printf ("    hosted VST3 note RMS %.4f\n", rms);
    check (rms > 0.005, "the hosted plugin makes sound");
    check (runner.nonFinite == 0, "no NaN or Inf");
    runner.run (300);

    // remove again: the graph lets go of it
    model.undo.beginNewTransaction ("remove");
    engine.removeDevice (track.device (0));
    engine.rebuildGraphIfNeeded();
    runner.run (4); // the audio thread moves on to the new graph
    engine.collectGarbage();
    check (engine.findProcessor (deviceId) == nullptr, "the engine no longer owns the plugin");
    check (engine.getNumRetiredObjects() <= 1, "nothing but the current graph is left waiting for deletion");
    check (runner.run (10) < 1e-6, "the track is silent without a device");
    engine.audioDeviceStopped();
    engine.collectGarbage();
    check (engine.getNumRetiredObjects() == 0, "everything is released once the device stops");
}
#endif
} // namespace

void tests::runHostingTests()
{
    testStateSurvivesDeleteAndUndo();
#ifdef DAW_TEST_VST3_DIR
    testHostVst3();
#else
    check (false, "DAW_TEST_VST3_DIR is not defined: the VST3 hosting test cannot run");
#endif
}
