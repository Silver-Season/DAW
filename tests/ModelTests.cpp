// Tests 1 and 2 of docs/spec/01-layout-project.md (project model, undo, scene invariant), plus the small
// pieces of tasks 1.1-1.2 that have no test of their own: Strings and Settings.
#include "TestSupport.h"

#include "../app/Source/Strings.h"

using namespace tests;

namespace
{
juce::String xml (const ProjectModel& m)
{
    return m.root.toXmlString();
}

void testModelUndo()
{
    std::printf ("[test 1] model: every operation is one undoable transaction\n");
    auto model = ProjectModel::createEmpty();
    const auto initial = xml (model);
    int transactions = 0;
    const auto begin = [&] (const juce::String& name) {
        model.undo.beginNewTransaction (name);
        ++transactions;
    };

    std::vector<int64_t> trackIds;
    for (int i = 0; i < 3; ++i)
    {
        begin (Strings::undoNewTrack);
        trackIds.push_back (model.addTrack ("midi").id());
    }
    begin (Strings::undoNewScene);
    model.insertScene (-1);
    begin (Strings::undoNewScene);
    model.insertScene (-1);

    begin (Strings::undoNewClip);
    auto clip = model.createClip (model.findTrack (trackIds[1]), 1, 4.0, "Riff");
    // Ten notes added in scrambled order; the clip keeps them sorted by start, then pitch.
    const int starts[10] { 7, 2, 9, 0, 5, 3, 3, 8, 1, 6 };
    for (int i = 0; i < 10; ++i)
        clip.addNote ({ starts[i] * 0.5, 0.25, 60 + (i * 7) % 12, 90 }, &model.undo);

    begin (Strings::undoRenameTrack);
    model.findTrack (trackIds[0]).setName ("Bass", &model.undo);
    begin (Strings::undoDeleteTrack);
    model.deleteTrack (trackIds[2]);
    const auto final = xml (model);

    check (transactions == 8, "eight transactions were opened");
    check (model.numTracks() == 2 && model.numScenes() == 2, "2 tracks and 2 scenes remain");
    check (clip.numNotes() == 10, "the clip holds 10 notes");
    bool sorted = true;
    for (int i = 1; i < clip.numNotes(); ++i)
    {
        const auto a = clip.note (i - 1), b = clip.note (i);
        sorted = sorted && (a.start < b.start || (! (b.start < a.start) && a.pitch <= b.pitch));
    }
    check (sorted, "notes are sorted by start, then pitch");

    int undone = 0;
    while (model.undo.canUndo())
    {
        model.undo.undo();
        ++undone;
    }
    check (undone == 8, "undo goes back exactly eight transactions");
    check (xml (model) == initial, "undoing everything restores the original XML");

    int redone = 0;
    while (model.undo.canRedo())
    {
        model.undo.redo();
        ++redone;
    }
    check (redone == 8, "redo goes forward exactly eight transactions");
    check (xml (model) == final, "redoing everything restores the final XML");

    // Ids are never reused, not even after undo.
    model.undo.beginNewTransaction ("a");
    const auto a = model.addTrack ("midi").id();
    model.undo.undo();
    model.undo.beginNewTransaction ("b");
    const auto b = model.addTrack ("midi").id();
    check (a != b, "an id is not handed out twice after undo");
    std::set<int64_t> seen;
    bool unique = true;
    for (const auto& t : model.tracks())
        unique = unique && seen.insert (t.id()).second;
    check (unique, "track ids are unique");

    // History limit and defaults
    check (ProjectModel::kMaxUndoTransactions == 500, "history keeps 500 transactions");
    auto def = ProjectModel::createDefault();
    check (def.numTracks() == 2 && def.numScenes() == 8, "default project: 2 tracks, 8 scenes");
    check (def.trackAt (0).device (0).kind() == "prisma", "default tracks carry Prisma");
    check (! def.undo.canUndo(), "a new project has no undo history");
    check ((int) def.root.getProperty (ids::schemaVersion) == ProjectModel::kSchemaVersion, "schema version is written");

    // New tracks walk around the palette: 0, 2, 4, ...
    auto colours = ProjectModel::createEmpty();
    std::vector<int> seq;
    for (int i = 0; i < 10; ++i)
        seq.push_back (colours.addTrack ("midi").colour());
    check (seq[0] == 0 && seq[1] == 2 && seq[7] == 14 && seq[8] == 1 && seq[9] == 3, "track colours alternate around the palette");

    // Duplicating a track copies devices and clips with fresh ids.
    auto dup = ProjectModel::createDefault();
    dup.insertScene (-1);
    const auto src = dup.trackAt (0);
    dup.createClip (src, 0, 4.0, "Clip").addNote ({ 0.0, 1.0, 60, 100 }, &dup.undo);
    const auto copy = dup.duplicateTrack (src.id());
    check (dup.numTracks() == 3 && dup.indexOfTrack (copy.id()) == 1, "the duplicate sits right after the original");
    check (copy.id() != src.id() && copy.device (0).id() != src.device (0).id() && copy.clipAt (0).id() != src.clipAt (0).id(),
           "the duplicate has new ids all the way down");
    check (copy.clipAt (0).numNotes() == 1 && copy.numSlots() == dup.numScenes(), "the duplicate keeps notes and slots");
}

void testSceneInvariant()
{
    std::printf ("[test 2] model: tracks always have as many slots as there are scenes\n");
    auto model = ProjectModel::createDefault(); // 2 tracks, 8 scenes
    model.addTrack ("midi");                    // 3 tracks

    // mirror[track][scene] = name of the clip, or "" for an empty slot
    std::vector<std::vector<juce::String>> mirror (3, std::vector<juce::String> (8));
    const auto put = [&] (int track, int scene, const juce::String& name) {
        model.createClip (model.trackAt (track), scene, 4.0, name);
        mirror[(size_t) track][(size_t) scene] = name;
    };
    put (0, 0, "a0");
    put (0, 3, "a3");
    put (1, 1, "b1");
    put (1, 7, "b7");
    put (2, 4, "c4");

    const auto verify = [&] (const juce::String& after) {
        bool counts = true, clips = true;
        for (int t = 0; t < model.numTracks(); ++t)
        {
            const auto track = model.trackAt (t);
            counts = counts && track.numSlots() == model.numScenes();
            for (int s = 0; s < model.numScenes() && s < track.numSlots(); ++s)
            {
                const auto clip = track.clipAt (s);
                const juce::String expected = mirror[(size_t) t][(size_t) s];
                clips = clips && (clip.isValid() ? clip.name() : juce::String()) == expected;
            }
        }
        check (counts && model.numScenes() == (int) mirror[0].size(), "slot count equals scene count after " + after);
        check (clips, "clips stayed in their scenes after " + after);
    };
    verify ("setup");

    model.undo.beginNewTransaction ("insert at 0");
    model.insertScene (0);
    for (auto& row : mirror)
        row.insert (row.begin(), juce::String());
    verify ("inserting a scene at the top");

    model.undo.beginNewTransaction ("insert in the middle");
    model.insertScene (5);
    for (auto& row : mirror)
        row.insert (row.begin() + 5, juce::String());
    verify ("inserting a scene in the middle");

    model.undo.beginNewTransaction ("append");
    model.insertScene (-1);
    for (auto& row : mirror)
        row.push_back ({});
    verify ("appending a scene");

    model.undo.beginNewTransaction ("delete");
    model.deleteScene (1); // removes the scene that holds "a0"
    for (auto& row : mirror)
        row.erase (row.begin() + 1);
    verify ("deleting a scene");

    model.undo.beginNewTransaction ("duplicate");
    model.duplicateScene (3); // "b1" moved to index 2, "a3" to index 3 after the delete above
    for (auto& row : mirror)
        row.insert (row.begin() + 4, row[3]);
    verify ("duplicating a scene");
    check (model.trackAt (0).clipAt (3).id() != model.trackAt (0).clipAt (4).id(), "the duplicated clip has its own id");

    model.undo.beginNewTransaction ("new track");
    model.addTrack ("midi"); // a track created later gets one slot per scene as well
    mirror.push_back (std::vector<juce::String> (mirror[0].size()));
    verify ("adding a track");

    // Undo everything: the invariant holds in every intermediate state.
    bool alwaysHolds = true;
    while (model.undo.canUndo())
    {
        model.undo.undo();
        for (const auto& t : model.tracks())
            alwaysHolds = alwaysHolds && t.numSlots() == model.numScenes();
    }
    check (alwaysHolds && model.numScenes() == 8, "slots match scenes in every state while undoing");

    // Clips can move and be copied between slots.
    auto moved = ProjectModel::createDefault();
    const auto t0 = moved.trackAt (0), t1 = moved.trackAt (1);
    moved.createClip (t0, 0, 4.0, "m");
    moved.moveClip (t0.id(), 0, t1.id(), 5, false);
    check (! t0.clipAt (0).isValid() && t1.clipAt (5).name() == "m", "moveClip moves");
    moved.moveClip (t1.id(), 5, t0.id(), 2, true);
    check (t1.clipAt (5).isValid() && t0.clipAt (2).isValid() && t1.clipAt (5).id() != t0.clipAt (2).id(), "moveClip with copy copies");
}

void testStringsAndSettings()
{
    std::printf ("[test] Strings and Settings\n");
    check (Strings::trackTypeMidi.isNotEmpty() && Strings::undoNewTrack.containsNonWhitespaceChars(), "strings exist");
    check (Strings::utf8 ("Новая дорожка").length() == 13, "Russian strings are decoded as UTF-8 (13 characters for the \"new track\" name)");
    check (Strings::undoNewTrack == (Strings::isRussian() ? Strings::utf8 ("Новая дорожка") : juce::String ("New Track")),
           "the UI strings follow the language of this run (DAW_LANG)");
    check (Strings::defaultTrackName (3).endsWith ("3"), "default track names are numbered");

    {
        // The language for the next launch sits in a "language" file next to the settings file.
        juce::TemporaryFile folder;
        folder.getFile().createDirectory();
        const auto settingsFile = folder.getFile().getChildFile ("DAW.settings");
        {
            Settings s (settingsFile);
            check (s.language() == "en", "the UI language is English by default");
            s.setLanguage ("ru");
        }
        check (Settings (settingsFile).language() == "ru", "the language choice survives a restart");
        check (Strings::languageFromCode (Settings::languageFileFor (settingsFile).loadFileAsString()) == Strings::Language::russian,
               "and is read back from the language file");
        folder.getFile().deleteRecursively();
    }

    juce::TemporaryFile file (".settings");
    {
        Settings s (file.getFile());
        check (s.theme () == "glass-dark" && juce::exactlyEqual (s.uiScale (), 1.0) && s.animations (), "defaults");
        check (std::abs (s.previewVolumeDb() + 12.0) < 1e-9, "preview volume defaults to -12 dB");
        s.setTheme ("neon");
        s.setUiScale (2.5); // clamped to 1.5
        s.setAnimations (false);
        s.setPlaceFolders ({ "/a", "/b" });
        for (int i = 0; i < 12; ++i)
            s.addRecentProject (juce::File ("/projects/p" + juce::String (i) + ".prisma"));
        s.addRecentProject (juce::File ("/projects/p5.prisma")); // moves to the top, no duplicate
        s.setChannelName ("Card", true, 2, "Guitar");
        juce::XmlElement state ("DEVICESETUP");
        state.setAttribute ("deviceType", "ALSA");
        s.setAudioDeviceState (state);
        s.save();
    }
    Settings s (file.getFile());
    check (s.theme() == "neon" && juce::exactlyEqual (s.uiScale(), 1.5) && ! s.animations(), "values survive a restart");
    check (s.placeFolders().size() == 2, "place folders are stored");
    const auto recent = s.recentProjects();
    check (recent.size() == 10 && recent[0].endsWith ("p5.prisma") && [&] { auto copy = recent; copy.removeDuplicates (false); return copy.size() == 10; }(),
           "recent projects: at most 10, most recent first, no duplicates");
    check (s.channelName ("Card", true, 2) == "Guitar" && s.channelName ("Card", false, 2).isEmpty(), "channel names are per device and direction");
    const auto restored = s.audioDeviceState();
    check (restored != nullptr && restored->getStringAttribute ("deviceType") == "ALSA", "audio device state round-trips");
}
} // namespace

void tests::runModelTests()
{
    testModelUndo();
    testSceneInvariant();
    testStringsAndSettings();
}
