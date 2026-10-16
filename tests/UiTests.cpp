// Tests of the window layer that need no window: the themes (test 12 of docs/spec/01-layout-project.md), the
// model side of every interface action (one undo transaction each), the edit commands, number parsing and
// the info bar's help lookup.
#include "TestSupport.h"

#include <BinaryData.h>

#include "../app/Source/Strings.h"
#include "../app/Source/model/ProjectActions.h"
#include "../app/Source/ui/Chrome.h"
#include "../app/Source/ui/Controls.h"
#include "../app/Source/ui/EditCommands.h"
#include "../app/Source/ui/Theme.h"

using namespace tests;

namespace
{
juce::String hex (juce::Colour c)
{
    return "#" + juce::String::toHexString ((int) (c.getARGB() & 0xffffff)).paddedLeft ('0', 6).toLowerCase();
}

//==============================================================================
void testThemes()
{
    std::printf ("[test 12] themes: four JSON files, all tokens, 16-colour palette\n");
    auto& manager = ThemeManager::get();
    const auto problems = manager.reload();
    check (problems.isEmpty(), "all built-in theme files parse");
    check (manager.all().size() == 4, "four built-in themes");

    struct Expected
    {
        const char *id, *background, *chrome, *panel, *panel2, *line, *text, *dimText, *accent, *accent2, *record, *play, *clipInk;
        int saturation, lightness;
        float glow, radius;
        bool scanlines;
        const char *fontUi, *fontNum;
    };
    const Expected table[] {
        { "prism", "#07080d", "#0c0e15", "#121520", "#181c29", "#232838", "#e6e8f2", "#7f869c", "#8be9ff", "#c59bff", "#ff4d6d", "#6bf0a6", "#0b0d14", 78, 62, 1.0f, 6.0f, false, "Onest", "JetBrains Mono" },
        { "neon", "#040208", "#090511", "#0f0919", "#160d24", "#2d1946", "#f4eaff", "#8e7aab", "#00f0ff", "#ff2bd6", "#ff2b4f", "#c3ff1a", "#07030d", 100, 58, 1.6f, 0.0f, true, "JetBrains Mono", "JetBrains Mono" },
        { "studio", "#161718", "#232426", "#2b2c2f", "#333438", "#3e4044", "#dcd8cf", "#8f8a82", "#ffb547", "#7fc8b2", "#e5484d", "#93d36c", "#1a1b1c", 42, 62, 0.0f, 3.0f, false, "Onest", "JetBrains Mono" },
        { "paper", "#e3e7ee", "#f6f7f9", "#ffffff", "#eef1f5", "#d5dae3", "#1b2130", "#667085", "#2457e6", "#b5179e", "#e5243f", "#0f9a4c", "#141a26", 72, 74, 0.0f, 6.0f, false, "Onest", "JetBrains Mono" },
    };
    for (const auto& e : table)
    {
        const Theme* found = nullptr;
        for (const auto& t : manager.all())
            if (t.id == e.id)
                found = &t;
        check (found != nullptr, juce::String ("theme present: ") + e.id);
        if (found == nullptr)
            continue;
        const auto& t = *found;
        const juce::String name = e.id;
        check (hex (t.background) == e.background && hex (t.chrome) == e.chrome && hex (t.panel) == e.panel && hex (t.panel2) == e.panel2,
               name + ": surface colours match the table");
        check (hex (t.line) == e.line && hex (t.text) == e.text && hex (t.dimText) == e.dimText, name + ": line and text colours match");
        check (hex (t.accent) == e.accent && hex (t.accent2) == e.accent2 && hex (t.record) == e.record && hex (t.play) == e.play
                   && hex (t.clipInk) == e.clipInk,
               name + ": accent, record, play and clip ink match");
        check (std::abs (t.clipSaturation * 100.0f - (float) e.saturation) < 0.01f && std::abs (t.clipLightness * 100.0f - (float) e.lightness) < 0.01f,
               name + ": palette saturation and lightness");
        check (std::abs (t.glow - e.glow) < 1e-4f && std::abs (t.radius - e.radius) < 1e-4f && t.scanlines == e.scanlines, name + ": glow, radius, scanlines");
        check (t.fontUi == e.fontUi && t.fontNum == e.fontNum, name + ": fonts");

        std::set<juce::uint32> distinct;
        for (int i = 0; i < Theme::kPaletteSize; ++i)
            distinct.insert (t.palette (i).getARGB());
        check ((int) distinct.size() == Theme::kPaletteSize, name + ": palette has 16 distinct colours");
        check (t.palette (0) == juce::Colour::fromHSL (t.hues[0] / 360.0f, t.clipSaturation, t.clipLightness, 1.0f), name + ": palette colour 0 is HSL of hue 0");
        check (t.clipColour (3, false).getAlpha() == 140 && t.clipColour (3, true).getAlpha() == 255, name + ": inactive clips are drawn at 55 % opacity");
    }

    for (const auto& th : manager.all())
        for (int i = 0; i < Theme::kPaletteSize; ++i)
            check (Theme::contrast (th.textPalette (i), th.panel) >= 4.5f && Theme::contrast (th.textPalette (i), th.chrome) >= 4.5f,
                   th.id + ": palette colour " + juce::String (i) + " as text has contrast 4.5:1");

    const auto prism = manager.all()[0];
    const auto neon = manager.all()[1];
    check ((int) prism.hues[0] == 350 && (int) prism.hues[15] == 328 && (int) neon.hues[0] == 318 && (int) neon.hues[15] == 335, "spectrum and neon hue lists");

    // A theme without a token is refused with the name of the token.
    Theme broken;
    juce::String error;
    check (! Theme::fromJson ("{\"id\":\"x\",\"name\":\"X\"}", broken, error) && error.contains ("background"), "missing token is reported by name");
    check (! Theme::fromJson ("not json", broken, error), "garbage is not a theme");
    auto json = juce::String::fromUTF8 (BinaryData::prism_json, BinaryData::prism_jsonSize);
    check (! Theme::fromJson (json.replace ("\"accent\": \"#8be9ff\"", "\"accent\": \"zzz\""), broken, error) && error.contains ("accent"), "bad colour is reported");

    // User themes from a folder: valid ones join the list, a broken one is reported and skipped.
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("daw-theme-test-" + juce::String (juce::Random::getSystemRandom().nextInt (1000000)));
    dir.createDirectory();
    dir.getChildFile ("mine.json").replaceWithText (json.replace ("\"id\": \"prism\"", "\"id\": \"mine\"").replace ("\"name\": \"Призма\"", "\"name\": \"Mine\""));
    dir.getChildFile ("broken.json").replaceWithText ("{ \"id\": \"broken\" }");
    const auto userProblems = manager.reload (dir);
    check (manager.all().size() == 5 && manager.all().back().id == "mine", "a theme from User Library/Themes appears in the list");
    check (userProblems.size() == 1 && userProblems[0].startsWith ("broken.json"), "a broken user theme is reported, the others load");
    manager.setCurrent ("mine");
    check (theme().id == "mine", "current theme can be switched");
    manager.setCurrent ("no-such-theme");
    check (theme().id == "prism", "unknown theme id falls back to the first theme");
    dir.deleteRecursively();
    manager.reload();

    // Fonts come from the embedded files, with different weights.
    manager.setCurrent ("prism");
    const auto regular = theme().uiFont (13.0f, 400), semibold = theme().uiFont (13.0f, 600), mono = theme().numFont (13.0f);
    check (regular.getTypefacePtr() != nullptr && regular.getTypefacePtr()->getName().startsWith ("Onest"), "UI font is Onest");
    check (semibold.getTypefacePtr() != nullptr && semibold.getTypefacePtr() != regular.getTypefacePtr(), "semibold is a different face than regular");
    check (mono.getTypefacePtr() != nullptr && mono.getTypefacePtr()->getName().startsWith ("JetBrains Mono"), "number font is JetBrains Mono");
    check (std::abs (juce::GlyphArrangement::getStringWidth (mono, "1111") - juce::GlyphArrangement::getStringWidth (mono, "8888")) < 0.01f, "digits of the number font have equal widths");
    manager.setCurrent ("neon");
    check (theme().uiFont (13.0f).getTypefacePtr()->getName().startsWith ("JetBrains Mono"), "Neon uses the monospace font for the UI");
    manager.setCurrent ("prism");
}

//==============================================================================
// A project with two tracks, eight scenes, and a few clips; each action must be one transaction.
struct ActionFixture
{
    ActionFixture() : model (ProjectModel::createDefault()), actions (model)
    {
        track0 = model.trackAt (0);
        track1 = model.trackAt (1);
        putClip (model, track0, 0, daw::engine::demo::padClip());
        putClip (model, track0, 1, daw::engine::demo::arpClip());
        putClip (model, track1, 0, daw::engine::demo::bassClip (false));
        model.undo.clearUndoHistory();
    }

    juce::String xml() const { return model.root.toXmlString(); }

    // Runs `action` and checks that it changed the model, that one undo restores it exactly and that one redo repeats it.
    template <typename F>
    void oneTransaction (const juce::String& what, F&& action)
    {
        model.undo.clearUndoHistory();
        const auto before = xml();
        action();
        const auto after = xml();
        check (after != before, what + ": changes the project");
        check (model.undo.getNumActionsInCurrentTransaction() > 0 || model.undo.canUndo(), what + ": is recorded in the history");
        model.undo.undo();
        check (xml() == before, what + ": one undo restores the project exactly");
        check (! model.undo.canUndo(), what + ": was a single transaction");
        model.undo.redo();
        check (xml() == after, what + ": redo repeats it");
        model.undo.clearUndoHistory();
    }

    ProjectModel model;
    ProjectActions actions;
    TrackRef track0, track1;
};

void testActionTransactions()
{
    std::printf ("[test ui-1] interface actions: rename, colour, duplicate, delete, move are one transaction each\n");

    ActionFixture f;
    auto& a = f.actions;
    auto& m = f.model;
    const auto t0 = f.track0.id(), t1 = f.track1.id();

    f.oneTransaction ("rename track", [&] { a.renameTrack (t0, "Bassline"); });
    check (m.findTrack (t0).name() == "Bassline", "track name changed");
    f.oneTransaction ("track colour", [&] { a.setTrackColour (t0, 9); });
    check (m.findTrack (t0).colour() == 9, "track colour is a palette index");
    f.oneTransaction ("new MIDI track", [&] { a.addMidiTrack (1); });
    check (m.numTracks() == 3 && m.trackAt (1).numDevices() == 1 && m.trackAt (1).clipAt (7).isValid() == false && m.trackAt (1).numSlots() == 8,
           "a new track gets Prisma and one slot per scene");
    {
        ActionFixture g;
        const auto g0 = g.track0.id();
        g.oneTransaction ("duplicate track", [&] { g.actions.duplicateTrack (g0); });
        check (g.model.numTracks() == 3 && g.model.trackAt (1).clipAt (0).isValid() && g.model.trackAt (1).id() != g0, "the copy has the clips and new ids");
        g.oneTransaction ("delete track with clips and devices", [&] { g.actions.deleteTrack (g0); });
        check (g.model.numTracks() == 2 && ! g.model.findTrack (g0).isValid(), "track removed");
    }

    f.oneTransaction ("rename clip", [&] { a.renameClip (t0, 0, "Intro pad"); });
    f.oneTransaction ("clip colour", [&] { a.setClipColour (t0, 0, 14); });
    f.oneTransaction ("empty clip", [&] { a.createEmptyClip (t1, 3); });
    check (m.findTrack (t1).clipAt (3).isValid() && std::abs (m.findTrack (t1).clipAt (3).lengthBeats() - 4.0) < 1e-9, "an empty clip is one 4/4 bar long");
    f.oneTransaction ("duplicate clip", [&] { a.duplicateClip (t0, 0); });
    check (! m.findTrack (t0).clipAt (0).isValid() || m.findTrack (t0).clipAt (0).id() > 0, "(clip ids)");
    f.oneTransaction ("delete clip", [&] { a.deleteClip (t0, 1); });
    f.oneTransaction ("move clip to another slot", [&] { a.moveClip (t0, 0, t1, 5, false); });
    check (! m.findTrack (t0).clipAt (0).isValid() && m.findTrack (t1).clipAt (5).isValid() && m.findTrack (t1).clipAt (5).numNotes() > 0,
           "the clip left its slot and arrived with its notes");
    f.oneTransaction ("copy clip with Alt", [&] { a.moveClip (t0, 2, t1, 6, true); });
    check (m.findTrack (t0).clipAt (2).isValid() && m.findTrack (t1).clipAt (6).isValid(), "copy keeps the original");
    f.oneTransaction ("move clip over an occupied slot", [&] { a.moveClip (t0, 2, t1, 0, false); });
    f.oneTransaction ("cut clip", [&] { a.cutClip (t1, 3); });
    check (a.hasClipboard(), "cut fills the clipboard");
    f.oneTransaction ("paste clip", [&] { a.pasteClip (t1, 7); });

    // Duplicating a clip with no free slot below adds a scene.
    {
        ActionFixture g;
        for (int s = 1; s < 8; ++s)
            g.model.createClip (g.track0, s, 4.0);
        g.model.undo.clearUndoHistory();
        g.oneTransaction ("duplicate clip at the bottom adds a scene", [&] { g.actions.duplicateClip (g.track0.id(), 7); });
        check (g.model.numScenes() == 9 && g.model.trackAt (0).clipAt (8).isValid(), "the new scene holds the copy");
    }

    {
        ActionFixture g;
        auto& ga = g.actions;
        const auto g0 = g.track0.id(), g1 = g.track1.id();
        g.oneTransaction ("insert scene", [&] { ga.insertScene (2); });
        g.oneTransaction ("rename scene", [&] { ga.renameScene (1, "Drop"); });
        g.oneTransaction ("scene colour", [&] { ga.setSceneColour (1, 4); });
        g.oneTransaction ("duplicate scene", [&] { ga.duplicateScene (0); });
        check (g.model.numScenes() == 10 && g.model.findTrack (g0).clipAt (1).isValid() && g.model.findTrack (g0).clipAt (0).isValid(), "the copied scene has the clips");
        g.oneTransaction ("delete scene", [&] { ga.deleteScene (0); });
        check (g.model.numScenes() == 9, "scene removed");
        g.oneTransaction ("capture scene", [&] { ga.captureScene (2, { { g0, 0 }, { g1, 0 } }); });
        check (g.model.numScenes() == 10 && g.model.findTrack (g0).clipAt (3).isValid() && g.model.findTrack (g1).clipAt (3).isValid(),
               "playing clips are copied into the new scene");
    }

    {
        ActionFixture g;
        // Slot numbers before the insertion: the clips of scenes below the new one moved down by one.
        g.actions.captureScene (0, { { g.track0.id(), 1 } });
        check (g.model.numScenes() == 9 && g.model.trackAt (0).clipAt (1).isValid() && g.model.trackAt (0).clipAt (1).name() == g.model.trackAt (0).clipAt (2).name(),
               "capture after scene 0 copies the clip of the scene that moved to slot 2");
    }

    f.oneTransaction ("mute", [&] { a.setTrackMute (t0, true); });
    f.oneTransaction ("solo", [&] { a.setTrackSolo (t1, true, true); });
    check (m.findTrack (t1).solo(), "solo on");
    f.oneTransaction ("exclusive arm", [&] { a.setTrackArm (t0, true, true); });
    {
        ActionFixture g;
        g.actions.setTrackSolo (g.track0.id(), true, true);
        g.actions.setTrackSolo (g.track1.id(), true, true);
        check (! g.track0.solo() && g.track1.solo(), "a plain click on Solo leaves only one track soloed");
        g.actions.setTrackSolo (g.track0.id(), true, false);
        check (g.track0.solo() && g.track1.solo(), "Cmd-click adds to the soloed tracks");
    }

    f.oneTransaction ("device on/off", [&] { a.setDeviceEnabled (f.track0.device (0).id(), false); });
    f.oneTransaction ("fold device", [&] { a.setDeviceFolded (f.track0.device (0).id(), true); });
    f.oneTransaction ("remove device", [&] { a.removeDevice (f.track0.device (0).id()); });
    {
        ActionFixture g;
        g.model.addDevice (g.track0, "prisma", "Second", -1);
        g.model.undo.clearUndoHistory();
        g.oneTransaction ("move device", [&] { g.actions.moveDevice (g.track0.device (1).id(), 0); });
    }

    // Things that do nothing are not recorded as undoable edits.
    {
        ActionFixture g;
        for (int i = g.model.numScenes() - 1; i > 0; --i)
            g.model.deleteScene (i);
        g.model.undo.clearUndoHistory();
        const auto before = g.xml();
        check (! g.actions.deleteScene (0), "the last scene cannot be deleted");
        g.actions.renameTrack (g.track0.id(), "   ");
        g.actions.renameTrack (g.track0.id(), g.track0.name());
        check (! g.actions.deleteClip (g.track1.id(), 4) && ! g.actions.duplicateClip (g.track1.id(), 4), "empty slots have nothing to delete or duplicate");
        check (g.xml() == before && ! g.model.undo.canUndo(), "no-op actions leave the project and the history alone");
    }
}

//==============================================================================
void testActionsWithEngine()
{
    std::printf ("[test ui-2] deleting and duplicating tracks with a running engine keeps the live parameters\n");
    Fixture f;
    f.model.addTrack ("midi");
    f.model.insertScene (-1);
    auto track = f.model.trackAt (0);
    auto device = f.model.addDevice (track, "prisma", "Prisma");
    device.setStateBase64 (prismaState (1), &f.model.undo);
    putClip (f.model, track, 0, daw::engine::demo::padClip());
    f.model.undo.clearUndoHistory();
    f.engine.rebuildGraph();

    // Change a parameter after the state was stored: only the live processor knows about it.
    auto* processor = f.engine.findProcessor (device.id());
    check (processor != nullptr, "the engine made the processor");
    auto* cutoff = dynamic_cast<juce::AudioProcessorParameterWithID*> (processor->getParameters()[0]);
    (void) cutoff;
    for (auto* p : processor->getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p); withId != nullptr && withId->paramID == "flt_cutoff")
            p->setValueNotifyingHost (0.123f);

    ProjectActions actions (f.model, &f.engine);
    const auto id = track.id();
    actions.deleteTrack (id);
    f.engine.rebuildGraph();
    check (f.model.numTracks() == 0, "the track is gone");
    f.model.undo.undo();
    f.engine.rebuildGraph();
    check (f.model.numTracks() == 1 && f.model.trackAt (0).clipAt (0).isValid() && f.model.trackAt (0).numDevices() == 1, "one undo brings back the track, its clip and device");
    auto* restored = f.engine.findProcessor (f.model.trackAt (0).device (0).id());
    float value = -1.0f;
    if (restored != nullptr)
        for (auto* p : restored->getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p); withId != nullptr && withId->paramID == "flt_cutoff")
                value = p->getValue();
    check (std::abs (value - 0.123f) < 0.01f, "the filter cutoff set before the deletion is back after undo");

    auto copy = actions.duplicateTrack (f.model.trackAt (0).id());
    check (copy.isValid() && f.model.numTracks() == 2, "duplicate through the engine path");
    f.model.undo.undo();
    check (f.model.numTracks() == 1, "one undo removes the duplicate");
}

//==============================================================================
void testEditCommands()
{
    std::printf ("[test ui-3] edit commands act on the selection\n");
    Fixture f;
    auto& m = f.model;
    m.addTrack ("midi");
    m.addTrack ("midi");
    m.insertScene (-1);
    m.insertScene (-1);
    m.insertScene (-1);
    const auto ids = std::array<int64_t, 2> { m.trackAt (0).id(), m.trackAt (1).id() };
    putClip (m, m.trackAt (0), 0, daw::engine::demo::padClip());
    m.undo.clearUndoHistory();

    ProjectActions actions (m, &f.engine);
    ui::SelectionState selection;
    ui::EditCommands commands (f.engine, m, actions, selection);

    check (! commands.canDelete() && ! commands.canDuplicate() && ! commands.canPaste(), "nothing selected: nothing to do");

    commands.selectSlot (ids[0], 0);
    check (commands.canDelete() && commands.canDuplicate() && commands.canCopy() && ! commands.canPaste(), "a clip is selected");
    commands.copySelection();
    commands.selectSlot (ids[1], 2);
    check (commands.canPaste() && ! commands.canDelete(), "an empty slot can take a paste but cannot be deleted");
    commands.pasteSelection();
    check (m.trackAt (1).clipAt (2).isValid(), "paste puts the clip into the selected slot");
    m.undo.undo();
    check (! m.trackAt (1).clipAt (2).isValid(), "paste is undone in one step");

    commands.selectSlot (ids[0], 0);
    commands.cutSelection();
    check (! m.trackAt (0).clipAt (0).isValid() && commands.canPaste(), "cut removes the clip and keeps it for paste");
    m.undo.undo();

    commands.selectTrack (ids[0]);
    commands.newTrack();
    check (m.numTracks() == 3 && m.trackAt (1).numDevices() == 1 && m.selectedTrack() == m.trackAt (1).id(), "a new track goes after the selected one and gets selected");
    commands.deleteSelection();
    check (m.numTracks() == 2 && m.selectedTrack() == m.trackAt (1).id(), "after deleting a track its neighbour is selected");

    selection.selectScene (1);
    commands.newScene();
    check (m.numScenes() == 4 && selection.get().kind == ui::Selection::Kind::scene && selection.get().scene == 2, "Cmd+I inserts a scene after the selected one");
    commands.duplicateSelection();
    check (m.numScenes() == 5 && selection.get().scene == 3, "Cmd+D on a scene duplicates it");
    commands.deleteSelection();
    check (m.numScenes() == 4, "Delete on a scene removes it");

    commands.selectTrack (ids[1]);
    commands.newClip();
    check (m.findTrack (ids[1]).clipAt (0).isValid() && selection.get().kind == ui::Selection::Kind::clip, "Cmd+Shift+M creates a clip in the first free slot of the track");

    // Capture scene reads what plays: nothing plays here, so the new scene is empty.
    selection.selectScene (0);
    commands.captureScene();
    check (m.numScenes() == 5 && ! m.trackAt (0).clipAt (1).isValid(), "capture without playing clips gives an empty scene");

    // Deleting the selected device
    auto device = m.addDevice (m.trackAt (0), "prisma", "Prisma");
    m.undo.clearUndoHistory();
    selection.selectDevice (m.trackAt (0).id(), device.id());
    check (commands.canDelete(), "a device can be deleted");
    commands.deleteSelection();
    check (m.trackAt (0).numDevices() == 0, "Delete on a device removes it");
    m.undo.undo();
    check (m.trackAt (0).numDevices() == 1, "and undo brings it back");
}

//==============================================================================
void testControlsAndHelp()
{
    std::printf ("[test ui-4] number parsing, in-place editing, info bar help lookup, window settings\n");
    double v = 0.0;
    check (ui::parseNumber ("-6,5 dB", v) && std::abs (v + 6.5) < 1e-9, "comma decimals and unit suffix");
    check (ui::parseNumber (juce::String::fromUTF8 ("\xe2\x88\x92" "12.25"), v) && std::abs (v + 12.25) < 1e-9, "typographic minus");
    check (ui::parseNumber (" 124 BPM", v) && std::abs (v - 124.0) < 1e-9, "tempo with unit");
    check (ui::parseNumber ("+3", v) && std::abs (v - 3.0) < 1e-9, "plus sign");
    check (! ui::parseNumber ("abc", v) && ! ui::parseNumber ("", v) && ! ui::parseNumber ("-", v), "no number, no value");

    // Double-click opens an editor: Enter confirms, Esc cancels.
    {
        ui::NumberField field;
        field.setBounds (0, 0, 80, 28);
        field.setRange (-60.0, 6.0, 0.1);
        field.setValue (-3.0);
        int starts = 0, ends = 0;
        double last = 99.0;
        field.onGestureStart = [&] { ++starts; };
        field.onGestureEnd = [&] { ++ends; };
        field.onValueChange = [&] (double x) { last = x; };

        field.beginEditing();
        check (field.isEditing(), "double-click opens the editor");
        auto* editor = dynamic_cast<ui::InlineEditor*> (field.getChildComponent (0));
        check (editor != nullptr, "the editor is a child of the field");
        if (editor != nullptr)
        {
            editor->setText ("-12,5", false);
            editor->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
        check (! field.isEditing() && std::abs (field.getValue() + 12.5) < 1e-9 && std::abs (last + 12.5) < 1e-9, "Enter confirms the typed value");
        check (starts == 1 && ends == 1, "typing is one gesture (one undo transaction)");

        field.beginEditing();
        editor = dynamic_cast<ui::InlineEditor*> (field.getChildComponent (0));
        if (editor != nullptr)
        {
            editor->setText ("5", false);
            editor->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
        check (! field.isEditing() && std::abs (field.getValue() + 12.5) < 1e-9 && starts == 1, "Esc cancels and changes nothing");

        field.beginEditing();
        editor = dynamic_cast<ui::InlineEditor*> (field.getChildComponent (0));
        if (editor != nullptr)
        {
            editor->setText ("500", false);
            editor->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
        check (std::abs (field.getValue() - 6.0) < 1e-9, "typed values are limited to the range");
    }

    // Help text lookup: the nearest parent with a text, or a provider asked for the position.
    {
        struct Provider final : juce::Component, ui::HelpProvider
        {
            juce::String helpAt (juce::Point<int> p) const override { return p.x < 50 ? "left\nLeft part" : juce::String(); }
        };
        Provider parent;
        parent.setBounds (0, 0, 100, 100);
        parent.setHelpText ("whole\nWhole panel");
        juce::Component child;
        child.setBounds (0, 0, 100, 100);
        parent.addAndMakeVisible (child);
        juce::Component labelled;
        labelled.setBounds (60, 60, 20, 20);
        labelled.setHelpText ("button\nA button");
        parent.addAndMakeVisible (labelled);

        check (ui::InfoBar::helpFor (&child, { 10, 10 }) == "left\nLeft part", "a provider answers for the position under the mouse");
        check (ui::InfoBar::helpFor (&child, { 90, 10 }) == "whole\nWhole panel", "otherwise the panel's own text is used");
        check (ui::InfoBar::helpFor (&labelled, { 5, 5 }) == "button\nA button", "a component's own text wins over its parent's");
        check (ui::helpTitle ("a\nb") == "a" && ui::helpBody ("a\nb") == "b" && ui::helpBody ("a").isEmpty(), "title and description split");
    }

    // The texts of the info bar come from Strings.h: every help text has a title and a description.
    check (ui::helpBody (Strings::help::tempo).isNotEmpty() && ui::helpBody (Strings::help::clipSlot).isNotEmpty(), "help texts have a description");

    // Window layout settings
    {
        juce::TemporaryFile file (".settings");
        {
            Settings s (file.getFile());
            check (s.browserWidth() == 236 && s.detailHeight() == 232 && s.browserVisible() && s.detailVisible() && s.mixerVisible(), "layout defaults are those of the concept");
            s.setBrowserWidth (1000);
            s.setDetailHeight (10);
            s.setBrowserVisible (false);
            s.setMixerVisible (false);
            s.setWindowState ("w=1 h=2");
            s.save();
        }
        Settings s (file.getFile());
        check (s.browserWidth() == 420 && s.detailHeight() == 160, "panel sizes are limited to 180-420 and 160-480");
        check (! s.browserVisible() && s.detailVisible() && ! s.mixerVisible() && s.windowState() == "w=1 h=2", "panel visibility and window state are stored");
    }
}
} // namespace

void tests::runUiTests()
{
    testThemes();
    testActionTransactions();
    testActionsWithEngine();
    testEditCommands();
    testControlsAndHelp();
}
