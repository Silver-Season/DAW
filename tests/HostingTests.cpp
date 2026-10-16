// Device state across undo, and hosting of a real third-party-style plugin.
//   - Deleting a device (or a track) stores its live state first, so undo brings the parameters back.
//   - The Prisma VST3 built in this same build is scanned, loaded through AudioEngine::loadPluginInstrument
//     (the path the UI uses), played, and released again.
#include "TestSupport.h"

#include "../app/Source/project/ProjectFile.h"

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
void waitUntil (const std::function<bool()>& done, int maxMs = 15000)
{
    for (int waited = 0; waited < maxMs && ! done(); waited += 20)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
}

// Index of the hosted plugin's filter cutoff parameter (the parameters of a VST3 are not named by our ids).
int cutoffIndex (juce::AudioProcessor& p)
{
    const auto& params = p.getParameters();
    for (int i = 0; i < params.size(); ++i)
        if (params[i]->getName (64).containsIgnoreCase ("cutoff"))
            return i;
    return -1;
}

juce::PluginDescription scanPrisma (AudioEngine& engine)
{
    juce::VST3PluginFormat vst3;
    juce::OwnedArray<juce::PluginDescription> found;
    for (const auto& file : juce::File (DAW_TEST_VST3_DIR).findChildFiles (juce::File::findFilesAndDirectories, false, "*.vst3"))
        engine.getKnownPlugins().scanAndAddFile (file.getFullPathName(), true, found, vst3);
    return found.isEmpty() ? juce::PluginDescription() : *found[0];
}

void testRestoreHostedPlugin()
{
    std::printf ("[test] hosted plugins come back from pluginDesc: project load, undo of a delete, a missing plugin that appears\n");

    // First life of the project: a hosted Prisma with its cutoff moved.
    Fixture f;
    const auto desc = scanPrisma (f.engine);
    check (desc.name == "Prisma", "the Prisma VST3 of this build is found");
    if (desc.name != "Prisma")
        return;
    f.model.insertScene (-1);
    const auto firstTrack = f.model.addTrack ("midi");
    f.model.addDevice (firstTrack, "prisma", "Prisma");
    FakeDevice fake { 512, 48000.0 };
    f.engine.audioDeviceAboutToStart (&fake);
    f.engine.rebuildGraphIfNeeded();

    juce::String error = "pending";
    f.engine.loadPluginInstrument (firstTrack.id(), desc, [&] (juce::String e, int64_t) { error = e; });
    waitUntil ([&] { return error != "pending"; });
    check (error.isEmpty(), "the plugin loaded: " + error);
    auto* hosted = f.engine.findProcessor (firstTrack.device (0).id());
    check (hosted != nullptr, "the hosted plugin is live");
    if (hosted == nullptr)
        return;
    const int cutoff = cutoffIndex (*hosted);
    check (cutoff >= 0, "the plugin has a cutoff parameter");
    if (cutoff < 0)
        return;
    const float target = 0.8f; // open enough to hear the note
    hosted->getParameters()[cutoff]->setValueNotifyingHost (target);
    f.engine.captureDeviceStates (f.model.root);
    const auto xml = project::serialise (f.model.root);
    check (firstTrack.device (0).stateBase64().isNotEmpty() && firstTrack.device (0).pluginDesc().isNotEmpty(),
           "the DEVICE node holds the description and the state");
    f.engine.audioDeviceStopped();

    // 1. The project is loaded into another engine: the plugin is created in the background.
    juce::TemporaryFile settingsFile (".settings");
    Settings settings (settingsFile.getFile());
    ProjectModel model (ProjectModel::createEmpty());
    AudioEngine engine (settings);
    engine.setModel (&model);
    scanPrisma (engine);
    const auto parsed = project::parseProject (xml);
    check (parsed.ok(), "the project parses");
    FakeDevice fake2 { 512, 48000.0 };
    engine.audioDeviceAboutToStart (&fake2);
    model.adopt (parsed.tree);
    engine.rebuildGraphIfNeeded();
    const auto loaded = model.trackAt (0).device (0);
    check (loaded.kind() == "plugin" && engine.findProcessor (loaded.id()) == nullptr, "right after loading the device has no instance yet");
    check (engine.isDeviceLoading (loaded.id()), "and is marked as loading (the view shows a spinner)");
    const auto epochBefore = engine.getDeviceEpoch();
    waitUntil ([&] { return engine.findProcessor (loaded.id()) != nullptr; });
    auto* restored = engine.findProcessor (loaded.id());
    check (restored != nullptr, "the plugin instance appears without blocking the caller");
    if (restored == nullptr)
        return;
    check (! engine.isDeviceLoading (loaded.id()) && engine.getDeviceEpoch() != epochBefore, "loading is over and the views were told");
    check (loaded.kind() == "plugin", "the kind stays \"plugin\"");
    check (cutoffIndex (*restored) == cutoff && std::abs (restored->getParameters()[cutoff]->getValue() - target) < 2e-3f,
           "the restored plugin has the saved parameter value");

    model.trackAt (0).setArm (true, nullptr);
    Runner runner { engine };
    runner.run (10);
    check (runner.run (10) < 1e-6, "silent before a note");
    engine.getKeyboardState().noteOn (1, 60, 1.0f);
    const double rms = runner.run (40);
    engine.getKeyboardState().noteOff (1, 60, 0.0f);
    std::printf ("    restored VST3 note RMS %.4f\n", rms);
    check (rms > 0.005 && runner.nonFinite == 0, "the restored plugin makes sound");
    runner.run (300);

    // 2. Delete and undo: the instance is made again, with the state it had when it was deleted.
    const float moved = 0.45f;
    restored->getParameters()[cutoff]->setValueNotifyingHost (moved);
    model.undo.beginNewTransaction ("delete");
    engine.removeDevice (loaded);
    engine.rebuildGraphIfNeeded();
    runner.run (4);
    engine.collectGarbage();
    check (engine.findProcessor (loaded.id()) == nullptr, "the deleted plugin is gone");
    model.undo.undo();
    engine.rebuildGraphIfNeeded();
    const auto undone = model.trackAt (0).device (0);
    check (model.trackAt (0).numDevices() == 1 && undone.kind() == "plugin", "undo brings the DEVICE node back");
    check (engine.findProcessor (undone.id()) == nullptr && engine.isDeviceLoading (undone.id()), "without an instance, which starts loading");
    waitUntil ([&] { return engine.findProcessor (undone.id()) != nullptr; });
    auto* back = engine.findProcessor (undone.id());
    check (back != nullptr, "the instance is back after undo");
    if (back != nullptr)
        check (std::abs (back->getParameters()[cutoff]->getValue() - moved) < 2e-3f, "with the parameter it had when it was deleted");

    // 3. A device that was missing when the project was saved comes back once the plugin is installed.
    auto tree = juce::ValueTree::fromXml (*juce::XmlDocument::parse (xml));
    tree.getChildWithName (ids::TRACKS).getChild (0).getChildWithName (ids::DEVICES).getChild (0).setProperty (ids::kind, "missing", nullptr);
    model.adopt (tree);
    engine.rebuildGraphIfNeeded();
    const auto wasMissing = model.trackAt (0).device (0);
    check (wasMissing.kind() == "missing" && engine.findProcessor (wasMissing.id()) == nullptr, "a missing device has no instance");
    engine.reviveMissingDevices();
    check (wasMissing.kind() == "plugin", "it becomes a plugin again, because the plugin exists now");
    engine.rebuildGraphIfNeeded();
    waitUntil ([&] { return engine.findProcessor (wasMissing.id()) != nullptr; });
    check (engine.findProcessor (wasMissing.id()) != nullptr, "and it loads");

    // 4. An unreadable description: the device is missing at once and keeps its state.
    auto broken = juce::ValueTree::fromXml (*juce::XmlDocument::parse (xml));
    auto node = broken.getChildWithName (ids::TRACKS).getChild (0).getChildWithName (ids::DEVICES).getChild (0);
    node.setProperty (ids::pluginDesc, "this is not a description", nullptr);
    model.adopt (broken);
    engine.rebuildGraphIfNeeded();
    check (model.trackAt (0).device (0).kind() == "missing", "an unreadable description makes the device missing");
    check (model.trackAt (0).device (0).stateBase64() == node.getProperty (ids::state).toString(), "and its state is kept");

    // 5. The project is replaced while a load is in flight: the late instance is dropped.
    model.adopt (parsed.tree.createCopy());
    engine.rebuildGraphIfNeeded();
    check (engine.isDeviceLoading (model.trackAt (0).device (0).id()), "a load is in flight");
    model.adopt (ProjectModel::createDefault().root.createCopy());
    engine.rebuildGraphIfNeeded();
    const auto epochAfterReplace = engine.getDeviceEpoch();
    waitUntil ([] { return false; }, 800);
    check (engine.getDeviceEpoch() == epochAfterReplace, "the instance that arrives for the old project is thrown away");
    runner.run (10);
    engine.audioDeviceStopped();
}

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
    testRestoreHostedPlugin();
#else
    check (false, "DAW_TEST_VST3_DIR is not defined: the VST3 hosting test cannot run");
#endif
}
