#include "Inspiration.h"

#include "Strings.h"
#include "engine/AudioEngine.h"
#include "engine/DrumKitProcessor.h"

#include "../../plugins/prisma/Source/PrismaProcessor.h"
#include "daw/gen/Patches.h"
#include "daw/gen/Patterns.h"
#include "daw/music/Scales.h"
#include "daw/synth/PrismaParams.h"

namespace inspiration
{
namespace
{
uint32_t freshSeed() { return (uint32_t) juce::Random::getSystemRandom().nextInt (0x7fffffff) + 1u; }

daw::gen::GenContext contextFor (const ProjectModel& model, const juce::String& style, uint32_t seed)
{
    daw::gen::GenContext ctx;
    ctx.root = model.scaleRoot();
    // Without a project scale the ideas are in a minor key, the most common choice for beats.
    const auto mode = model.scaleMode();
    ctx.scale = daw::music::findScale (mode.isNotEmpty() ? mode.toRawUTF8() : "minor");
    if (mode.isEmpty())
        ctx.root = 9; // A minor
    ctx.beatsPerBar = model.beatsPerBar();
    ctx.bars = 4;
    ctx.seed = seed;
    ctx.style = style.toStdString();
    ctx.density = 0.4 + 0.3 * juce::Random (seed).nextDouble();
    ctx.complexity = 0.3 + 0.4 * juce::Random (seed + 7).nextDouble();
    return ctx;
}

const char* kitForStyle (const juce::String& style)
{
    if (style == "trap")
        return "Trap";
    if (style == "hiphop")
        return "Boom Bap";
    if (style == "techno")
        return "Techno";
    if (style == "ambient")
        return "Lo-Fi";
    if (style == "house")
        return "909 Club";
    return "808 Classic";
}

daw::gen::SoundKind kindOf (const juce::String& key)
{
    for (const auto k : daw::gen::allSoundKinds())
        if (key == daw::gen::soundKindKey (k))
            return k;
    return daw::gen::SoundKind::lead;
}

// The Prisma state of a generated patch, as a DEVICE node stores it.
juce::String patchState (daw::gen::SoundKind kind, uint32_t seed)
{
    PrismaProcessor prisma;
    const auto values = daw::gen::generatePatch (kind, seed, 0.35f);
    const auto& descriptors = daw::synth::parameterDescriptors();
    for (size_t i = 0; i < descriptors.size() && i < values.size(); ++i)
        if (auto* param = prisma.apvts.getParameter (descriptors[i].id))
            param->setValueNotifyingHost (param->convertTo0to1 (values[i]));
    juce::MemoryBlock state;
    prisma.getStateInformation (state);
    return state.toBase64Encoding();
}

TrackRef newSynthTrack (ProjectModel& model, daw::gen::SoundKind kind, uint32_t seed)
{
    auto track = model.addTrack ("midi");
    track.setName (juce::String (daw::gen::patchName (kind, seed)), &model.undo);
    auto device = model.addDevice (track, "prisma", "Prisma");
    device.setStateBase64 (patchState (kind, seed), &model.undo);
    return track;
}

TrackRef newDrumTrack (ProjectModel& model, const juce::String& style)
{
    auto track = model.addTrack ("midi", 0);
    track.setName (Strings::drums::group, &model.undo);
    auto kit = model.addDevice (track, "drums", Strings::drums::deviceName);
    kit.setStateBase64 (DrumKitProcessor::factoryStateBase64 (kitForStyle (style)), &model.undo);
    return track;
}

bool hasDrumMachine (const TrackRef& t)
{
    return t.isValid() && t.numDevices() > 0 && t.device (0).kind() == "drums";
}

int emptySlot (ProjectModel& model, const TrackRef& track, int fromScene)
{
    for (int s = juce::jmax (0, fromScene); s < track.numSlots(); ++s)
        if (! track.clipAt (s).isValid())
            return s;
    model.insertScene (-1);
    return model.numScenes() - 1;
}

daw::gen::Notes notesFor (Part part, const daw::gen::GenContext& ctx)
{
    std::vector<int> degrees;
    auto chordNotes = daw::gen::chords (ctx, &degrees);
    switch (part)
    {
        case Part::drums: return daw::gen::humanise (daw::gen::drums (ctx), 0.01, 10, ctx.seed, ctx.bars * ctx.beatsPerBar);
        case Part::chords: return chordNotes;
        case Part::bass: return daw::gen::bass (ctx, degrees);
        case Part::melody: return daw::gen::melody (ctx, degrees);
        case Part::arp: return daw::gen::arp (ctx, degrees, daw::gen::ArpPattern::upDown, 0.25);
    }
    return {};
}

const juce::String& partName (Part part)
{
    switch (part)
    {
        case Part::drums: return Strings::inspiration::drums;
        case Part::chords: return Strings::inspiration::chords;
        case Part::bass: return Strings::inspiration::bass;
        case Part::melody: return Strings::inspiration::melody;
        case Part::arp: return Strings::inspiration::arp;
    }
    return Strings::inspiration::melody;
}

daw::gen::SoundKind soundForPart (Part part)
{
    switch (part)
    {
        case Part::bass: return daw::gen::SoundKind::bass;
        case Part::chords: return daw::gen::SoundKind::pad;
        case Part::arp: return daw::gen::SoundKind::pluck;
        case Part::melody:
        case Part::drums: break;
    }
    return daw::gen::SoundKind::lead;
}

// Writes a part into a slot of the track; the caller opened the transaction.
void writeClip (ProjectModel& model, const TrackRef& track, int scene, const juce::String& name, const daw::gen::Notes& notes, double length)
{
    auto clip = model.createClip (model.findTrack (track.id()), scene, length, name);
    clip.setNotes (notes, &model.undo);
}
} // namespace

Result makePart (ProjectModel& model, AudioEngine& engine, Part part, const juce::String& style, int64_t selectedTrack, int fromScene)
{
    Result r;
    const auto seed = freshSeed();
    const auto ctx = contextFor (model, style, seed);
    const double length = ctx.bars * ctx.beatsPerBar;

    model.undo.beginNewTransaction (Strings::inspiration::undo);
    auto track = model.findTrack (selectedTrack);
    const bool drums = part == Part::drums;
    if (drums ? ! hasDrumMachine (track) : (! track.isValid() || track.type() != "midi" || hasDrumMachine (track)))
        track = drums ? newDrumTrack (model, style) : newSynthTrack (model, soundForPart (part), seed);
    const int scene = emptySlot (model, track, fromScene);
    writeClip (model, track, scene, partName (part), notesFor (part, ctx), length);
    engine.rebuildGraph();

    r.ok = true;
    r.trackId = track.id();
    r.scene = scene;
    r.message = Strings::inspiration::made (partName (part), track.name());
    return r;
}

Result makeSound (ProjectModel& model, AudioEngine& engine, const juce::String& kindKey, int64_t selectedTrack)
{
    Result r;
    const auto seed = freshSeed();
    const auto kind = kindOf (kindKey);
    model.undo.beginNewTransaction (Strings::inspiration::undoSound);
    auto track = model.findTrack (selectedTrack);
    const juce::String name (daw::gen::patchName (kind, seed));
    if (track.isValid() && track.numDevices() > 0 && track.device (0).kind() == "prisma")
    {
        // The new sound replaces the one of the track's Prisma (undo brings the old one back).
        auto device = track.device (0);
        engine.captureDeviceStates (device.state);
        engine.removeDevice (device);
        auto fresh = model.addDevice (track, "prisma", "Prisma", 0);
        fresh.setStateBase64 (patchState (kind, seed), &model.undo);
    }
    else
        track = newSynthTrack (model, kind, seed);
    engine.rebuildGraph();
    r.ok = true;
    r.trackId = track.id();
    r.message = Strings::inspiration::newSound (name, track.name());
    return r;
}

Result makeIdea (ProjectModel& model, AudioEngine& engine, const juce::String& style)
{
    Result r;
    const auto seed = freshSeed();
    const auto ctx = contextFor (model, style, seed);
    const double length = ctx.bars * ctx.beatsPerBar;
    model.undo.beginNewTransaction (Strings::inspiration::undoIdea);
    model.insertScene (-1);
    const int scene = model.numScenes() - 1;
    model.sceneAt (scene).setName (Strings::inspiration::ideaScene (Strings::inspiration::styleName (style)), &model.undo);

    auto drums = newDrumTrack (model, style);
    writeClip (model, drums, scene, Strings::inspiration::drums, notesFor (Part::drums, ctx), length);
    for (const auto part : { Part::bass, Part::chords, Part::melody })
    {
        auto track = newSynthTrack (model, soundForPart (part), seed + (uint32_t) part);
        writeClip (model, track, scene, partName (part), notesFor (part, ctx), length);
    }
    engine.rebuildGraph();
    r.ok = true;
    r.trackId = drums.id();
    r.scene = scene;
    r.message = Strings::inspiration::idea;
    return r;
}

juce::PopupMenu menu (ProjectModel& model, AudioEngine& engine, std::function<int64_t()> selectedTrack, std::function<int()> selectedScene,
                      std::function<void (const Result&)> done)
{
    juce::PopupMenu m;
    const auto styles = daw::gen::styleNames();
    const auto styleMenu = [&] (std::function<void (const juce::String&)> action) {
        juce::PopupMenu sub;
        for (const auto& s : styles)
        {
            const juce::String style (s);
            sub.addItem (Strings::inspiration::styleName (style), [action, style] { action (style); });
        }
        return sub;
    };

    m.addSubMenu (Strings::inspiration::idea, styleMenu ([&model, &engine, done] (const juce::String& style) { done (makeIdea (model, engine, style)); }));
    m.addSeparator();
    for (const auto part : { Part::drums, Part::chords, Part::bass, Part::melody, Part::arp })
        m.addSubMenu (partName (part), styleMenu ([&model, &engine, part, selectedTrack, selectedScene, done] (const juce::String& style) {
                          done (makePart (model, engine, part, style, selectedTrack(), selectedScene()));
                      }));
    m.addSeparator();
    juce::PopupMenu sounds;
    for (const auto kind : daw::gen::allSoundKinds())
    {
        const juce::String key (daw::gen::soundKindKey (kind));
        sounds.addItem (Strings::inspiration::soundKind (key), [&model, &engine, key, selectedTrack, done] { done (makeSound (model, engine, key, selectedTrack())); });
    }
    m.addSubMenu (Strings::inspiration::sound, sounds);
    return m;
}
} // namespace inspiration
