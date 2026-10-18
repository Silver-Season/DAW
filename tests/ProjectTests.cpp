// The project file layer (docs/spec/01-layout-project.md, task 1.7, tests 3, 4, 5 and 10): saving and loading
// with identical sound, missing plugins, schema versions, migrations, the history on disk, crash recovery.
#include "TestSupport.h"

#include "../app/Source/Strings.h"
#include "../app/Source/library/UserLibrary.h"
#include "../app/Source/project/MidiImport.h"
#include "../app/Source/project/ProjectFile.h"
#include "../app/Source/project/ProjectSession.h"
#include "../app/Source/project/RecoveryCheck.h"

using namespace tests;

namespace
{
juce::File makeTempFolder (const juce::String& tag)
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("daw-test-" + tag + "-" + juce::String (juce::Random::getSystemRandom().nextInt (1000000)));
    dir.createDirectory();
    return dir;
}

void stripState (juce::ValueTree v)
{
    v.removeProperty (ids::state, nullptr);
    for (auto c : v)
        stripState (c);
}

juce::String xmlWithoutState (const juce::ValueTree& tree)
{
    auto copy = tree.createCopy();
    stripState (copy);
    return copy.toXmlString();
}

// Renders `seconds` of the engine's output (left and right) by calling the audio callback by hand.
struct Rendered
{
    std::vector<float> left, right;
};

Rendered render (AudioEngine& engine, double seconds, int block = 512, double rate = 48000.0)
{
    Rendered r;
    juce::AudioBuffer<float> out (2, block);
    const int blocks = (int) std::ceil (seconds * rate / block);
    for (int b = 0; b < blocks; ++b)
    {
        engine.audioDeviceIOCallbackWithContext (nullptr, 0, out.getArrayOfWritePointers(), 2, block, {});
        for (int i = 0; i < block; ++i)
        {
            r.left.push_back (out.getSample (0, i));
            r.right.push_back (out.getSample (1, i));
        }
    }
    return r;
}

const daw::synth::ParamDescriptor& cutoffParam()
{
    for (const auto& d : daw::synth::parameterDescriptors())
        if (juce::String (d.id).containsIgnoreCase ("cutoff"))
            return d;
    return daw::synth::parameterDescriptors().front();
}

//==============================================================================
void testSaveAndLoad()
{
    std::printf ("[test 3] save and load: same XML, same parameter, same sound sample by sample\n");
    const auto folder = makeTempFolder ("save");
    const auto& cutoff = cutoffParam();
    const float target = cutoff.min + 0.31f * (cutoff.max - cutoff.min);

    juce::String savedXml;
    Rendered original;
    int64_t trackId = 0;
    {
        Fixture f;
        for (int i = 0; i < 4; ++i)
            f.model.insertScene (-1);
        f.model.setName ("Roundtrip");
        const auto track = addPrismaTrack (f.model, 3);
        trackId = track.id();
        putClip (f.model, track, 0, daw::engine::demo::padClip());
        track.setVolumeDb (-3.0f, nullptr);
        f.model.setTempo (131.0);

        FakeDevice fake { 512, 48000.0 };
        f.engine.audioDeviceAboutToStart (&fake);
        f.engine.rebuildGraphIfNeeded();
        auto* prisma = dynamic_cast<PrismaProcessor*> (f.engine.findProcessor (track.device (0).id()));
        check (prisma != nullptr, "the track has a live Prisma");
        if (prisma == nullptr)
            return;
        auto* param = prisma->apvts.getParameter (cutoff.id);
        param->setValueNotifyingHost (param->convertTo0to1 (target)); // the live change the user made with the filter knob

        f.engine.captureDeviceStates (f.model.root);
        savedXml = project::serialise (f.model.root);
        juce::String error;
        check (project::writeSave (folder, "Roundtrip", savedXml, 1000, error), "the project is written: " + error);

        f.engine.launchClip (trackId, 0);
        f.engine.rebuildGraphIfNeeded();
        original = render (f.engine, 1.0);
        f.engine.audioDeviceStopped();
    }

    check (project::mainFile (folder, "Roundtrip").existsAsFile(), "Roundtrip.prisma exists");
    check (project::historyFolder (folder).getChildFile ("index.xml").existsAsFile() && project::versionFile (folder, 1).existsAsFile(),
           "the save made version 1 in History");

    const auto loaded = project::loadProjectFile (project::mainFile (folder, "Roundtrip"));
    check (loaded.ok(), "the file loads: " + loaded.error);
    if (! loaded.ok())
        return;
    check (xmlWithoutState (loaded.tree) == xmlWithoutState (juce::ValueTree::fromXml (*juce::XmlDocument::parse (savedXml))),
           "XML of the loaded project equals the saved one (without state)");

    juce::TemporaryFile settingsFile (".settings");
    Settings settings (settingsFile.getFile());
    ProjectModel model (ProjectModel::createEmpty());
    AudioEngine engine (settings, false);
    engine.setModel (&model);
    model.adopt (loaded.tree);
    FakeDevice fake { 512, 48000.0 };
    engine.audioDeviceAboutToStart (&fake);
    engine.rebuildGraphIfNeeded();

    const auto track = model.findTrack (trackId);
    check (track.isValid() && model.name() == "Roundtrip" && std::abs (model.tempo() - 131.0) < 1e-9, "tempo, name and track survive");
    auto* prisma = track.isValid() ? dynamic_cast<PrismaProcessor*> (engine.findProcessor (track.device (0).id())) : nullptr;
    check (prisma != nullptr, "the loaded project has a live Prisma");
    if (prisma != nullptr)
    {
        const float value = prisma->apvts.getRawParameterValue (cutoff.id)->load();
        check (std::abs (value - target) < 0.002f * (cutoff.max - cutoff.min), "the filter cutoff is the saved one");
    }

    engine.launchClip (trackId, 0);
    engine.rebuildGraphIfNeeded();
    const auto again = render (engine, 1.0);
    engine.audioDeviceStopped();

    check (again.left.size() == original.left.size() && ! original.left.empty(), "both renders have the same length");
    double maxDiff = 0.0, peak = 0.0;
    for (size_t i = 0; i < juce::jmin (original.left.size(), again.left.size()); ++i)
    {
        maxDiff = juce::jmax (maxDiff, (double) std::abs (original.left[i] - again.left[i]), (double) std::abs (original.right[i] - again.right[i]));
        peak = juce::jmax (peak, (double) std::abs (original.left[i]));
    }
    std::printf ("    peak of the first second %.4f, largest difference after loading %.3g\n", peak, maxDiff);
    check (peak > 0.01, "the first second of the demo clip is not silent");
    check (juce::exactlyEqual (maxDiff, 0.0), "the sound after loading is identical sample by sample");
    folder.deleteRecursively();
}

//==============================================================================
void testMissingPlugin()
{
    std::printf ("[test 4] a plugin that does not exist loads as missing and keeps its state\n");
    const auto folder = makeTempFolder ("missing");

    juce::PluginDescription desc;
    desc.name = "Third Synth";
    desc.manufacturerName = "Xfer Records";
    desc.pluginFormatName = "VST3";
    desc.fileOrIdentifier = "/no/such/folder/ThirdSynth.vst3";
    desc.uniqueId = 12345;
    desc.isInstrument = true;
    desc.numInputChannels = 0;
    desc.numOutputChannels = 2;
    const auto descText = desc.createXml()->toString (juce::XmlElement::TextFormat().singleLine());
    const juce::String stateText = juce::MemoryBlock ("secret plugin state", 19).toBase64Encoding();

    juce::String xml;
    {
        auto model = ProjectModel::createEmpty();
        model.insertScene (-1);
        const auto track = model.addTrack ("midi");
        const auto device = model.addDevice (track, "plugin", "Third Synth");
        device.setPluginDesc (descText, nullptr);
        device.setStateBase64 (stateText, nullptr);
        xml = project::serialise (model.root);
    }
    const auto parsed = project::parseProject (xml);
    check (parsed.ok(), "a project with a plugin device parses");

    juce::TemporaryFile settingsFile (".settings");
    Settings settings (settingsFile.getFile());
    ProjectModel model (ProjectModel::createEmpty());
    AudioEngine engine (settings, false);
    engine.setModel (&model);
    model.adopt (parsed.tree);
    FakeDevice fake { 512, 48000.0 };
    engine.audioDeviceAboutToStart (&fake);
    engine.rebuildGraphIfNeeded();

    const auto device = model.trackAt (0).device (0);
    check (device.kind() == "missing", "kind became \"missing\" (was: " + device.kind() + ")");
    check (device.stateBase64() == stateText && device.pluginDesc() == descText && device.name() == "Third Synth", "state, description and name are untouched");
    check (engine.findProcessor (device.id()) == nullptr && ! engine.isDeviceLoading (device.id()), "no instance, nothing loading");

    // The sound goes through the gap: the track is silent, the engine does not complain.
    Runner runner { engine };
    check (runner.run (10) < 1e-6 && runner.nonFinite == 0, "a missing device is silent and harmless");

    // Saving again keeps everything, and a second load gives the same result.
    engine.captureDeviceStates (model.root);
    const auto xml2 = project::serialise (model.root);
    const auto parsed2 = project::parseProject (xml2);
    const auto device2 = parsed2.tree.getChildWithName (ids::TRACKS).getChild (0).getChildWithName (ids::DEVICES).getChild (0);
    check (device2.getProperty (ids::kind).toString() == "missing" && device2.getProperty (ids::state).toString() == stateText
               && device2.getProperty (ids::pluginDesc).toString() == descText,
           "after saving again the state and description are still there");
    engine.audioDeviceStopped();
    folder.deleteRecursively();
}

//==============================================================================
void testSchemaVersion()
{
    std::printf ("[test 5] schema versions: newer is an error, older is migrated, garbage does not crash\n");
    auto make = [] (int version) {
        auto model = ProjectModel::createDefault();
        model.root.setProperty (ids::schemaVersion, version, nullptr);
        return project::serialise (model.root);
    };

    const auto future = project::parseProject (make (999));
    check (! future.ok() && future.error == Strings::errorNewerProject, "schemaVersion 999 gives \"Project was created in a newer version\"");
    check (! future.tree.isValid(), "and no tree");

    const auto current = project::parseProject (make (ProjectModel::kSchemaVersion));
    check (current.ok() && current.tree.getChildWithName (ids::TRACKS).getNumChildren() == 2, "the current version loads");

    check (! project::parseProject ("this is not xml").ok() && ! project::parseProject ("").ok(), "garbage is an error, not a crash");
    check (! project::parseProject ("<SOMETHING/>").ok(), "XML that is not a project is an error");
    check (! project::parseProject (make (0)).ok(), "schemaVersion 0 is an error");

    const auto missing = project::loadProjectFile (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("no-such-project.prisma"));
    check (! missing.ok() && missing.error.isNotEmpty(), "a missing file is an error");

    const auto file = makeTempFolder ("future").getChildFile ("future.prisma");
    file.replaceWithText (make (999));
    check (! project::loadProjectFile (file).ok(), "a newer file on disk is refused");
    file.getParentDirectory().deleteRecursively();

    // The migration chain: version 1 -> 2 -> 3 with test steps.
    std::vector<project::Migration> steps;
    steps.push_back ([] (juce::ValueTree& t) { t.setProperty ("addedIn2", "yes", nullptr); });
    steps.push_back ([] (juce::ValueTree& t) { t.setProperty ("addedIn3", t.getProperty ("addedIn2").toString() + "!", nullptr); });
    const auto migrated = project::parseProject (make (1), 3, steps);
    check (migrated.ok() && (int) migrated.tree.getProperty (ids::schemaVersion) == 3, "a version 1 file is brought to version 3");
    check (migrated.tree.getProperty ("addedIn3").toString() == "yes!", "the steps ran in order");
    const auto halfway = project::parseProject (make (2), 3, steps);
    check (halfway.ok() && ! halfway.tree.hasProperty ("addedIn2") && halfway.tree.hasProperty ("addedIn3"), "a version 2 file only gets the second step");
    check (! project::parseProject (make (1), 4, steps).ok(), "a missing migration step is an error");
    check (project::migrations().size() == (size_t) (ProjectModel::kSchemaVersion - 1), "this build has exactly the migrations up to its schema version");
}

//==============================================================================
void testRecovery()
{
    std::printf ("[test 10] recovery: a lock plus an autosave newer than the project\n");
    using project::RecoveryCheck;
    check (RecoveryCheck::needsRecovery (true, 2000, 1000), "lock, autosave newer than the project: yes");
    check (! RecoveryCheck::needsRecovery (true, 1000, 2000), "autosave older than the project: no");
    check (! RecoveryCheck::needsRecovery (true, 1000, 1000), "equal times: no");
    check (! RecoveryCheck::needsRecovery (false, 2000, 1000), "no lock (normal exit): no");
    check (! RecoveryCheck::needsRecovery (true, std::nullopt, 1000), "no autosave: no");
    check (RecoveryCheck::needsRecovery (true, 2000, std::nullopt), "never saved, but autosaved: yes");
    check (! RecoveryCheck::needsRecovery (false, std::nullopt, std::nullopt), "nothing at all: no");

    // The same through real files
    const auto root = makeTempFolder ("recovery");
    const auto folder = root.getChildFile ("Song Project");
    const auto lock = root.getChildFile ("session.lock");
    juce::String error;
    project::writeAtomically (project::mainFile (folder, "Song"), "<PROJECT/>", error);
    project::writeAtomically (project::autosaveFile (folder), "<PROJECT/>", error);
    const auto now = juce::Time::getCurrentTime();
    project::mainFile (folder, "Song").setLastModificationTime (now - juce::RelativeTime::minutes (10));
    project::autosaveFile (folder).setLastModificationTime (now - juce::RelativeTime::minutes (2));

    check (! RecoveryCheck::check (lock).needed, "no lock file: no recovery");
    RecoveryCheck::writeLock (lock, folder);
    auto result = RecoveryCheck::check (lock);
    check (result.needed && result.folder == folder && result.autosave == project::autosaveFile (folder)
               && result.projectFile == project::mainFile (folder, "Song"),
           "lock + newer autosave: recovery, with the right files");

    project::mainFile (folder, "Song").setLastModificationTime (now - juce::RelativeTime::minutes (1));
    check (! RecoveryCheck::check (lock).needed, "saved after the autosave: no recovery");
    project::mainFile (folder, "Song").setLastModificationTime (now - juce::RelativeTime::minutes (10));
    check (RecoveryCheck::check (lock).needed, "and yes again when the autosave is newer");

    project::mainFile (folder, "Song").deleteFile();
    result = RecoveryCheck::check (lock);
    check (result.needed && ! result.projectFile.existsAsFile(), "an unsaved project with an autosave is recovered, too");

    RecoveryCheck::removeLock (lock);
    check (! RecoveryCheck::check (lock).needed, "a normal exit removes the lock");
    lock.replaceWithText ("");
    check (! RecoveryCheck::check (lock).needed, "an empty lock names no project");
    root.deleteRecursively();
}

//==============================================================================
void testHistoryOnDisk()
{
    std::printf ("[test] history on disk: versions, labels, thinning, loading a version\n");
    const auto folder = makeTempFolder ("history");
    juce::String error;
    const auto doc = [] (int n) { return "<PROJECT schemaVersion=\"1\" marker=\"" + juce::String (n) + "\"/>"; };

    check (project::readVersions (folder).empty(), "a new folder has no versions");
    check (project::writeSave (folder, "Song", doc (1), 1000, error), "save: " + error);
    check (project::writeAutosave (folder, doc (2), 2000, error), "autosave: " + error);
    check (project::writeRestorePoint (folder, doc (3), 3000, error), "restore point: " + error);
    auto versions = project::readVersions (folder);
    check (versions.size() == 3 && versions[0].number == 1 && versions[1].number == 2 && versions[2].number == 3, "three versions, numbered 1 to 3");
    check (versions[0].kind == daw::project::VersionKind::save && versions[1].kind == daw::project::VersionKind::autosave
               && versions[2].kind == daw::project::VersionKind::restore,
           "kinds are save, autosave, restore");
    check (project::mainFile (folder, "Song").loadFileAsString().contains ("marker=\"1\""), "the main file holds the saved state");
    check (project::autosaveFile (folder).loadFileAsString().contains ("marker=\"2\""), "the autosave file holds the autosaved state");

    check (project::setVersionLabel (folder, 2, "before the new bass"), "a label can be set");
    check (project::readVersions (folder)[1].label == "before the new bass", "and read back");
    check (! project::setVersionLabel (folder, 99, "x"), "an unknown version has no label");

    // 70 more autosaves within one hour: the labelled and the saved stay, only the newest 50 of the plain ones remain
    for (int i = 0; i < 70; ++i)
        project::writeAutosave (folder, doc (100 + i), 10000 + i * 1000, error);
    versions = project::readVersions (folder);
    int plain = 0;
    bool keptSpecial = false;
    for (const auto& v : versions)
    {
        if (v.kind == daw::project::VersionKind::autosave && v.label.isEmpty())
            ++plain;
        if (v.number == 1 || v.number == 3 || v.number == 2)
            keptSpecial = true;
    }
    check (keptSpecial, "the save, the restore point and the labelled version survive thinning");
    check (plain <= daw::project::kRecentAutosavesKept + 1 && plain >= daw::project::kRecentAutosavesKept, "about 50 plain autosaves remain (" + juce::String (plain) + ")");
    check (! project::versionFile (folder, 5).existsAsFile(), "the files of thinned versions are deleted");

    const auto loaded = project::loadVersion (folder, 1);
    check (loaded.ok() && (int) loaded.tree.getProperty ("marker") == 1, "a version can be loaded");
    check (! project::loadVersion (folder, 5).ok(), "a deleted version cannot");

    // numbers are never reused
    const int next = project::addVersion (folder, doc (7), daw::project::VersionKind::save, 99999999, error);
    check (next > (int) versions.back().number, "numbers keep growing after thinning");
    folder.deleteRecursively();
}

void testFolderHelpers()
{
    std::printf ("[test] project folder helpers and Save As\n");
    check (project::projectNameOfFolder (juce::File ("/x/My Song Project")) == "My Song", "folder name -> project name");
    check (project::projectNameOfFolder (juce::File ("/x/Plain")) == "Plain", "a folder without the suffix keeps its name");
    const auto unsaved = project::newUnsavedFolder (juce::File ("/home/u/Music"), juce::Time (2026, 9, 14, 21, 5, 7));
    check (unsaved.getFullPathName().contains ("Prisma DAW/Unsaved/2026-10-14 21.05.07 Project"), "unsaved folder: " + unsaved.getFullPathName());

    const auto root = makeTempFolder ("copy");
    const auto from = root.getChildFile ("A Project");
    juce::String error;
    project::writeSave (from, "A", "<PROJECT schemaVersion=\"1\"/>", 1000, error);
    project::writeAutosave (from, "<PROJECT schemaVersion=\"1\"/>", 2000, error);
    from.getChildFile ("Samples/Imported").createDirectory();
    from.getChildFile ("Samples/Imported/kick.wav").replaceWithText ("RIFF");
    const auto to = root.getChildFile ("B Project");
    check (project::copyProjectFolder (from, to, error), "the folder is copied");
    check (to.getChildFile ("Samples/Imported/kick.wav").existsAsFile(), "samples are copied");
    check (to.getChildFile ("History/index.xml").existsAsFile(), "the history is copied");
    check (! to.getChildFile ("Backup").exists(), "the old autosave is not");
    check (! project::findMainFile (to).existsAsFile(), "and the main file is left for the save that follows");
    root.deleteRecursively();
}

//==============================================================================
void testMidiImport()
{
    std::printf ("[test] MIDI file import: first track with notes, bars rounded up\n");
    juce::MidiFile file;
    file.setTicksPerQuarterNote (480);
    juce::MidiMessageSequence conductor;
    conductor.addEvent (juce::MidiMessage::tempoMetaEvent (500000), 0);
    file.addTrack (conductor);

    juce::MidiMessageSequence notes;
    const auto add = [&notes] (int pitch, double startQ, double lengthQ, int vel) {
        notes.addEvent (juce::MidiMessage::noteOn (1, pitch, (juce::uint8) vel).withTimeStamp (startQ * 480), 0);
        notes.addEvent (juce::MidiMessage::noteOff (1, pitch).withTimeStamp ((startQ + lengthQ) * 480), 0);
    };
    add (60, 0.0, 1.0, 100);
    add (64, 1.0, 0.5, 90);
    add (67, 4.5, 1.0, 80); // ends at 5.5 beats: the clip needs two bars
    file.addTrack (notes);

    juce::MemoryOutputStream out;
    file.writeTo (out);
    juce::MemoryInputStream in (out.getData(), out.getDataSize(), false);

    project::ImportedClip clip;
    juce::String error;
    check (project::importMidiStream (in, "riff", 4.0, clip, error), "the file imports: " + error);
    check (clip.name == "riff" && clip.notes.size() == 3, "three notes from the second track");
    if (clip.notes.size() == 3)
    {
        check (clip.notes[0].pitch == 60 && std::abs (clip.notes[0].length - 1.0) < 1e-9 && clip.notes[0].velocity == 100, "first note: pitch, length, velocity");
        check (std::abs (clip.notes[1].start - 1.0) < 1e-9 && std::abs (clip.notes[1].length - 0.5) < 1e-9, "second note position");
        check (std::abs (clip.notes[2].start - 4.5) < 1e-9, "third note position");
    }
    check (std::abs (clip.lengthBeats - 8.0) < 1e-9, "5.5 beats become two bars (8 beats)");

    juce::MidiFile empty;
    empty.setTicksPerQuarterNote (96);
    empty.addTrack (conductor);
    juce::MemoryOutputStream out2;
    empty.writeTo (out2);
    juce::MemoryInputStream in2 (out2.getData(), out2.getDataSize(), false);
    check (! project::importMidiStream (in2, "empty", 4.0, clip, error) && error.isNotEmpty(), "a file without notes is refused with a message");
    juce::MemoryInputStream in3 ("junk", 4, false);
    check (! project::importMidiStream (in3, "junk", 4.0, clip, error), "a file that is not MIDI is refused");
}

//==============================================================================
void testUserLibrary()
{
    std::printf ("[test] user library: presets and chains on disk\n");
    const auto root = makeTempFolder ("library");
    library::UserLibrary lib (root);
    juce::String error;

    const auto f1 = lib.savePreset ("Prisma", "My Bass", "prisma", {}, "QUJD", error);
    check (f1.existsAsFile() && f1 == root.getChildFile ("Presets/Prisma/My Bass.dawpreset"), "preset path: Presets/<device>/<name>.dawpreset");
    const auto f2 = lib.savePreset ("Prisma", "My Bass", "prisma", {}, "QUJD", error);
    check (f2.getFileName() == "My Bass 2.dawpreset", "a taken name gets a number instead of overwriting");
    lib.savePreset ("Third Synth", "Wub", "plugin", "<PLUGIN name=\"Third Synth\"/>", "REVG", error);

    const auto data = library::UserLibrary::readPreset (f1);
    check (data.valid && data.deviceKind == "prisma" && data.name == "My Bass" && data.state == "QUJD", "a preset reads back");
    const auto xml = juce::XmlDocument::parse (f1);
    check (xml != nullptr && xml->hasTagName ("DAWPRESET") && xml->getIntAttribute ("version") == 1 && xml->hasAttribute ("created"),
           "the file is DAWPRESET version=1 with a creation time");

    auto list = lib.listPresets();
    check (list.size() == 3, "three presets are listed");
    int found = 0;
    for (const auto& p : list)
        if (p.deviceName == "Third Synth" && p.data.deviceKind == "plugin" && p.data.pluginDesc.contains ("Third Synth"))
            ++found;
    check (found == 1, "the plugin preset sits in the plugin's folder with its description");

    const auto renamed = library::UserLibrary::renamePreset (f1, "Deep Bass");
    check (renamed.existsAsFile() && ! f1.existsAsFile() && library::UserLibrary::readPreset (renamed).name == "Deep Bass", "rename changes the file and the name inside");
    check (library::UserLibrary::deleteFile (renamed) && lib.listPresets().size() == 2, "delete removes it from the list");
    juce::File (root.getChildFile ("Presets/Prisma/broken.dawpreset")).replaceWithText ("nope");
    check (lib.listPresets().size() == 2, "a broken file is skipped");

    juce::Array<juce::ValueTree> devices;
    juce::ValueTree d1 (ids::DEVICE);
    d1.setProperty (ids::id, 7, nullptr);
    d1.setProperty (ids::kind, "prisma", nullptr);
    d1.setProperty (ids::name, "Prisma", nullptr);
    d1.setProperty (ids::state, "QUJD", nullptr);
    juce::ValueTree d2 (ids::DEVICE);
    d2.setProperty (ids::id, 8, nullptr);
    d2.setProperty (ids::kind, "plugin", nullptr);
    d2.setProperty (ids::name, "Reverb", nullptr);
    devices.add (d1);
    devices.add (d2);
    const auto chain = lib.saveChain ("Lead chain", 5, devices, error);
    check (chain == root.getChildFile ("Chains/Lead chain.dawchain"), "chain path: Chains/<name>.dawchain");
    const auto cd = library::UserLibrary::readChain (chain);
    check (cd.valid && cd.colour == 5 && cd.devices.size() == 2 && cd.devices[0].getProperty (ids::state).toString() == "QUJD"
               && ! cd.devices[0].hasProperty (ids::id) && cd.devices[1].getProperty (ids::name).toString() == "Reverb",
           "a chain reads back: colour, devices in order, states, no ids");
    check (lib.listChains().size() == 1, "one chain listed");
    check (library::UserLibrary::renameChain (chain, "Pad chain").existsAsFile() && lib.listChains().size() == 1, "chains can be renamed");
    root.deleteRecursively();
}

//==============================================================================
void testSession()
{
    std::printf ("[test] project session: autosave, lock, crash recovery, save as, versions, restore\n");
    const auto root = makeTempFolder ("session");
    const auto music = root.getChildFile ("Music");
    juce::TemporaryFile settingsFile (".settings");
    Settings settings (settingsFile.getFile());
    library::UserLibrary lib (root.getChildFile ("Library"));
    ProjectModel model (ProjectModel::createEmpty());
    AudioEngine engine (settings, false);
    engine.setModel (&model);
    juce::File unsavedFolder;
    juce::String stateBeforeCrash;
    {
        project::ProjectSession session (engine, model, settings, lib, music);
        // Before any document is open (the recovery question is still on screen) nothing may be written, least of all the lock.
        project::RecoveryCheck::writeLock (project::ProjectSession::lockFileFor (settings), root.getChildFile ("lost project"));
        session.autosaveNow();
        session.flush();
        check (project::ProjectSession::lockFileFor (settings).existsAsFile() && ! music.exists(), "no autosave and no new lock before a document is open");
        project::RecoveryCheck::removeLock (project::ProjectSession::lockFileFor (settings));
        session.newProject();
        check (model.numTracks() == 2 && ! session.hasFile() && ! session.isDirty(), "a new project is the default one, not dirty, without a file");
        check (! session.save().wasOk(), "save needs a file first");
        session.autosaveNow();
        session.flush();
        check (! session.folder().exists(), "nothing changed: no autosave");

        // an edit, then the autosave
        model.undo.beginNewTransaction ("add");
        model.addTrack ("midi");
        session.noteEdited();
        check (session.isDirty(), "an edit makes it dirty");
        session.autosaveNow();
        session.flush();
        unsavedFolder = session.folder();
        check (unsavedFolder.getFullPathName().contains ("Prisma DAW/Unsaved/") && unsavedFolder.getFileName().endsWith (" Project"),
               "an unsaved project is autosaved into Music/Prisma DAW/Unsaved/<date> Project");
        check (project::autosaveFile (unsavedFolder).existsAsFile() && project::readVersions (unsavedFolder).size() == 1, "Backup/autosave.prisma and version 1");
        check (project::readVersions (unsavedFolder)[0].kind == daw::project::VersionKind::autosave, "which is an autosave");
        check (session.lockFile().existsAsFile() && session.lockFile().loadFileAsString() == unsavedFolder.getFullPathName(), "the lock names the folder");

        stateBeforeCrash = project::serialise (model.root);
        // no closeCleanly(): this is the crash
    }
    check (project::RecoveryCheck::check (project::ProjectSession::lockFileFor (settings)).needed, "after the crash the next start offers recovery");

    {
        ProjectModel model2 (ProjectModel::createEmpty());
        AudioEngine engine2 (settings, false);
        engine2.setModel (&model2);
        project::ProjectSession session (engine2, model2, settings, lib, music);
        const auto check1 = project::RecoveryCheck::check (session.lockFile());
        check (session.openRecovered (check1).wasOk(), "the autosave opens");
        check (model2.numTracks() == 3 && session.isDirty() && ! session.hasFile(), "with the edit, dirty, still without a file");

        // Save as: the project gets a folder, a name and a first saved version
        const auto target = root.getChildFile ("Projects/Night Drive Project");
        check (session.saveAs (target).wasOk(), "save as works");
        session.flush();
        check (project::mainFile (target, "Night Drive").existsAsFile() && model2.name() == "Night Drive" && session.hasFile() && ! session.isDirty(),
               "Night Drive.prisma exists, the name is the folder's, nothing is dirty");
        check (! unsavedFolder.exists(), "the temporary folder of the never-saved project is gone");
        auto versions = session.versions();
        check (versions.size() == 2 && versions.back().kind == daw::project::VersionKind::save, "the history came along and the save is its newest version");
        check (session.save().wasOk(), "Cmd+S saves again");
        check (session.versions().size() == 3, "every Cmd+S makes a version");
        check (! session.saveAs (target).wasOk(), "save as onto an existing project is refused");

        // change, autosave, label, restore
        model2.undo.beginNewTransaction ("add");
        model2.addTrack ("midi");
        session.noteEdited();
        check (model2.numTracks() == 4 && session.isDirty(), "a fourth track");
        session.autosaveNow();
        session.flush();
        versions = session.versions();
        check (versions.size() == 4 && versions.back().kind == daw::project::VersionKind::autosave, "the autosave is version 4");
        check (project::mainFile (target, "Night Drive").loadFileAsString().contains ("Night Drive"), "(the file on disk is the saved one)");
        check (session.setVersionLabel (3, "before the fourth track") && session.versions()[2].label == "before the fourth track", "a label is set on version 3");
        check (session.versionButtonText().startsWith ("v4 "), "the button shows the newest version: " + session.versionButtonText());

        const auto restored = session.restoreVersion (3);
        check (restored.wasOk() && model2.numTracks() == 3, "going back to version 3 gives three tracks");
        versions = session.versions();
        check (versions.size() == 5 && versions.back().kind == daw::project::VersionKind::restore, "the state before the return was saved as a \"restore\" version");
        const auto before = project::loadVersion (target, versions.back().number);
        check (before.ok() && before.tree.getChildWithName (ids::TRACKS).getNumChildren() == 4, "and it holds the four tracks");
        check (! model2.undo.canUndo(), "undo history is cleared");
        check (! session.restoreVersion (99).wasOk(), "an unknown version is an error");

        session.closeCleanly();
        check (! session.lockFile().existsAsFile(), "a normal exit removes the lock");
    }

    // open a saved project by its file
    {
        ProjectModel model3 (ProjectModel::createEmpty());
        AudioEngine engine3 (settings, false);
        engine3.setModel (&model3);
        project::ProjectSession session (engine3, model3, settings, lib, music);
        const auto file = root.getChildFile ("Projects/Night Drive Project/Night Drive.prisma");
        check (session.openFile (file).wasOk() && model3.name() == "Night Drive" && session.hasFile() && ! session.isDirty(), "a project opens by its file, clean");
        check (settings.recentProjects().contains (file.getFullPathName()), "and joins the recent projects");
        check (! session.openFile (root.getChildFile ("nothing.prisma")).wasOk(), "a missing file is an error");
        check (session.saveAsDefaultTemplate().wasOk() && lib.defaultTemplate().existsAsFile(), "save as default template writes Templates/Default.prisma");
        session.newProject();
        check (model3.numTracks() == 3 && ! session.hasFile() && model3.name().isEmpty(), "a new project starts from the template, without a name");
        session.closeCleanly();
    }
    root.deleteRecursively();
}
} // namespace

void tests::runProjectTests()
{
    testSaveAndLoad();
    testMissingPlugin();
    testSchemaVersion();
    testRecovery();
    testHistoryOnDisk();
    testFolderHelpers();
    testMidiImport();
    testUserLibrary();
    testSession();
}
