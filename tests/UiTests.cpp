// Tests of the window layer that need no window: the themes (test 12 of docs/spec/01-layout-project.md), the
// model side of every interface action (one undo transaction each), the edit commands, number parsing and
// the info bar's help lookup.
#include "TestSupport.h"

#include <BinaryData.h>

#include "../app/Source/Strings.h"
#include "../app/Source/model/ProjectActions.h"
#include "../app/Source/ui/Chrome.h"
#include "../app/Source/ui/ComputerKeys.h"
#include "../app/Source/ui/Controls.h"
#include "../app/Source/ui/TransportMenus.h"
#include "../app/Source/model/ClipEdit.h"
#include "../app/Source/ui/EditCommands.h"
#include "../app/Source/ui/Theme.h"
#include "../app/Source/ui/Kit.h"

using namespace tests;

namespace
{
//==============================================================================
void testThemes()
{
    std::printf ("[test 12] themes: the two glass JSON files, all tokens, 16-colour palette\n");
    auto& manager = ThemeManager::get();
    const auto problems = manager.reload();
    check (problems.isEmpty(), "all built-in theme files parse");
    check (manager.all ().size () == 2 && manager.all ()[0].id == "glass-light" && manager.all ()[1].id == "glass-dark", "two built-in themes, light and dark glass");

    for (const auto& t : manager.all())
    {
        const juce::String name = t.id;
        std::set<juce::uint32> distinct;
        for (int i = 0; i < Theme::kPaletteSize; ++i)
            distinct.insert (t.palette (i).getARGB());
        check ((int) distinct.size() == Theme::kPaletteSize, name + ": palette has 16 distinct colours");
        check (t.palette (0) == juce::Colour::fromHSL (t.hues[0] / 360.0f, t.clipSaturation, t.clipLightness, 1.0f), name + ": palette colour 0 is HSL of hue 0");
        check (t.glass, name + ": the built-in themes are glass");
    }

    for (const auto& th : manager.all())
        for (int i = 0; i < Theme::kPaletteSize; ++i)
            check (Theme::contrast (th.textPalette (i), th.panel) >= 4.5f && Theme::contrast (th.textPalette (i), th.chrome) >= 4.5f,
                   th.id + ": palette colour " + juce::String (i) + " as text has contrast 4.5:1");


    // A theme without a token is refused with the name of the token.
    Theme broken;
    juce::String error;
    check (! Theme::fromJson ("{\"id\":\"x\",\"name\":\"X\"}", broken, error) && error.contains ("background"), "missing token is reported by name");
    check (! Theme::fromJson ("not json", broken, error), "garbage is not a theme");
    auto json = juce::String::fromUTF8 (BinaryData::glassdark_json, BinaryData::glassdark_jsonSize);
    auto invalid = juce::JSON::parse (json);
    invalid.getDynamicObject()->setProperty ("accent", "zzz");
    check (! Theme::fromJson (juce::JSON::toString (invalid), broken, error) && error.contains ("accent"), "bad colour is reported");

    // Older user themes have no ambient tokens; each optional token is validated independently.
    auto legacy = juce::JSON::parse (json);
    legacy.getDynamicObject()->removeProperty ("glassTint");
    legacy.getDynamicObject()->removeProperty ("glassTint2");
    check (Theme::fromJson (juce::JSON::toString (legacy), broken, error)
               && broken.glassTint == broken.accentFill && broken.glassTint2 == broken.accentFill2,
           "themes without ambient tokens retain their accent-derived ground");
    for (const auto* key : { "glassTint", "glassTint2" })
    {
        auto badTint = juce::JSON::parse (json);
        badTint.getDynamicObject()->setProperty (key, "#wrong");
        check (! Theme::fromJson (juce::JSON::toString (badTint), broken, error) && error.contains (key),
               juce::String (key) + ": malformed optional material colour is reported");
    }

    // User themes from a folder: valid ones join the list, a broken one is reported and skipped.
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("daw-theme-test-" + juce::String (juce::Random::getSystemRandom().nextInt (1000000)));
    dir.createDirectory();
    dir.getChildFile ("mine.json").replaceWithText (json.replace ("\"id\": \"glass-dark\"", "\"id\": \"mine\"").replace ("\"name\": \"Glass · Dark\"", "\"name\": \"Mine\""));
    dir.getChildFile ("broken.json").replaceWithText ("{ \"id\": \"broken\" }");
    const auto userProblems = manager.reload (dir);
    check (manager.all ().size () == 3 && manager.all ().back ().id == "mine",
           "a theme from User Library/Themes appears in the list");
    check (userProblems.size() == 1 && userProblems[0].startsWith ("broken.json"), "a broken user theme is reported, the others load");
    manager.setCurrent ("mine");
    check (theme().id == "mine", "current theme can be switched");
    manager.setCurrent ("no-such-theme");
    check (theme().id == "glass-light", "unknown theme id falls back to the first theme");
    dir.deleteRecursively();
    manager.reload();

    // Fonts come from the embedded files, with different weights.
    manager.setCurrent ("glass-light");
    const auto regular = theme().uiFont (13.0f, 400), semibold = theme().uiFont (13.0f, 600), mono = theme().numFont (13.0f);
    check (regular.getTypefacePtr() != nullptr && regular.getTypefacePtr()->getName().startsWith ("Onest"), "UI font is Onest");
    check (semibold.getTypefacePtr() != nullptr && semibold.getTypefacePtr() != regular.getTypefacePtr(), "semibold is a different face than regular");
    check (mono.getTypefacePtr() != nullptr && mono.getTypefacePtr()->getName().startsWith ("JetBrains Mono"), "number font is JetBrains Mono");
    check (std::abs (juce::GlyphArrangement::getStringWidth (mono, "1111") - juce::GlyphArrangement::getStringWidth (mono, "8888")) < 0.01f, "digits of the number font have equal widths");
    manager.setCurrent ("glass-dark");
}

void
testAppearance ()
{
    std::printf ("[test glass] appearance persistence, validation, contrast and logo resources\n");
    Appearance a;
    a.customAccent = true;
    a.primary = juce::Colours::yellow;
    a.secondary = juce::Colours::magenta;
    a.angle = 137;
    a.glow = 0.72f;
    a.glass = 0.45f;
    Appearance loaded;
    check (Appearance::fromJson (a.toJson (), loaded) && loaded.primary == a.primary &&
             loaded.secondary == a.secondary && loaded.customAccent && std::abs (loaded.angle - 137) < 0.001f,
           "appearance round-trip preserves chosen colours and gradient");
    check (!Appearance::fromJson ("{\"primary\":\"oops\"}", loaded), "invalid saved colour rejected");
    check (Appearance::fromJson ("{\"angle\":900,\"glow\":-2,\"glass\":10}", loaded) && loaded.angle <= 360 &&
             loaded.glow >= 0 && loaded.glass <= 1,
           "saved material parameters bounded");
    auto& manager = ThemeManager::get ();
    for (const auto* id : { "glass-light", "glass-dark" })
    {
        manager.setCurrent (id);
        const auto record = theme ().record, play = theme ().play;
        for (const auto fill :
             { juce::Colours::black, juce::Colours::white, juce::Colours::yellow, juce::Colours::purple })
            check (Theme::contrast (theme ().inkOn (fill), fill) >= 4.5f,
                   "glass labels stay readable with extreme custom colours");
        manager.setAppearance (a);
        check (theme ().glass && theme ().accentFill == a.primary, "raw accent preserved for glass fill");
        check (Theme::contrast (theme ().accent, theme ().panel) >= 4.5f &&
                 Theme::contrast (theme ().accent, theme ().chrome) >= 4.5f,
               "accent text contrast protected on both surfaces");
        check (theme ().record == record && theme ().play == play, "custom accent does not recolour record or play");
        a.reduceTransparency = true;
        manager.setAppearance (a);
        check (theme ().material <= 0 && theme ().glow <= 0, "reduced transparency disables lens and glow");
        a.reduceTransparency = false;
        manager.setAppearance (a);
        manager.reload ();
        check (manager.appearance ().primary == a.primary && theme ().accentFill == a.primary,
               "custom colours survive theme reload");
    }
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                  .getNonexistentChildFile ("prisma-appearance", ".settings");
    {
        Settings settings (file);
        settings.setAppearance (a.toJson ());
        settings.setTheme ("glass-light");
        settings.save ();
    }
    {
        Settings settings (file);
        Appearance restored;
        check (Appearance::fromJson (settings.appearance (), restored) && restored.primary == a.primary &&
                 settings.theme () == "glass-light",
               "appearance survives settings file reopen");
    }
    file.deleteFile ();
    for (const auto* id : { "glass-light", "glass-dark" })
    {
        manager.setCurrent (id);
        juce::Image mark (juce::Image::ARGB, 128, 128, true);
        juce::Graphics graphics (mark);
        ui::drawLogo (graphics, { 0.0f, 0.0f, 128.0f, 128.0f });
        int pixels = 0;
        for (int y = 0; y < mark.getHeight(); ++y)
            for (int x = 0; x < mark.getWidth(); ++x)
                pixels += mark.getPixelAt (x, y).getAlpha() > 0 ? 1 : 0;
        check (pixels > 100, "current native prism mark renders in both glass themes");
    }
    manager.setAppearance (Appearance ());
    manager.setCurrent ("glass-dark");
}

void testKit()
{
    std::printf ("[test kit] every control kind in every state paints in both themes; labels stay readable\n");
    auto& manager = ThemeManager::get();
    manager.reload();
    using ui::kit::Kind;
    const Kind kinds[] { Kind::plain, Kind::ghost, Kind::toggle, Kind::primary, Kind::danger, Kind::play };
    for (const auto& th : manager.all())
    {
        manager.setCurrent (th.id);
        const auto& t = theme();
        check (t.metrics.controlHeight >= 16.0f && t.metrics.compactHeight <= t.metrics.controlHeight
                   && t.metrics.text > t.metrics.small && t.metrics.panelRadius >= 0.0f,
               th.id + ": size tokens are sane");
        juce::Image image (juce::Image::ARGB, 120, 40, true);
        juce::Graphics g (image);
        bool readable = true, painted = true;
        for (const auto kind : kinds)
            for (int bits = 0; bits < 32; ++bits)
            {
                ui::kit::State st { (bits & 1) != 0, (bits & 2) != 0, (bits & 4) != 0, (bits & 8) != 0, (bits & 16) == 0 };
                image.clear (image.getBounds(), t.panel);
                const auto ink = ui::kit::button (g, { 4.0f, 4.0f, 112.0f, 32.0f }, kind, st);
                const auto fill = image.getPixelAt (60, 20);
                painted = painted && image.getPixelAt (60, 20).getAlpha() > 0;
                if (st.enabled && kind != Kind::play && fill.getAlpha() == 255)
                    readable = readable && Theme::contrast (ink, fill) >= 3.0f;
            }
        check (painted, th.id + ": buttons paint in every state");
        check (readable, th.id + ": button labels contrast with their fill (3:1 or more) in every enabled state");
        image.clear (image.getBounds());
        ui::kit::switchControl (g, { 0.0f, 0.0f, 40.0f, 24.0f }, { false, false, true });
        ui::kit::segmented (g, { 0.0f, 0.0f, 120.0f, 30.0f }, { "A", "B", "C" }, 1, 0, 2);
        ui::kit::field (g, { 0.0f, 0.0f, 120.0f, 26.0f }, { false, false, false, true });
        ui::kit::card (g, { 4.0f, 4.0f, 100.0f, 30.0f }, true);
        ui::kit::row (g, { 0.0f, 0.0f, 120.0f, 24.0f }, { true, false, true });
        check (ui::kit::segmentAt ({ 0.0f, 0.0f, 120.0f, 30.0f }, 3, 100.0f) == 2 && ui::kit::segmentAt ({ 0.0f, 0.0f, 120.0f, 30.0f }, 3, -5.0f) == 0,
               th.id + ": a click finds its segment");
    }
    manager.setCurrent ("glass-light");
    check (! theme().metrics.pill && std::abs (theme().metrics.controlRadius - 10.0f) < 0.01f && std::abs (theme().metrics.controlHeight - 28.0f) < 0.01f,
           "glass themes read their size tokens from the file");
    manager.setCurrent ("glass-dark");
}

void testGlassMaterial()
{
    std::printf ("[test glass material] continuous ground, crisp selections, contrast and opaque fallback\n");
    auto& manager = ThemeManager::get();
    manager.setAppearance (Appearance());
    for (const auto* id : { "glass-light", "glass-dark" })
    {
        manager.setCurrent (id);
        juce::Component window, child;
        window.setSize (480, 320);
        window.addAndMakeVisible (child);
        child.setBounds (90, 80, 160, 100);
        const auto ground = [] (juce::Component& c) {
            juce::Image image (juce::Image::ARGB, c.getWidth(), c.getHeight(), true);
            juce::Graphics g (image);
            ui::glass::windowBackdrop (g, c);
            return image;
        };
        const auto whole = ground (window), part = ground (child);
        bool readable = true;
        int maximumDifference = 0;
        for (int y = 10; y < child.getHeight(); y += 20)
            for (int x = 10; x < child.getWidth(); x += 20)
            {
                const auto pixel = part.getPixelAt (x, y);
                const auto parentPixel = whole.getPixelAt (x + child.getX(), y + child.getY());
                const auto difference = [] (juce::Colour a, juce::Colour b) {
                    return std::max ({ std::abs ((int) a.getRed() - b.getRed()),
                                       std::abs ((int) a.getGreen() - b.getGreen()),
                                       std::abs ((int) a.getBlue() - b.getBlue()) });
                };
                maximumDifference = std::max (maximumDifference, difference (pixel, parentPixel));
                const auto plate = pixel.overlaidWith (ui::glass::plateColour());
                readable = readable && Theme::contrast (theme().text, plate) >= 4.5f
                                    && Theme::contrast (theme().dimText, plate) >= 4.5f;
            }
        // Each of the three alpha-composited gradients can round by one byte at a different raster clip origin.
        check (maximumDifference <= 3, juce::String (id) + ": child plates use the same window-space ambient light (maximum channel difference "
                                          + juce::String (maximumDifference) + ")");
        check (readable, juce::String (id) + ": main and secondary text stay readable on ambient plates");
        if (! theme().isLight())
        {
            bool graphite = true;
            for (const auto point : { juce::Point<int> (30, 30), { 450, 30 }, { 240, 290 } })
            {
                const auto pixel = whole.getPixelAt (point.x, point.y);
                graphite = graphite && std::max ({ pixel.getRed(), pixel.getGreen(), pixel.getBlue() })
                                         - std::min ({ pixel.getRed(), pixel.getGreen(), pixel.getBlue() }) <= 12;
            }
            check (graphite, "dark ambient surfaces remain neutral graphite rather than navy");
        }

        const auto paint = [] (juce::Colour under, const auto& painter) {
            juce::Image image (juce::Image::ARGB, 120, 40, true);
            juce::Graphics g (image);
            g.fillAll (under);
            painter (g, juce::Rectangle<float> (4.0f, 4.0f, 112.0f, 32.0f));
            return image;
        };
        const auto card = [] (juce::Graphics& g, juce::Rectangle<float> r) { ui::kit::card (g, r); };
        const auto a = paint (theme().background, card), b = paint (theme().panel2, card);
        check (a.getPixelAt (60, 20) != b.getPixelAt (60, 20),
               juce::String (id) + ": satin cards preserve a little of the underlying material");
        check (a.getPixelAt (0, 20) == theme().background,
               juce::String (id) + ": cards do not cast clipped shadows outside their bounds");

        const auto selection = [] (juce::Graphics& g, juce::Rectangle<float> r) {
            ui::kit::row (g, r, { false, false, true });
        };
        const auto row = paint (theme().chrome, selection);
        const auto selected = row.getPixelAt (60, 20);
        check (selected != theme().chrome && selected == row.getPixelAt (60, 12),
               juce::String (id) + ": selection is a visible flat tint without a convex gloss band");
        check (Theme::contrast (theme().text, selected) >= 4.5f,
               juce::String (id) + ": selected row labels keep text contrast");

        Appearance custom;
        custom.customAccent = true;
        custom.primary = juce::Colours::yellow;
        custom.secondary = juce::Colours::magenta;
        manager.setAppearance (custom);
        check (ground (window).getPixelAt (30, 30) == whole.getPixelAt (30, 30),
               juce::String (id) + ": custom control colours do not flood the ambient ground");
        custom.reduceTransparency = true;
        manager.setAppearance (custom);
        const auto opaqueA = paint (theme().background, card), opaqueB = paint (theme().panel2, card);
        check (opaqueA.getPixelAt (60, 20) == opaqueB.getPixelAt (60, 20)
                   && ui::glass::plateColour().isOpaque(),
               juce::String (id) + ": reduced transparency makes cards and plates opaque");
        const auto field = paint (theme().background, [] (juce::Graphics& g, juce::Rectangle<float> r) {
            ui::kit::field (g, r, {});
        });
        check (field.getPixelAt (60, 20) == theme().panel
                   && ground (window).getPixelAt (30, 30) == theme().background,
               juce::String (id) + ": reduced transparency removes the field film and ambient glows");
        manager.setAppearance (Appearance());

        for (const bool vertical : { false, true })
        {
            ui::Splitter splitter (vertical);
            splitter.setSize (64, 40);
            const auto painted = [&] {
                juce::Image image (juce::Image::ARGB, 64, 40, true);
                juce::Graphics g (image);
                splitter.paint (g);
                return image.getPixelAt (32, 20).getAlpha() != 0;
            };
            const juce::MouseEvent event (juce::Desktop::getInstance().getMainMouseSource(), {}, {},
                                          0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &splitter, &splitter,
                                          juce::Time (1), {}, juce::Time (1), 1, false);
            check (! painted(), "splitter guide is hidden at rest");
            splitter.mouseEnter (event);
            check (painted(), "splitter guide appears on hover");
            splitter.mouseDown (event);
            splitter.mouseExit (event);
            check (painted(), "splitter guide stays visible during a drag outside its hit area");
            splitter.mouseUp (event);
            check (! painted(), "splitter guide hides again when the drag ends");
        }
    }
    manager.setCurrent ("glass-dark");
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

namespace
{
void testComputerKeys()
{
    std::printf ("[test ui-5] computer keys: octave and velocity are announced\n");
    juce::MidiKeyboardState state;
    ui::ComputerKeys keys (state);
    juce::StringArray messages;
    int stateChanges = 0;
    keys.onMessage = [&] (const juce::String& m) { messages.add (m); };
    keys.onStateChanged = [&] { ++stateChanges; };
    const auto press = [&] (juce::juce_wchar c, int mods = 0) { return keys.keyPressed (juce::KeyPress (c, juce::ModifierKeys (mods), c)); };
    namespace K = Strings::keyboardUi;

    check (keys.octaveText() == "C3", "the A key starts on C3");

    // switched off: Z and X explain themselves, other keys stay silent and unhandled
    check (! press ('z') && messages.size() == 1 && messages[0] == K::keysOffHint, "off: Z shows the hint and is not taken");
    check (! press ('X') && messages.size() == 2 && messages[1] == K::keysOffHint, "off: X shows the hint");
    check (! press ('a') && ! press ('c') && ! press ('v') && ! press ('q') && messages.size() == 2, "off: other keys do nothing");
    check (! press ('z', juce::ModifierKeys::commandModifier) && ! press ('z', juce::ModifierKeys::shiftModifier) && messages.size() == 2,
           "off: Cmd+Z and Shift+Z are not ours");
    check (keys.octaveText() == "C3", "off: the octave did not move");

    keys.setOn (true);
    check (stateChanges == 1 && keys.isOn(), "switching on notifies the lamp");
    messages.clear();
    check (press ('x') && messages.size() == 1 && messages[0] == K::octaveMessage (72), "X: octave up is announced");
    check (messages[0] == Strings::tr ("Octave: C4\xe2\x80\x93" "E5", "\xd0\x9e\xd0\xba\xd1\x82\xd0\xb0\xd0\xb2\xd0\xb0: C4\xe2\x80\x93" "E5"), "the message reads Octave: C4–E5");
    check (keys.octaveText() == "C4" && keys.panel().getBaseNote() == 72, "the lamp text follows the octave");
    check (press ('z') && press ('z') && keys.octaveText() == "C2" && messages[messages.size() - 1] == K::octaveMessage (48), "Z twice goes two octaves down");
    check (press ('c') && keys.panel().getVelocity() == 80 && messages[messages.size() - 1] == K::velocityMessage (80), "C: velocity down is announced");
    check (press ('v') && press ('v') && messages[messages.size() - 1] == K::velocityMessage (120), "V: velocity up is announced");
    check (stateChanges >= 7, "every change refreshes the lamp");

    for (int i = 0; i < 12; ++i)
        press ('z');
    check (keys.panel().getBaseNote() == 12 && messages[messages.size() - 1] == K::octaveMessage (12), "at the lowest octave the message still appears");

    // scale mode: the A row plays degrees of the project scale, the black row is silent
    keys.projectScale = [] { return ui::ComputerKeys::ScaleInfo { 2, "minor" }; }; // D minor
    keys.setOn (true);
    while (keys.panel().getBaseNote() < 60)
        press ('x');
    auto& panel = keys.panel();
    check (panel.noteForKey (0, 60) == 62 && panel.noteForKey (2, 60) == 64 && panel.noteForKey (4, 60) == 65 && panel.noteForKey (5, 60) == 67,
           "D minor: A S D F play D E F G");
    check (panel.noteForKey (9, 60) == 70 && panel.noteForKey (16, 60) == 77, "H is the sixth degree (Bb), the ; key the tenth (F)");
    check (panel.noteForKey (1, 60) == prisma_ui::KeyboardPanel::kNoNote, "the black-key row is silent in scale mode");
    keys.setInScale (false);
    check (panel.noteForKey (1, 60) == prisma_ui::KeyboardPanel::kDefaultNote, "scale mode off: chromatic again");
    keys.setInScale (true);
    keys.projectScale = [] { return ui::ComputerKeys::ScaleInfo { 0, "" }; };
    check (panel.noteForKey (1, 60) == prisma_ui::KeyboardPanel::kDefaultNote, "no project scale: chromatic");

    // Russian layout: the keys type Cyrillic letters, but their key codes are still the Latin ones
    const auto pressRu = [&] (juce::juce_wchar code, juce::juce_wchar typed) { return keys.keyPressed (juce::KeyPress (code, {}, typed)); };
    const int before = keys.panel().getBaseNote();
    check (pressRu ('X', 0x447 /* ч */) && keys.panel().getBaseNote() == before + 12, "Russian layout: X (types ч) moves the octave up");
    check (pressRu ('Z', 0x44f /* я */) && keys.panel().getBaseNote() == before, "Russian layout: Z (types я) moves it down");
    const int velocity = keys.panel().getVelocity();
    check (pressRu ('C', 0x441 /* с */) && keys.panel().getVelocity() == velocity - 20, "Russian layout: C (types с) lowers the velocity");
    check (pressRu ('A', 0x444 /* ф */), "Russian layout: a note key is swallowed, not beeped at");

    // A Prisma window's Oct buttons move the DAW keyboard, and the window shows the DAW's octave
    prisma_ui::KeyboardPanel windowPanel (state);
    windowPanel.onOctaveButton = [&] (int delta) { keys.panel().shiftOctave (delta); };
    windowPanel.showState (keys.panel().getBaseNote(), keys.panel().getVelocity());
    check (windowPanel.getBaseNote() == keys.panel().getBaseNote(), "the window panel shows the DAW octave");
    windowPanel.onOctaveButton (1);
    check (keys.panel().getBaseNote() == before + 12, "Oct + in the window moves the DAW octave");

    keys.setOn (false);
    messages.clear();
    check (! press ('x') && messages.size() == 1 && messages[0] == K::keysOffHint, "off again: the hint returns");
    check (! pressRu ('Z', 0x44f) && messages.size() == 2, "off: the hint also appears with the Russian layout");
}

//==============================================================================
// Finds an item of a popup menu by the texts of its path ("Count-in", "2 bars"); null if there is none.
const juce::PopupMenu::Item* findMenuItem (const juce::PopupMenu& menu, std::initializer_list<juce::String> path, juce::PopupMenu& holder)
{
    holder = menu;
    const juce::PopupMenu::Item* found = nullptr;
    size_t depth = 0;
    for (const auto& name : path)
    {
        found = nullptr;
        juce::PopupMenu::MenuItemIterator it (holder, false);
        while (it.next())
            if (it.getItem().text == name)
            {
                found = &it.getItem();
                if (++depth < path.size())
                    holder = it.getItem().subMenu != nullptr ? *it.getItem().subMenu : juce::PopupMenu();
                break;
            }
        if (found == nullptr)
            return nullptr;
    }
    return found;
}

void testRecordingUi()
{
    std::printf ("[test ui-rec] count-in text, transport menus, record source, controller lane\n");
    namespace R = Strings::recording;
    const auto minus = juce::String::fromUTF8 ("\xe2\x88\x92");

    // The position field: bar. beat. sixteenth, and "−bar. beat" during a count-in (one bar of 4/4 starts at -4)
    check (ui::formatPosition (0.0, 4.0) == "1. 1. 1", "position 0 is 1. 1. 1");
    check (ui::formatPosition (5.5, 4.0) == "2. 2. 3", "position 5.5 is 2. 2. 3");
    check (ui::formatPosition (-4.0, 4.0) == minus + "1. 1", "count-in start: -1. 1 with a real minus sign");
    check (ui::formatPosition (-2.0, 4.0) == minus + "1. 3", "two beats into the count-in: -1. 3");
    check (ui::formatPosition (-0.25, 4.0) == minus + "1. 4", "the last sixteenth of the count-in still reads -1. 4");
    check (ui::formatPosition (-5.0, 4.0) == minus + "2. 4", "two bars of count-in: the first bar is -2");
    check (ui::formatPosition (-3.0, 3.0) == minus + "1. 1", "3/4 count-in: -1. 1 at -3 beats");
    check (ui::formatPosition (-1.0, 3.0) == minus + "1. 3", "3/4 count-in: -1. 3 at -1 beat");
    check (ui::formatPosition (-4.0, 4.0).startsWithChar (0x2212), "the minus is U+2212");

    // The metronome menu writes the project's preferences
    {
        ProjectModel model = ProjectModel::createEmpty();
        juce::PopupMenu holder;
        const auto menu = ui::makeMetronomeMenu (model);
        const auto call = [&] (std::initializer_list<juce::String> path) {
            juce::PopupMenu h;
            const auto* item = findMenuItem (menu, path, h);
            check (item != nullptr && item->action != nullptr, "menu item exists: " + *(path.end() - 1));
            if (item != nullptr && item->action != nullptr)
                item->action();
        };
        check (std::abs (model.metronomeVolumeDb() - (-6.0)) < 1e-9, "default click volume is -6 dB");
        {
            juce::PopupMenu h;
            const auto* item = findMenuItem (menu, { R::menuVolume, R::volumeDb (-6) }, h);
            check (item != nullptr && item->isTicked, "volume submenu ticks the current value (-6 dB)");
        }
        call ({ R::menuVolume, R::volumeDb (-18) });
        check (std::abs (model.metronomeVolumeDb() - (-18.0)) < 1e-9, "volume -18 dB");
        call ({ R::menuVolume, R::volumeDb (-60) });
        check (model.metronomeVolumeDb() <= ProjectModel::kMinMetronomeDb, "volume -inf");
        call ({ R::menuVolume, R::volumeDb (0) });
        check (std::abs (model.metronomeVolumeDb() - (0.0)) < 1e-9, "volume 0 dB");
        call ({ R::menuOnlyRecording });
        check (model.metronomeOnlyRecording(), "only while recording is switched on");
        call ({ R::menuOnlyRecording });
        check (! model.metronomeOnlyRecording(), "and off again");
        for (const int bars : { 0, 2, 4, 1 })
        {
            call ({ R::menuCountIn, bars == 0 ? R::none : R::bars (bars) });
            check (model.countInBars() == bars, "count-in " + juce::String (bars) + " bars");
        }
        call ({ R::menuClickSound, R::soundWood });
        check (model.metronomeSound() == "wood", "wooden click");
        call ({ R::menuClickSound, R::soundClassic });
        check (model.metronomeSound() == "classic", "classic click");
        check (! model.undo.canUndo(), "transport preferences are not undo steps");
    }

    // The record menu: fixed length and record quantisation
    {
        ProjectModel model = ProjectModel::createEmpty();
        const auto menu = ui::makeRecordMenu (model);
        const auto call = [&] (std::initializer_list<juce::String> path) {
            juce::PopupMenu h;
            const auto* item = findMenuItem (menu, path, h);
            check (item != nullptr && item->action != nullptr, "menu item exists: " + *(path.end() - 1));
            if (item != nullptr && item->action != nullptr)
                item->action();
        };
        for (const int bars : { 1, 2, 4, 8, 16, 0 })
        {
            call ({ R::menuFixedLength, bars == 0 ? R::off : R::bars (bars) });
            check (model.recordFixedBars() == bars, "fixed length " + juce::String (bars));
        }
        const std::pair<const char*, double> grids[] { { "1/4", 1.0 }, { "1/8", 0.5 }, { "1/8T", 1.0 / 3.0 }, { "1/16", 0.25 }, { "1/16T", 1.0 / 6.0 }, { "1/32", 0.125 } };
        for (const auto& [name, beats] : grids)
        {
            call ({ R::menuRecordQuantise, name });
            check (std::abs (model.recordQuantise() - beats) < 1e-9, juce::String ("record quantise ") + name);
        }
        call ({ R::menuRecordQuantise, R::off });
        check (std::abs (model.recordQuantise()) < 1e-9, "record quantise off");
    }

    // The record source of a track: one transaction, audio is a value the model keeps
    {
        ActionFixture f;
        const auto id = f.track0.id();
        check (f.model.findTrack (id).recordSource() == "midi", "a new track records MIDI");
        f.oneTransaction ("record source", [&] { f.actions.setTrackRecordSource (id, "audio"); });
        check (f.model.findTrack (id).recordSource() == "audio", "the source is audio after the redo of the helper");
        f.actions.setTrackRecordSource (id, "audio");
        check (! f.model.undo.canUndo(), "choosing the current source is not an edit");
    }

    // The controller lane: a stroke redraws the events of its kind under it; the whole stroke is one transaction
    {
        ActionFixture f;
        const auto clip = f.model.findTrack (f.track0.id()).clipAt (0);
        const double length = clip.lengthBeats();
        using clipedit::ControllerLane;
        // an existing sustain event and a modwheel event after the stroke must survive
        std::vector<daw::engine::ControllerEvent> start { clipedit::laneEvent (ControllerLane::sustain, 0.5, 1.0),
                                                          clipedit::laneEvent (ControllerLane::modwheel, 3.5, 0.2) };
        clip.setControllers (start, &f.model.undo);
        f.model.undo.clearUndoHistory();
        const auto before = f.model.root.toXmlString();

        const auto original = clip.controllers();
        std::vector<std::pair<double, double>> stroke;
        f.model.undo.beginNewTransaction (Strings::recording::undoDrawController);
        for (int i = 0; i <= 8; ++i) // a ramp over two beats, applied step by step as the mouse moves
        {
            stroke.emplace_back (1.0 + i * 0.25, i / 8.0);
            clip.setControllers (clipedit::drawControllerStroke (original, ControllerLane::modwheel, stroke, length), &f.model.undo);
        }
        const auto drawn = clip.controllers();
        int mod = 0, sustain = 0;
        double lastTime = -1.0;
        bool sorted = true;
        for (const auto& e : drawn)
        {
            mod += clipedit::isLaneEvent (e, ControllerLane::modwheel) ? 1 : 0;
            sustain += clipedit::isLaneEvent (e, ControllerLane::sustain) ? 1 : 0;
            sorted = sorted && e.time >= lastTime;
            lastTime = e.time;
        }
        check (mod == 10 && sustain == 1, "the ramp is nine events, the later modwheel event and the sustain event survive");
        check (sorted, "controllers stay sorted by time");
        check (f.model.undo.canUndo(), "the stroke is in the history");
        f.model.undo.undo();
        check (f.model.root.toXmlString() == before, "one undo takes back the whole stroke");
        check (! f.model.undo.canUndo(), "and it was a single transaction");

        // replacement semantics: a flat stroke over existing events removes them and writes one
        const auto flat = clipedit::drawControllerStroke (original, ControllerLane::sustain, { { 0.0, 0.0 }, { 1.0, 0.0 }, { 2.0, 0.0 } }, length);
        int sus = 0;
        for (const auto& e : flat)
            sus += clipedit::isLaneEvent (e, ControllerLane::sustain) ? 1 : 0;
        check (sus == 1 && flat.size() == 2, "a flat sustain stroke replaces the old events with a single off event");
        const auto bend = clipedit::laneEvent (ControllerLane::pitchBend, 0.0, 0.51);
        check (((bend.data2 << 7) | bend.data1) == 8192, "pitch bend sticks to the centre");
        check (clipedit::laneEvent (ControllerLane::pitchBend, 0.0, 1.0).data2 == 127, "pitch bend top is 16383");
        check (! clipedit::isLaneEvent (clipedit::laneEvent (ControllerLane::sustain, 0.0, 1.0), ControllerLane::modwheel), "lanes do not mix");
    }
}
} // namespace

void tests::runUiTests()
{
    testComputerKeys();
    testRecordingUi();
    testThemes();
    testAppearance ();
    testKit();
    testGlassMaterial();
    testActionTransactions();
    testActionsWithEngine();
    testEditCommands();
    testControlsAndHelp();
}
