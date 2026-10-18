// The browser without a window (docs/spec/01-layout-project.md, tasks 1.5 and 1.6, test 11): the catalog and its
// search, the sources, favourites and recents, and what every drop does to the project.
#include "TestSupport.h"

#include "../app/Source/Strings.h"
#include "../app/Source/browser/BrowserActions.h"
#include "../app/Source/browser/BrowserSources.h"

using namespace tests;
using namespace browser;

namespace
{
juce::File makeTempFolder (const juce::String& tag)
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("daw-test-" + tag + "-" + juce::String (juce::Random::getSystemRandom().nextInt (1000000)));
    dir.createDirectory();
    return dir;
}

bool contains (const std::vector<Item>& items, const juce::String& name)
{
    for (const auto& i : items)
        if (i.name == name)
            return true;
    return false;
}

DragItem drag (const juce::String& kind, const juce::String& id = {}, const juce::String& path = {}, const juce::String& name = {}, bool instrument = true)
{
    DragItem d;
    d.kind = kind;
    d.id = id;
    d.path = path;
    d.name = name;
    d.instrument = instrument;
    return d;
}

//==============================================================================
void testSearch()
{
    std::printf ("[test 11] browser search: case, Cyrillic, empty text, groups\n");
    const auto root = makeTempFolder ("search");
    juce::TemporaryFile settingsFile (".settings");
    Settings settings (settingsFile.getFile());
    juce::KnownPluginList plugins;
    library::UserLibrary lib (root);
    juce::String error;
    lib.savePreset ("Prisma", juce::String::fromUTF8 ("Реестр-бас"), "prisma", {}, "QUJD", error);
    lib.savePreset ("Prisma", juce::String::fromUTF8 ("Ёлочка"), "prisma", {}, "QUJD", error);

    BrowserSources sources (settings, plugins, lib);
    const auto& catalog = sources.catalog();

    check (contains (catalog.items (Category::presets), "Reese Bass"), "the factory preset Reese Bass is in Presets");
    check (contains (catalog.items (Category::instruments), "Prisma"), "Prisma is in Instruments");
    check (contains (catalog.items (Category::presets), juce::String::fromUTF8 ("Реестр-бас")), "the user preset is in Presets");
    check (contains (catalog.items (Category::effects), Strings::fx::effectName ("eq8")) && contains (catalog.items (Category::effects), Strings::fx::effectName ("reverb")),
           "Effects lists the built-in effects (stage 6)");
    check (contains (catalog.items (Category::instruments), "909 Club"), "the drum kits are in Instruments");

    for (const auto* query : { "reese", "REESE", "Reese", "  reese  ", "eese" })
    {
        const auto found = catalog.search (query);
        check (found.size() == 1 && found[0].name == "Reese Bass", juce::String ("\"") + query + "\" finds Reese Bass and nothing else (" + juce::String ((int) found.size()) + ")");
    }
    for (const auto& query : { juce::String::fromUTF8 ("Рее"), juce::String::fromUTF8 ("РЕЕ"), juce::String::fromUTF8 ("рее"), juce::String::fromUTF8 ("бас") })
    {
        const auto found = catalog.search (query);
        check (contains (found, juce::String::fromUTF8 ("Реестр-бас")), "\"" + query + "\" finds the Cyrillic preset");
    }
    check (contains (catalog.search (juce::String::fromUTF8 ("елочка")), juce::String::fromUTF8 ("Ёлочка")), "yo and ye are the same letter");
    check (contains (catalog.search (juce::String::fromUTF8 ("ёлочка")), juce::String::fromUTF8 ("Ёлочка")), "and the other way round");
    check (catalog.search ("bass reese").size() == 1, "several words must all match");
    check (catalog.search ("zzzzzz").empty(), "nothing matches nonsense");

    const auto pads = catalog.search ("pad");
    check (contains (pads, "Glass Pad"), "the name matches");
    const auto folder = catalog.search ("Lead");
    check (contains (folder, "Supersaw Lead"), "the folder name matches, too");

    const auto everything = catalog.search ({});
    check ((int) everything.size() == catalog.total() && catalog.total() > 10, "an empty text returns everything (" + juce::String ((int) everything.size()) + ")");
    check (catalog.search ("   ").size() == everything.size(), "so does blank text");

    // grouped by category in the order of Category
    bool ordered = true;
    for (size_t i = 1; i < everything.size(); ++i)
        ordered = ordered && (int) everything[i - 1].category <= (int) everything[i].category;
    check (ordered, "results come grouped by category");

    // names that start with the text first
    std::vector<Item> items;
    Item a, b;
    a.name = "Deep Bass";
    a.group = "G";
    b.name = "Bass Deep";
    b.group = "G";
    items.push_back (a);
    items.push_back (b);
    Catalog c;
    c.set (Category::presets, items);
    const auto ranked = c.search ("bass");
    check (ranked.size() == 2 && ranked[0].name == "Bass Deep", "a name that starts with the text comes first");

    check (foldForSearch (juce::String::fromUTF8 ("ПрИвЕт Ёж")) == foldForSearch (juce::String::fromUTF8 ("привет еж")), "folding: Cyrillic capitals and yo");
    check (factoryPresetFolder ("Reese Bass") == "Bass" && factoryPresetFolder ("Vox Sweep") == "FX" && factoryPresetFolder ("Init") == "Init"
               && factoryPresetFolder ("Glass Pad") == "Pad" && factoryPresetFolder ("Pluck") == "Pluck" && factoryPresetFolder ("Supersaw Lead") == "Lead",
           "factory presets sit in Bass, Lead, Pad, Pluck, FX, Init");
    root.deleteRecursively();
}

void testDropFolder()
{
    std::printf ("[test] places: a dropped folder is linked once, a dropped file is ignored\n");
    const auto root = makeTempFolder ("dropfolder");
    const auto folder = root.getChildFile ("Samples");
    folder.createDirectory();
    const auto file = root.getChildFile ("loop.wav");
    file.replaceWithText ("x");
    juce::TemporaryFile settingsFile (".settings");
    Settings settings (settingsFile.getFile());
    juce::KnownPluginList plugins;
    library::UserLibrary lib (root.getChildFile ("lib"));
    BrowserSources sources (settings, plugins, lib);

    check (settings.placeFolders().isEmpty(), "no places to begin with");
    check (sources.addPlaceFolder (folder) && settings.placeFolders().size() == 1 && settings.placeFolders()[0] == folder.getFullPathName(),
           "a folder goes into the settings by path");
    check (folder.isDirectory() && folder.getChildFile ("loop.wav").exists() == false, "it is linked, nothing is copied into it");
    check (! sources.addPlaceFolder (folder) && settings.placeFolders().size() == 1, "the same folder twice is not added again");
    check (! sources.addPlaceFolder (file) && settings.placeFolders().size() == 1, "a file is ignored");
    check (! sources.addPlaceFolder (root.getChildFile ("missing")) && settings.placeFolders().size() == 1, "a missing path is ignored");
    check (sources.places().contains (folder), "the folder is among the places");
    root.deleteRecursively();
}

void testPlaceScan()
{
    std::printf ("[test] places: sounds and MIDI files are found, hidden and bookkeeping folders are not\n");
    const auto root = makeTempFolder ("places");
    root.getChildFile ("Drums/Kicks").createDirectory();
    root.getChildFile ("Drums/Kicks/kick one.wav").replaceWithText ("x");
    root.getChildFile ("Drums/snare.WAV").replaceWithText ("x");
    root.getChildFile ("loop.mid").replaceWithText ("x");
    root.getChildFile ("notes.txt").replaceWithText ("x");
    root.getChildFile (".hidden").createDirectory();
    root.getChildFile (".hidden/secret.wav").replaceWithText ("x");
    root.getChildFile ("History").createDirectory();
    root.getChildFile ("History/v0001.wav").replaceWithText ("x");

    const auto result = BrowserSources::scanFolder (root, "Mine");
    check (result.sounds.size() == 2 && result.midi.size() == 1 && result.places.size() == 3, "2 sounds, 1 MIDI file, 3 in Places (" + juce::String ((int) result.places.size()) + ")");
    bool grouped = false;
    for (const auto& s : result.sounds)
        grouped = grouped || (s.name == "kick one" && s.group == "Mine / Drums/Kicks" && s.kind == kinds::audioFile);
    check (grouped, "the group is the place and the folder inside it");
    check (result.midi[0].kind == kinds::midiFile && result.midi[0].path == root.getChildFile ("loop.mid").getFullPathName(), "the MIDI file keeps its path and kind");
    root.deleteRecursively();
}

void testFavouritesAndRecents()
{
    std::printf ("[test] favourites and recents in the settings\n");
    juce::TemporaryFile file (".settings");
    {
        Settings s (file.getFile());
        check (s.favouriteColour ("a") == -1, "no label by default");
        s.setFavouriteColour ("a", 2);
        s.setFavouriteColour ("file:/x/y z.wav", 5);
        s.setFavouriteColour ("b", 9);
        check (s.favouriteColour ("a") == 2 && s.favouriteColour ("file:/x/y z.wav") == 5 && s.favouriteColour ("b") == 6, "labels are stored (clamped to 7 colours)");
        s.setFavouriteColour ("a", -1);
        check (s.favouriteColour ("a") == -1 && s.favourites().size() == 2, "a label can be removed");
        for (int i = 0; i < 40; ++i)
            s.addRecentItem ("item" + juce::String (i));
        s.addRecentItem ("item30");
        const auto recent = s.recentItems();
        check (recent.size() == Settings::kMaxRecentItems && recent[0] == "item30" && recent[1] == "item39", "recents: 30, newest first, repeats move to the top");
        check (s.libraryPath().isEmpty() && ! s.previewEnabled() && ! s.firstRunDone() && s.midiInputPlays ("dev"), "defaults");
        s.setPreviewEnabled (true);
        s.setLibraryPath ("/somewhere");
        s.setMidiInputPlays ("dev", false);
        s.setFirstRunDone();
        s.save();
    }
    Settings s (file.getFile());
    check (s.favouriteColour ("file:/x/y z.wav") == 5 && s.recentItems().size() == 30 && s.previewEnabled() && s.libraryPath() == "/somewhere"
               && ! s.midiInputPlays ("dev") && s.midiInputPlays ("other") && s.firstRunDone(),
           "all of it survives a restart");
}

//==============================================================================
struct DropFixture
{
    DropFixture() : root (makeTempFolder ("drop")), library (root), actions (f.model, f.engine, library)
    {
        for (int i = 0; i < 4; ++i)
            f.model.insertScene (-1);
        trackA = addPrismaTrack (f.model, 0);
        trackB = addPrismaTrack (f.model, 0);
        f.model.undo.clearUndoHistory();
        f.engine.rebuildGraphIfNeeded();
    }
    ~DropFixture() { root.deleteRecursively(); }

    juce::String xml() const { return f.model.root.toXmlString(); }
    int presetIndex (const juce::String& name) const
    {
        const auto& p = daw::synth::factoryPresets();
        for (size_t i = 0; i < p.size(); ++i)
            if (name == juce::String (p[i].name))
                return (int) i;
        return 0;
    }
    DragItem presetDrag (const juce::String& name) const { return drag (kinds::prismaPreset, juce::String (presetIndex (name)), {}, name); }

    // runs `action`, checks it is one transaction and that undo and redo restore exactly
    template <typename F>
    void oneTransaction (const juce::String& what, const juce::String& expectedName, F&& action)
    {
        f.engine.captureDeviceStates (f.model.root); // removing a device stores its live state first, outside the history
        const auto before = xml();
        action();
        const auto after = xml();
        check (after != before, what + ": the project changed");
        check (f.model.undo.getUndoDescription() == expectedName, what + ": transaction \"" + f.model.undo.getUndoDescription() + "\" (expected \"" + expectedName + "\")");
        f.model.undo.undo();
        check (xml() == before, what + ": one undo restores the project exactly");
        f.model.undo.redo();
        check (xml() == after, what + ": redo repeats it");
        f.model.undo.undo();
        check (xml() == before, what + ": and undo again");
        f.model.undo.clearUndoHistory();
    }

    juce::File root;
    Fixture f;
    library::UserLibrary library;
    BrowserActions actions;
    TrackRef trackA, trackB;
};

void testDrops()
{
    std::printf ("[test] drops: Prisma presets, user presets, plugins, chains, MIDI files\n");
    DropFixture d;
    auto& model = d.f.model;
    auto& actions = d.actions;
    const auto reese = d.presetDrag ("Reese Bass");

    // --- what lights up
    check (actions.accepts (reese, DropTarget::newTrack) && actions.accepts (reese, DropTarget::track, d.trackA.id())
               && actions.accepts (reese, DropTarget::chainGap, d.trackA.id()),
           "a Prisma preset is accepted by an empty place, a track and the chain");
    check (actions.accepts (reese, DropTarget::device, d.trackA.id(), d.trackA.device (0).id()), "and by a Prisma device");
    check (! actions.accepts (drag (kinds::midiFile, {}, "/x.mid"), DropTarget::chainGap, d.trackA.id())
               && ! actions.accepts (drag (kinds::midiFile, {}, "/x.mid"), DropTarget::track, d.trackA.id())
               && actions.accepts (drag (kinds::midiFile, {}, "/x.mid"), DropTarget::slot, d.trackA.id())
               && actions.accepts (drag (kinds::midiFile, {}, "/x.mid"), DropTarget::newTrack),
           "a MIDI file is accepted by a slot and by an empty place, not by the chain");
    check (actions.accepts (drag (kinds::audioFile, {}, "/x.wav"), DropTarget::newTrack)
               && ! actions.accepts (drag (kinds::audioFile, {}, "/x.wav"), DropTarget::slot, d.trackA.id()),
           "stage 5: a sound file makes a new audio track, a MIDI track does not take it");
    check (! actions.accepts (reese, DropTarget::track, 123456), "a track that does not exist accepts nothing");
    check (actions.accepts (drag (kinds::chain, {}, "/x.dawchain", "c"), DropTarget::newTrack) && ! actions.accepts (drag (kinds::chain), DropTarget::device, d.trackA.id(), d.trackA.device (0).id()),
           "a chain: an empty place yes, a single device no");

    // --- a preset on an empty place: a new track
    d.oneTransaction ("preset on an empty place", Strings::undoAddDevice ("Prisma" + juce::String::fromUTF8 (" \xc2\xb7 ") + "Reese Bass"), [&] {
        const auto r = actions.drop (reese, DropTarget::newTrack, -1);
        check (r.ok && model.numTracks() == 3 && model.trackAt (2).id() == r.trackId, "a third track appears at the end");
        const auto track = model.trackAt (2);
        check (track.numDevices() == 1 && track.device (0).kind() == "prisma" && track.device (0).stateBase64().isNotEmpty(), "with Prisma and the preset state");
        check (track.name() == "Reese Bass" && track.numSlots() == model.numScenes(), "named after the preset, with a slot per scene");
    });

    // --- the state is the preset: a Prisma built from it reports the preset as its program
    {
        const auto r = actions.drop (reese, DropTarget::newTrack, -1);
        d.f.engine.rebuildGraphIfNeeded();
        auto* p = dynamic_cast<PrismaProcessor*> (d.f.engine.findProcessor (model.trackAt (2).device (0).id()));
        check (r.ok && p != nullptr && p->getProgramName (p->getCurrentProgram()) == "Reese Bass", "the live Prisma plays the dropped preset");
        model.undo.undo();
        d.f.engine.rebuildGraphIfNeeded();
        model.undo.clearUndoHistory();
    }

    // --- a preset on a track: replaces the instrument
    d.oneTransaction ("preset on a track", Strings::undoApplyPreset ("Prisma" + juce::String::fromUTF8 (" \xc2\xb7 ") + "Wobble Bass"), [&] {
        const auto oldId = d.trackA.device (0).id();
        const auto r = actions.drop (d.presetDrag ("Wobble Bass"), DropTarget::track, d.trackA.id());
        check (r.ok && d.trackA.numDevices() == 1 && d.trackA.device (0).id() != oldId && d.trackA.device (0).stateBase64() == d.presetDrag ("Wobble Bass").path + d.trackA.device (0).stateBase64(),
               "the instrument is replaced by a new device");
    });

    // --- a preset on a Prisma device: applied to it
    d.trackA.device (0).setEnabled (false, nullptr);
    d.oneTransaction ("preset on a device", Strings::undoApplyPreset ("Prisma" + juce::String::fromUTF8 (" \xc2\xb7 ") + "Glass Pad"), [&] {
        const auto r = actions.drop (d.presetDrag ("Glass Pad"), DropTarget::device, d.trackA.id(), -1, -1, d.trackA.device (0).id());
        check (r.ok && d.trackA.numDevices() == 1 && ! d.trackA.device (0).enabled(), "the device keeps its on/off state");
    });
    d.trackA.device (0).setEnabled (true, nullptr);

    // --- Prisma itself on a track with a user preset sitting where the instrument is
    {
        const auto before = d.xml();
        const auto r = actions.drop (drag (kinds::prisma), DropTarget::track, d.trackB.id());
        check (r.ok && d.trackB.numDevices() == 1 && d.trackB.device (0).kind() == "prisma", "Prisma on a track replaces the instrument with Prisma");
        model.undo.undo();
        check (d.xml() == before, "and undoes");
        model.undo.clearUndoHistory();
    }

    // --- an effect plugin (a description only: the drop works on the model)
    juce::PluginDescription fx;
    fx.name = "Test Reverb";
    fx.manufacturerName = "Acme";
    fx.pluginFormatName = "VST3";
    fx.fileOrIdentifier = "/no/such/Reverb.vst3";
    fx.uniqueId = 777;
    fx.isInstrument = false;
    d.f.engine.getKnownPlugins().addType (fx);
    juce::PluginDescription synth = fx;
    synth.name = "Test Synth";
    synth.fileOrIdentifier = "/no/such/Synth.vst3";
    synth.uniqueId = 778;
    synth.isInstrument = true;
    d.f.engine.getKnownPlugins().addType (synth);
    const auto fxDrag = drag (kinds::plugin, fx.createIdentifierString(), {}, fx.name, false);
    const auto synthDrag = drag (kinds::plugin, synth.createIdentifierString(), {}, synth.name, true);

    check (actions.accepts (fxDrag, DropTarget::newTrack) && actions.accepts (synthDrag, DropTarget::newTrack), "an effect and an instrument both make a new track");
    {
        auto& m = d.f.model;
        const int before = m.numTracks();
        const auto r = actions.drop (fxDrag, DropTarget::newTrack, -1, -1, -1, -1);
        check (r.ok && m.numTracks() == before + 1 && m.findTrack (r.trackId).isAudio(), "an effect dropped on empty space makes an audio track that carries it");
        m.undo.undo();
        check (m.numTracks() == before, "and is one undo step");
    }
    check (actions.accepts (fxDrag, DropTarget::track, d.trackA.id()) && ! actions.accepts (fxDrag, DropTarget::device, d.trackA.id(), d.trackA.device (0).id()),
           "an effect goes on a track, not on a device");

    d.oneTransaction ("effect on a track", Strings::undoAddEffect ("Test Reverb"), [&] {
        const auto r = actions.drop (fxDrag, DropTarget::track, d.trackA.id());
        check (r.ok && d.trackA.numDevices() == 2 && d.trackA.device (1).kind() == "plugin" && d.trackA.device (1).name() == "Test Reverb"
                   && d.trackA.device (1).pluginDesc().contains ("Test Reverb"),
               "an effect is added at the end of the chain with its description");
    });
    actions.drop (fxDrag, DropTarget::track, d.trackA.id());
    actions.drop (fxDrag, DropTarget::track, d.trackA.id());
    model.undo.clearUndoHistory();
    check (d.trackA.numDevices() == 3, "two effects on the track");
    d.oneTransaction ("effect in a gap", Strings::undoAddEffect ("Test Reverb"), [&] {
        const auto r = actions.drop (fxDrag, DropTarget::chainGap, d.trackA.id(), -1, 1);
        check (r.ok && d.trackA.numDevices() == 4 && d.trackA.device (1).kind() == "plugin", "dropped at the gap after the instrument");
    });
    {
        const auto r = actions.drop (fxDrag, DropTarget::chainGap, d.trackA.id(), -1, 0);
        check (r.ok && d.trackA.device (0).kind() == "prisma", "an effect is never put in front of the instrument");
        model.undo.undo();
    }
    d.oneTransaction ("plugin instrument on a track", Strings::undoReplaceInstrument ("Test Synth"), [&] {
        const auto r = actions.drop (synthDrag, DropTarget::track, d.trackB.id());
        check (r.ok && d.trackB.device (0).kind() == "plugin" && d.trackB.device (0).name() == "Test Synth", "a plugin instrument replaces the instrument");
    });
    // reset track A
    while (d.trackA.numDevices() > 1)
        d.trackA.devicesNode().removeChild (d.trackA.numDevices() - 1, nullptr);
    model.undo.clearUndoHistory();
    check (d.trackA.numDevices() == 1, "(track A is back to one device)");

    // --- user presets: save from a device, show in the library, drop again
    juce::String error;
    const auto file = actions.savePreset (d.trackA.device (0).id(), "My Lead", error);
    check (file.existsAsFile() && file == d.root.getChildFile ("Presets/Prisma/My Lead.dawpreset"), "the preset is saved in Presets/Prisma: " + error);
    const auto data = library::UserLibrary::readPreset (file);
    check (data.valid && data.deviceKind == "prisma" && data.state.isNotEmpty(), "with the state of the live device");

    auto* live = dynamic_cast<PrismaProcessor*> (d.f.engine.findProcessor (d.trackB.device (0).id()));
    check (live != nullptr, "track B has a live Prisma");
    if (live != nullptr)
    {
        auto* cutoff = live->apvts.getParameter ("flt_cutoff");
        cutoff->setValueNotifyingHost (0.1234f);
        const auto saved = actions.savePreset (d.trackB.device (0).id(), "Dark", error);
        check (saved.existsAsFile(), "a second preset is saved from the live parameters");
        const auto userDrag = drag (kinds::userPreset, "prisma", saved.getFullPathName(), "Dark");
        d.oneTransaction ("user preset on a track", Strings::undoApplyPreset ("Prisma" + juce::String::fromUTF8 (" \xc2\xb7 ") + "Dark"), [&] {
            const auto r = actions.drop (userDrag, DropTarget::track, d.trackA.id());
            check (r.ok, "dropped");
            d.f.engine.rebuildGraphIfNeeded();
            auto* p = dynamic_cast<PrismaProcessor*> (d.f.engine.findProcessor (d.trackA.device (0).id()));
            check (p != nullptr && std::abs (p->apvts.getParameter ("flt_cutoff")->getValue() - 0.1234f) < 0.002f, "the new device has the saved cutoff");
        });
        d.f.engine.rebuildGraphIfNeeded();
    }

    // --- chains
    {
        actions.drop (fxDrag, DropTarget::track, d.trackB.id());
        model.undo.clearUndoHistory();
        d.trackB.setColour (9, nullptr);
        const auto chainFile = actions.saveChain (d.trackB.id(), "Bass chain", error);
        check (chainFile.existsAsFile() && chainFile == d.root.getChildFile ("Chains/Bass chain.dawchain"), "the chain is saved in Chains: " + error);
        const auto chain = library::UserLibrary::readChain (chainFile);
        check (chain.valid && chain.colour == 9 && chain.devices.size() == 2, "with the devices and the colour of the track");
        const auto chainDrag = drag (kinds::chain, {}, chainFile.getFullPathName(), "Bass chain");
        d.oneTransaction ("chain on a track", Strings::undoReplaceChain ("Bass chain"), [&] {
            const auto r = actions.drop (chainDrag, DropTarget::track, d.trackA.id());
            check (r.ok && d.trackA.numDevices() == 2 && d.trackA.device (1).name() == "Test Reverb", "the whole chain of the track is replaced");
        });
        d.oneTransaction ("chain on an empty place", Strings::undoAddChain ("Bass chain"), [&] {
            const auto r = actions.drop (chainDrag, DropTarget::newTrack, -1);
            check (r.ok && model.numTracks() == 3 && model.trackAt (2).numDevices() == 2 && model.trackAt (2).colour() == 9, "a new track with the chain and its colour");
        });
    }

    // --- MIDI files
    {
        juce::MidiFile mf;
        mf.setTicksPerQuarterNote (96);
        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::noteOn (1, 62, (juce::uint8) 90).withTimeStamp (0), 0);
        seq.addEvent (juce::MidiMessage::noteOff (1, 62).withTimeStamp (96), 0);
        seq.addEvent (juce::MidiMessage::noteOn (1, 65, (juce::uint8) 70).withTimeStamp (96), 0);
        seq.addEvent (juce::MidiMessage::noteOff (1, 65).withTimeStamp (192), 0);
        mf.addTrack (seq);
        const auto midiFile = d.root.getChildFile ("riff.mid");
        juce::FileOutputStream out (midiFile);
        mf.writeTo (out);
        out.flush();

        d.oneTransaction ("MIDI file on a slot", Strings::undoAddMidiClip ("riff"), [&] {
            const auto r = actions.dropMidiFile (midiFile, DropTarget::slot, d.trackB.id(), 2);
            const auto clip = d.trackB.clipAt (2);
            check (r.ok && clip.isValid() && clip.numNotes() == 2 && clip.name() == "riff" && std::abs (clip.lengthBeats() - 4.0) < 1e-9, "a clip with the notes of the file in the slot");
        });
        d.oneTransaction ("MIDI file on an empty place", Strings::undoAddMidiClip ("riff"), [&] {
            const auto r = actions.dropMidiFile (midiFile, DropTarget::newTrack, -1);
            check (r.ok && model.numTracks() == 3 && model.trackAt (2).clipAt (0).isValid() && model.trackAt (2).numDevices() == 1, "a new track with Prisma and the clip in the first scene");
        });
        const auto before = d.xml();
        const auto bad = actions.dropMidiFile (d.root.getChildFile ("nothing.mid"), DropTarget::slot, d.trackB.id(), 0);
        check (! bad.ok && bad.message.isNotEmpty() && d.xml() == before, "a missing file changes nothing and says why");

        // double click: first empty slot of the selected track
        const auto r = actions.useOnTrack (drag (kinds::midiFile, {}, midiFile.getFullPathName(), "riff"), d.trackB.id());
        check (r.ok && d.trackB.clipAt (0).isValid(), "Enter on a MIDI file puts it into the first empty slot of the selected track");
        model.undo.undo();
    }

    // --- Enter / double click
    {
        const auto before = d.xml();
        const auto r = actions.useOnTrack (reese, d.trackA.id());
        check (r.ok && r.trackId == d.trackA.id() && model.numTracks() == 2, "a preset goes on the selected track");
        model.undo.undo();
        check (d.xml() == before, "and undoes");
        const auto r2 = actions.useOnTrack (reese, -1);
        check (r2.ok && model.numTracks() == 3, "with no track selected a new one is made");
        model.undo.undo();
    }
}

void testToneAndPreview()
{
    std::printf ("[test] settings test tone and browser pre-listening reach the output\n");
    Fixture f;
    FakeDevice device;
    f.engine.audioDeviceAboutToStart (&device);
    f.engine.rebuildGraphIfNeeded();
    Runner runner { f.engine };
    check (runner.run (10) < 1e-6, "silent at rest");

    f.engine.playTestTone();
    const double tone = runner.run (20);
    // 440 Hz sine at -20 dBFS: peak 0.1, RMS 0.0707
    check (tone > 0.05 && tone < 0.09 && runner.peak < 0.11f, "the test tone is a quiet sine");
    runner.run (120); // one second is about 94 blocks of 512
    check (! f.engine.isTestToneSounding() && runner.run (5) < 1e-6, "the test tone ends after a second");

    f.engine.setPreviewVolumeDb (-6.0f);
    f.engine.previewFactoryPreset (2);
    const double heard = runner.run (60);
    check (heard > 0.002, "a pre-listened preset is audible");
    f.engine.stopPreview();
    runner.run (10); // the fade-out
    check (runner.run (20) < 1e-5, "stopping the preview silences it");
    check (runner.nonFinite == 0, "no bad samples");
}
} // namespace

void tests::runBrowserTests()
{
    testSearch();
    testPlaceScan();
    testDropFolder();
    testFavouritesAndRecents();
    testDrops();
    testToneAndPreview();
}
