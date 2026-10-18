// "Mountain King": an electronic arrangement of Edvard Grieg's "In the Hall of the Mountain King" (1875, public domain),
// built only with what the DAW has: Prisma, the drum machine, the built-in effects, return tracks, sidechain, automation,
// Session scenes laid out on the Arrangement, and the offline export.
//
// It is an acceptance test of the whole app as much as a demo: every step below is one the user can take in the UI
// (add a track, pick a preset, turn a knob, draw notes, add an effect, draw an automation line, export), done through
// the same model calls the UI makes.
//
//     daw_demo_song <output folder>
//
// writes "<output folder>/Mountain King Project/Mountain King.prisma" (open it in the app) and
// "<output folder>/Mountain King.wav" (rendered by OfflineRenderer, as File > Export does).

#include "../../app/Source/Settings.h"
#include "../../app/Source/Strings.h"
#include "../../app/Source/engine/AudioEngine.h"
#include "../../app/Source/engine/BuiltInEffectProcessor.h"
#include "../../app/Source/engine/DrumKitProcessor.h"
#include "../../app/Source/engine/OfflineRenderer.h"
#include "../../app/Source/model/ArrangementEdit.h"
#include "../../app/Source/model/Automation.h"
#include "../../app/Source/project/ProjectFile.h"
#include "../../plugins/prisma/Source/PrismaProcessor.h"

#include "daw/synth/PrismaParams.h"

#include <cstdio>

namespace
{
using daw::engine::Note;
using Notes = std::vector<Note>;

constexpr double kTempo = 136.0;
constexpr double kBar = 4.0;

//==============================================================================
// Sounds

// A Prisma state: a factory preset with some knobs turned, as the user would do in the synth window.
juce::String prismaState (const juce::String& preset, const std::vector<std::pair<juce::String, float>>& knobs)
{
    PrismaProcessor prisma;
    const auto& presets = daw::synth::factoryPresets();
    for (size_t i = 0; i < presets.size(); ++i)
        if (preset == juce::String (presets[i].name))
            prisma.setCurrentProgram ((int) i);
    for (const auto& [id, value] : knobs)
        if (auto* p = prisma.apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
        else
            std::printf ("  ! Prisma has no parameter %s\n", id.toRawUTF8());
    juce::MemoryBlock state;
    prisma.getStateInformation (state);
    return state.toBase64Encoding();
}

// Index and normalised value of a Prisma parameter, for an automation lane.
int prismaParamIndex (const juce::String& id)
{
    const auto& d = daw::synth::parameterDescriptors();
    for (size_t i = 0; i < d.size(); ++i)
        if (id == juce::String (d[i].id))
            return (int) i;
    return -1;
}

float prismaNormalised (const juce::String& id, float value)
{
    PrismaProcessor prisma;
    auto* p = prisma.apvts.getParameter (id);
    return p != nullptr ? p->convertTo0to1 (value) : 0.0f;
}

DeviceRef addEffect (ProjectModel& model, const TrackRef& track, const juce::String& type, const std::vector<std::pair<juce::String, float>>& values)
{
    auto fx = model.addDevice (track, "fx", Strings::fx::effectName (type));
    fx.setPluginDesc (type, &model.undo);
    fx.setStateBase64 (BuiltInEffectProcessor::stateBase64 (type, values), &model.undo);
    return fx;
}

//==============================================================================
// Music. B minor, C3 = 60 (so B3 = 71 sits in the middle of the keyboard).

// The theme in eighth notes. Phrase A stays on the tonic, phrase B answers a fifth higher and ends on the dominant.
Notes theme (double at, int transpose, int velocity)
{
    struct E
    {
        double len;
        int pitch;
    };
    // Phrase A
    const std::vector<std::vector<E>> bars {
        { { .5, 71 }, { .5, 73 }, { .5, 74 }, { .5, 76 }, { .5, 78 }, { .5, 74 }, { 1, 78 } },
        { { .5, 77 }, { .5, 73 }, { 1, 77 }, { .5, 76 }, { .5, 72 }, { 1, 76 } },
        { { .5, 71 }, { .5, 73 }, { .5, 74 }, { .5, 76 }, { .5, 78 }, { .5, 74 }, { .5, 78 }, { .5, 83 } },
        { { .5, 81 }, { .5, 78 }, { .5, 74 }, { .5, 78 }, { 2, 81 } },
        // Phrase B: A a fifth up, closing on F# major
        { { .5, 78 }, { .5, 80 }, { .5, 81 }, { .5, 83 }, { .5, 85 }, { .5, 81 }, { 1, 85 } },
        { { .5, 84 }, { .5, 80 }, { 1, 84 }, { .5, 83 }, { .5, 79 }, { 1, 83 } },
        { { .5, 78 }, { .5, 80 }, { .5, 81 }, { .5, 83 }, { .5, 85 }, { .5, 81 }, { .5, 85 }, { .5, 90 } },
        { { .5, 85 }, { .5, 82 }, { .5, 78 }, { .5, 82 }, { 2, 85 } },
    };
    Notes out;
    double t = at;
    for (const auto& bar : bars)
        for (const auto& e : bar)
        {
            // Staccato on the eighths, legato on the long notes; accents on the beat.
            const bool onBeat = std::fmod (t - at, 1.0) < 0.01;
            out.push_back ({ t, e.len > 0.5 ? e.len * 0.95 : 0.42, e.pitch + transpose, juce::jlimit (1, 127, velocity + (onBeat ? 12 : 0)) });
            t += e.len;
        }
    return out;
}

// Chord of each half bar of the 8-bar theme (root, third, fifth from a bass root), for the pad and the bass.
struct Chord
{
    int root;        // bass note
    int third, fifth;
};
const Chord kChords[16] {
    { 47, 3, 7 }, { 47, 3, 7 },   // Bm
    { 49, 4, 7 }, { 48, 4, 7 },   // C# | C (the chromatic slide of the theme)
    { 47, 3, 7 }, { 47, 3, 7 },   // Bm
    { 50, 4, 7 }, { 50, 4, 7 },   // D
    { 42, 3, 7 }, { 42, 3, 7 },   // F#m
    { 44, 4, 7 }, { 43, 4, 7 },   // G# | G
    { 42, 3, 7 }, { 42, 3, 7 },   // F#m
    { 42, 4, 7 }, { 42, 4, 7 },   // F#
};

Notes pad (double at, int bars, int velocity)
{
    Notes out;
    for (int h = 0; h < bars * 2; ++h)
    {
        const auto& c = kChords[h % 16];
        const double t = at + h * 2.0;
        const int r = c.root + 12;
        for (int p : { r, r + c.third, r + c.fifth, r + 12 })
            out.push_back ({ t, 1.98, p, velocity });
    }
    return out;
}

// Off-beat eighths on the chord root (the kick sits on the beat).
Notes bass (double at, int bars, int velocity)
{
    Notes out;
    for (int h = 0; h < bars * 2; ++h)
        for (int k = 0; k < 2; ++k)
            out.push_back ({ at + h * 2.0 + k + 0.5, 0.45, kChords[h % 16].root, velocity });
    return out;
}

namespace Drum
{
constexpr int kick = 36, snare = 38, clap = 39, closedHat = 42, openHat = 46, crash = 49;
}

Notes kicks (double at, int bars)
{
    Notes out;
    for (int b = 0; b < bars * 4; ++b)
        out.push_back ({ at + b, 0.25, Drum::kick, 115 });
    return out;
}

struct DrumParts
{
    bool hats = true, clap = true, openHats = false, crashAtStart = false;
    int rollBars = 0; // snare roll over the last bars
};

Notes drums (double at, int bars, DrumParts parts)
{
    Notes out;
    for (int b = 0; b < bars; ++b)
    {
        const double bar = at + b * kBar;
        const bool roll = b >= bars - parts.rollBars;
        for (int s = 0; s < 16 && parts.hats && ! roll; ++s)
            if (s % 2 == 0 || s % 4 == 3)
                out.push_back ({ bar + s * 0.25, 0.1, Drum::closedHat, s % 4 == 2 ? 100 : 62 });
        for (int beat = 0; beat < 4; ++beat)
        {
            if (parts.clap && ! roll && beat % 2 == 1)
                out.push_back ({ bar + beat, 0.25, Drum::clap, 110 });
            if (parts.openHats && ! roll)
                out.push_back ({ bar + beat + 0.5, 0.25, Drum::openHat, 80 });
        }
        if (roll)
        {
            // Sixteenths getting louder towards the drop, the last bar in 32nds.
            const int index = b - (bars - parts.rollBars);
            const double step = index == parts.rollBars - 1 && parts.rollBars > 1 ? 0.125 : 0.25;
            for (double t = 0.0; t < kBar - 0.01; t += step)
            {
                const double progress = (index * kBar + t) / (parts.rollBars * kBar);
                out.push_back ({ bar + t, step * 0.8, Drum::snare, (int) (50 + 70 * progress) });
            }
        }
    }
    if (parts.crashAtStart)
        out.push_back ({ at, 2.0, Drum::crash, 110 });
    return out;
}

Notes concat (Notes a, const Notes& b)
{
    a.insert (a.end(), b.begin(), b.end());
    return a;
}

//==============================================================================
struct Section
{
    juce::String name;
    int startBar, bars;
};

// One clip per track per section: the scene of that section in the Session View, then laid on the timeline.
struct Part
{
    juce::String track;
    int section;
    Notes notes;
};

void build (ProjectModel& model)
{
    auto* um = &model.undo;
    model.setName ("Mountain King");
    model.setTempo (kTempo);
    model.setScale (11, "minor"); // B minor
    model.setAuthor ("Silver-Season");
    model.setProjectNotes ("After Edvard Grieg, \"In the Hall of the Mountain King\" (Peer Gynt, 1875). Public domain. "
                           "Built with the DAW's own instruments and effects only.");

    const std::vector<Section> sections {
        { "Intro", 0, 8 }, { "Build", 8, 8 }, { "Drop", 16, 16 }, { "Break", 32, 8 }, { "Drop 2", 40, 16 }, { "Outro", 56, 4 },
    };
    while (model.numScenes() < (int) sections.size())
        model.insertScene (-1);
    for (size_t i = 0; i < sections.size(); ++i)
        model.sceneAt ((int) i).setName (sections[i].name, um);

    // Returns first, so the sends of the tracks below have somewhere to go: A = reverb, B = tempo delay.
    auto reverbReturn = model.addTrack ("return");
    reverbReturn.setName ("A Hall", um);
    addEffect (model, reverbReturn, "reverb", { { "size", 85.0f }, { "decay", 4.5f }, { "predelay", 25.0f }, { "lowCut", 250.0f },
                                                 { "damping", 6500.0f }, { "mix", 100.0f } });
    auto delayReturn = model.addTrack ("return");
    delayReturn.setName ("B Echo", um);
    addEffect (model, delayReturn, "delay", { { "sync", 1.0f }, { "divL", 8.0f }, { "divR", 6.0f }, { "link", 0.0f }, { "feedback", 42.0f },
                                               { "pingPong", 1.0f }, { "hp", 400.0f }, { "lp", 5000.0f }, { "mix", 100.0f } });

    // Kick on its own track: the bass and the pad duck to it.
    auto kick = model.addTrack ("midi");
    kick.setName ("Kick", um);
    kick.setVolumeDb (-12.0f, um);
    model.addDevice (kick, "drums", Strings::drums::deviceName).setStateBase64 (DrumKitProcessor::factoryStateBase64 ("Techno"), um);

    auto drumTrack = model.addTrack ("midi");
    drumTrack.setName ("Drums", um);
    drumTrack.setVolumeDb (-6.0f, um);
    drumTrack.setSendDb (0, -18.0f, um);
    model.addDevice (drumTrack, "drums", Strings::drums::deviceName).setStateBase64 (DrumKitProcessor::factoryStateBase64 ("909 Club"), um);
    addEffect (model, drumTrack, "eq8", { { "b1on", 1.0f }, { "b1type", 2.0f }, { "b1freq", 180.0f } }); // no low end under the kick

    auto bassTrack = model.addTrack ("midi");
    bassTrack.setName ("Bass", um);
    bassTrack.setVolumeDb (0.0f, um);
    model.addDevice (bassTrack, "prisma", "Prisma")
        .setStateBase64 (prismaState ("Reese Bass", { { "flt_cutoff", 1400.0f }, { "flt_env", 0.2f }, { "env2_d", 0.18f }, { "env2_s", 0.2f },
                                                      { "glide", 0.0f } }),
                         um);
    auto bassComp = addEffect (model, bassTrack, "comp", { { "threshold", -24.0f }, { "ratio", 4.0f }, { "attack", 0.5f }, { "release", 120.0f } });
    bassComp.setSidechainTrack (kick.id(), um);

    auto pluck = model.addTrack ("midi");
    pluck.setName ("Pluck", um);
    pluck.setVolumeDb (0.0f, um);
    pluck.setSendDb (0, -14.0f, um);
    pluck.setSendDb (1, -10.0f, um);
    auto pluckSynth = model.addDevice (pluck, "prisma", "Prisma");
    pluckSynth.setStateBase64 (prismaState ("Pluck", { { "flt_cutoff", 900.0f }, { "env1_d", 0.35f }, { "env1_r", 0.25f } }), um);

    auto lead = model.addTrack ("midi");
    lead.setName ("Lead", um);
    lead.setVolumeDb (-4.0f, um);
    lead.setSendDb (0, -12.0f, um);
    lead.setSendDb (1, -16.0f, um);
    model.addDevice (lead, "prisma", "Prisma")
        .setStateBase64 (prismaState ("Supersaw Lead", { { "env1_r", 0.2f }, { "glide", 0.0f }, { "flt_cutoff", 7000.0f } }), um);
    addEffect (model, lead, "drive", { { "mode", 1.0f }, { "drive", 5.0f }, { "mix", 60.0f } });

    auto padTrack = model.addTrack ("midi");
    padTrack.setName ("Pad", um);
    padTrack.setVolumeDb (-1.0f, um);
    padTrack.setSendDb (0, -8.0f, um);
    model.addDevice (padTrack, "prisma", "Prisma").setStateBase64 (prismaState ("Glass Pad", { { "env1_a", 0.25f }, { "env1_r", 1.2f } }), um);
    auto padComp = addEffect (model, padTrack, "comp", { { "threshold", -24.0f }, { "ratio", 3.0f }, { "attack", 1.0f }, { "release", 160.0f } });
    padComp.setSidechainTrack (kick.id(), um);
    addEffect (model, padTrack, "chorus", { { "mix", 40.0f } });

    // A noise riser: Init with the oscillator off and white noise through a band-pass that the automation sweeps.
    auto riser = model.addTrack ("midi");
    riser.setName ("Riser", um);
    riser.setVolumeDb (-12.0f, um);
    riser.setSendDb (0, -10.0f, um);
    auto riserSynth = model.addDevice (riser, "prisma", "Prisma");
    riserSynth.setStateBase64 (prismaState ("Init", { { "oscA_on", 0.0f }, { "noise_level", 0.9f }, { "flt_type", 4.0f }, { "flt_res", 0.55f },
                                                      { "env1_a", 0.5f }, { "env1_r", 0.8f } }),
                               um);

    // Return tracks sit after the others, as the app adds them.
    model.tracksNode().moveChild (model.indexOfTrack (reverbReturn.id()), model.numTracks() - 1, um);
    model.tracksNode().moveChild (model.indexOfTrack (delayReturn.id()), model.numTracks() - 1, um);

    // Master: a gentle glue compressor and a limiter.
    model.addDevice (model.masterDevicesNode(), "fx", Strings::fx::effectName ("comp"))
        .setPluginDesc ("comp", um);
    {
        auto glue = DeviceRef (model.masterDevicesNode().getChild (0));
        glue.setStateBase64 (BuiltInEffectProcessor::stateBase64 ("comp", { { "threshold", -12.0f }, { "ratio", 2.0f }, { "attack", 10.0f },
                                                                          { "release", 200.0f }, { "knee", 12.0f } }),
                             um);
        auto limiter = model.addDevice (model.masterDevicesNode(), "fx", Strings::fx::effectName ("limiter"));
        limiter.setPluginDesc ("limiter", um);
        limiter.setStateBase64 (BuiltInEffectProcessor::stateBase64 ("limiter", { { "ceiling", -0.5f }, { "gain", 7.0f } }), um);
    }

    //==========================================================================
    // Notes, section by section (beats from the start of the section).
    const double b8 = 8 * kBar;
    std::vector<Part> parts {
        // Intro: the theme on the pluck (an octave down) over the pad, hats come in halfway.
        { "Pluck", 0, theme (0.0, -12, 90) },
        { "Pad", 0, pad (0.0, 8, 70) },
        { "Drums", 0, drums (4 * kBar, 4, { true, false, false, false, 0 }) },
        // Build: kick, bass and claps; a snare roll and the riser in the last bars.
        { "Pluck", 1, theme (0.0, -12, 100) },
        { "Pad", 1, pad (0.0, 8, 75) },
        { "Kick", 1, kicks (0.0, 6) },
        { "Bass", 1, bass (0.0, 8, 100) },
        { "Drums", 1, drums (0.0, 8, { true, true, false, false, 2 }) },
        { "Riser", 1, { { 4 * kBar, 4 * kBar - 0.25, 71, 100 } } },
        // Drop: the lead takes the theme, twice.
        { "Lead", 2, concat (theme (0.0, 0, 100), theme (b8, 0, 105)) },
        { "Pluck", 2, concat (theme (0.0, -12, 95), theme (b8, -12, 95)) },
        { "Pad", 2, pad (0.0, 16, 75) },
        { "Kick", 2, kicks (0.0, 16) },
        { "Bass", 2, bass (0.0, 16, 110) },
        { "Drums", 2, drums (0.0, 16, { true, true, true, true, 0 }) },
        // Break: no drums, the pad and the pluck alone, the riser and a roll back into the drop.
        { "Pad", 3, pad (0.0, 8, 80) },
        { "Pluck", 3, theme (0.0, -12, 85) },
        { "Riser", 3, { { 4 * kBar, 4 * kBar - 0.25, 71, 100 } } },
        { "Drums", 3, drums (0.0, 8, { false, false, false, false, 2 }) },
        // Drop 2: the lead in octaves.
        { "Lead", 4, concat (concat (theme (0.0, 0, 105), theme (b8, 0, 110)), concat (theme (0.0, 12, 80), theme (b8, 12, 85))) },
        { "Pluck", 4, concat (theme (0.0, -12, 100), theme (b8, -12, 100)) },
        { "Pad", 4, pad (0.0, 16, 80) },
        { "Kick", 4, kicks (0.0, 16) },
        { "Bass", 4, bass (0.0, 16, 115) },
        { "Drums", 4, drums (0.0, 16, { true, true, true, true, 0 }) },
        // Outro: the tonic chord rings out, a last kick and crash.
        { "Pad", 5, { { 0.0, 15.0, 59, 80 }, { 0.0, 15.0, 62, 80 }, { 0.0, 15.0, 66, 80 }, { 0.0, 15.0, 71, 80 } } },
        { "Bass", 5, { { 0.0, 3.5, 47, 110 } } },
        { "Kick", 5, { { 0.0, 0.25, Drum::kick, 120 } } },
        { "Drums", 5, { { 0.0, 2.0, Drum::crash, 110 } } },
        { "Pluck", 5, { { 0.0, 0.5, 59, 100 }, { 0.5, 0.5, 66, 90 }, { 1.0, 0.5, 71, 85 }, { 1.5, 2.0, 78, 80 } } },
    };
    // The drops open with a crash on the drum track.
    for (auto& p : parts)
        if (p.track == "Drums" && (p.section == 2 || p.section == 4))
            p.notes.push_back ({ 0.0, 2.0, Drum::crash, 115 });

    auto trackNamed = [&] (const juce::String& name) {
        for (const auto& t : model.tracks())
            if (t.name() == name)
                return t;
        return TrackRef();
    };

    for (const auto& p : parts)
    {
        const auto& s = sections[(size_t) p.section];
        const auto track = trackNamed (p.track);
        auto clip = model.createClip (track, p.section, s.bars * kBar, p.track + " " + s.name);
        clip.setNotes (p.notes, um);
        clip.setLoopEnabled (false, um);
    }

    // The Arrangement: every scene laid on the timeline at its section, with markers.
    for (size_t i = 0; i < sections.size(); ++i)
    {
        const auto& s = sections[i];
        for (const auto& track : model.tracks())
            if (const auto clip = track.clipAt ((int) i); clip.isValid())
                arrangement::addSlotClip (model, track, clip, s.startBar * kBar);
        arrangement::addMarker (model, s.startBar * kBar, s.name);
    }

    //==========================================================================
    // Automation (the "A" mode of the Arrangement).
    // The pluck filter opens over the intro and closes into the break.
    {
        const auto target = automation::deviceTarget (pluckSynth.id(), prismaParamIndex ("flt_cutoff"));
        auto lane = automation::getOrAddLane (model, pluck, target);
        const float closed = prismaNormalised ("flt_cutoff", 250.0f), open = prismaNormalised ("flt_cutoff", 2200.0f);
        automation::addPoint (model, lane, { 0.0, closed, 0.3f });
        automation::addPoint (model, lane, { 16 * kBar, open, 0.0f });
        automation::addPoint (model, lane, { 32 * kBar, open, -0.3f });
        automation::addPoint (model, lane, { 36 * kBar, closed, 0.4f });
        automation::addPoint (model, lane, { 40 * kBar, open, 0.0f });
    }
    // The riser sweeps up before each drop.
    {
        const auto target = automation::deviceTarget (riserSynth.id(), prismaParamIndex ("flt_cutoff"));
        auto lane = automation::getOrAddLane (model, riser, target);
        const float low = prismaNormalised ("flt_cutoff", 300.0f), high = prismaNormalised ("flt_cutoff", 9000.0f);
        for (int start : { 12, 36 })
        {
            automation::addPoint (model, lane, { start * kBar, low, 0.5f });
            automation::addPoint (model, lane, { (start + 4) * kBar - 0.25, high, 0.0f });
        }
    }
    // The pad fades out at the end.
    {
        auto lane = automation::getOrAddLane (model, padTrack, automation::volumeTarget);
        automation::addPoint (model, lane, { 56 * kBar, automation::normalisedVolume (-1.0f), 0.0f });
        automation::addPoint (model, lane, { 60 * kBar, automation::normalisedVolume (-40.0f), 0.0f });
    }

    model.setArrangementLoop (16 * kBar, 32 * kBar, false);
    model.setSelectedTrack (lead.id());
    model.undo.clearUndoHistory();
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce;
    const auto out = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                              : juce::File::getCurrentWorkingDirectory().getChildFile ("mountain-king");
    out.createDirectory();

    juce::TemporaryFile settingsFile (".settings");
    Settings settings (settingsFile.getFile());
    auto model = ProjectModel::createEmpty();
    build (model);

    // Save as the app does: "<name> Project/<name>.prisma".
    const auto folder = out.getChildFile ("Mountain King Project");
    juce::String error;
    if (! project::writeSave (folder, "Mountain King", project::serialise (model.root), juce::Time::currentTimeMillis(), error))
    {
        std::printf ("save failed: %s\n", error.toRawUTF8());
        return 1;
    }
    std::printf ("project: %s\n", project::mainFile (folder, "Mountain King").getFullPathName().toRawUTF8());

    // Load it back, so the render plays exactly what was saved.
    const auto loaded = project::loadProjectFile (project::mainFile (folder, "Mountain King"));
    if (! loaded.ok())
    {
        std::printf ("reload failed: %s\n", loaded.error.toRawUTF8());
        return 1;
    }
    ProjectModel reloaded (loaded.tree);

    AudioEngine engine (settings, false);
    engine.setProjectFolder (folder);
    engine.setModel (&reloaded);
    engine.rebuildGraphIfNeeded();

    OfflineRenderer::Options o;
    o.startBeat = 0.0;
    o.endBeat = arrangement::songLengthBeats (reloaded);
    o.file = out.getChildFile ("Mountain King.wav");
    o.format = "wav";
    o.bitDepth = 24;
    o.normalisePeakDb = 1.0; // the master limiter sets the level
    o.tail = true;
    OfflineRenderer renderer (settings, reloaded, engine, o);
    const auto r = renderer.renderNow();
    engine.setModel (nullptr);
    if (! r.ok)
    {
        std::printf ("render failed: %s\n", r.error.toRawUTF8());
        return 1;
    }
    std::printf ("render: %s, %.1f s, peak %.2f dBFS\n", o.file.getFullPathName().toRawUTF8(), r.seconds, r.peakDb);

    // --stems: every track soloed, for checking the balance of the mix.
    if (argc > 2 && juce::String (argv[2]) == "--stems")
        for (const auto& track : reloaded.tracks())
        {
            if (track.isReturn())
                continue;
            track.setSolo (true, nullptr);
            engine.setModel (&reloaded);
            engine.rebuildGraphIfNeeded();
            auto so = o;
            so.tail = false;
            so.file = out.getChildFile ("stems").getChildFile (track.name() + ".wav");
            so.file.getParentDirectory().createDirectory();
            OfflineRenderer stem (settings, reloaded, engine, so);
            const auto sr = stem.renderNow();
            engine.setModel (nullptr);
            track.setSolo (false, nullptr);
            std::printf ("stem %-8s peak %6.1f dBFS\n", track.name().toRawUTF8(), sr.peakDb);
        }
    return 0;
}
