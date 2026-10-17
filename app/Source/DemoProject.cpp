#include "DemoProject.h"

#include "model/ArrangementEdit.h"

#include "../../plugins/prisma/Source/PrismaProcessor.h"

#include "daw/engine/DemoContent.h"
#include "daw/synth/PrismaParams.h"

namespace
{
enum class DemoClip
{
    chords,
    arp,
    bass,
    longBass,
    lead
};

daw::engine::MidiClip makeDemoClip (DemoClip type)
{
    using namespace daw::engine::demo;
    switch (type)
    {
        case DemoClip::chords: return padClip();
        case DemoClip::arp: return arpClip();
        case DemoClip::bass: return bassClip (false);
        case DemoClip::longBass: return bassClip (true);
        case DemoClip::lead: return leadClip();
    }
    return {};
}

// The device state of Prisma with a factory preset loaded, as it is stored in a DEVICE node.
juce::String prismaStateForPreset (const juce::String& presetName)
{
    PrismaProcessor prisma;
    const auto& presets = daw::synth::factoryPresets();
    for (size_t i = 0; i < presets.size(); ++i)
        if (presetName == juce::String (presets[i].name))
            prisma.setCurrentProgram ((int) i);

    juce::MemoryBlock state;
    prisma.getStateInformation (state);
    return state.toBase64Encoding();
}
} // namespace

void populateDemoProject (ProjectModel& model)
{
    struct TrackSetup
    {
        const char* name;
        const char* preset;
        float volumeDb;
        std::vector<std::pair<int, DemoClip>> clips; // scene, clip
    };
    const std::vector<TrackSetup> setup {
        { "Pad", "Glass Pad", -4.0f, { { 0, DemoClip::chords }, { 1, DemoClip::chords }, { 2, DemoClip::chords } } },
        { "Arp", "Pluck", -9.0f, { { 0, DemoClip::arp }, { 1, DemoClip::arp } } },
        { "Reese", "Reese Bass", -5.0f, { { 0, DemoClip::bass } } },
        { "Lead", "Supersaw Lead", -10.0f, { { 1, DemoClip::lead } } },
        { "Wobble", "Wobble Bass", -6.0f, { { 1, DemoClip::longBass } } },
        { "Vox", "Vox Sweep", -8.0f, { { 2, DemoClip::lead } } },
        { "Track 7", "Init", -6.0f, {} },
        { "Track 8", "Init", -6.0f, {} },
    };

    while (model.numScenes() < 8)
        model.insertScene (-1);

    auto* um = &model.undo;
    for (const auto& s : setup)
    {
        auto track = model.addTrack ("midi");
        track.setName (s.name, um);
        track.setVolumeDb (s.volumeDb, um);
        auto device = model.addDevice (track, "prisma", "Prisma");
        device.setStateBase64 (prismaStateForPreset (s.preset), um);

        for (const auto& [scene, type] : s.clips)
        {
            const auto source = makeDemoClip (type);
            auto clip = model.createClip (track, scene, source.lengthBeats, source.name);
            clip.setNotes (source.notes, um);
        }
        if (juce::String (s.name) == "Lead")
            track.setArm (true, um); // like phase 0: the lead is armed for playing
    }

    // The scenes laid out on the timeline as a short song (the Arrangement View has something to show): the clips of a
    // scene play for the whole section, repeating their loop.
    struct Section
    {
        int scene;
        double startBeat, lengthBeats;
        const char* marker;
    };
    const Section song[] { { 0, 0.0, 32.0, "Интро" }, { 1, 32.0, 32.0, "Дроп" }, { 2, 64.0, 16.0, "Брейк" }, { 1, 80.0, 32.0, "Финал" } };
    for (const auto& section : song)
    {
        for (const auto& track : model.tracks())
        {
            const auto source = track.clipAt (section.scene);
            if (! source.isValid())
                continue;
            auto a = arrangement::addSlotClip (model, track, source, section.startBeat);
            arrangement::resizeRight (model, a, section.startBeat + section.lengthBeats);
        }
        arrangement::addMarker (model, section.startBeat, juce::String::fromUTF8 (section.marker));
    }
    model.setArrangementLoop (32.0, 64.0, false);

    model.setTempo (124.0);
    model.setSelectedTrack (model.trackAt (3).id());
    model.undo.clearUndoHistory();
}
