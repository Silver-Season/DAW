#pragma once

#include "BrowserCatalog.h"

#include "../library/UserLibrary.h"
#include "../model/ProjectModel.h"

class AudioEngine;
class Settings;

// What dropping a browser item (or a file from Finder) does to the project (docs/spec/01-layout-project.md,
// tasks 1.5 and 1.6). Each drop is exactly one undo transaction, named after what happened ("Добавить Prisma · Reese Bass").
namespace browser
{
struct ImportedSound
{
    int64_t fileId = -1;
    juce::String name;
    double lengthBeats = 4.0;
};

// The description a dragged browser row carries: { kind, id, path, name, instrument }.
struct DragItem
{
    juce::String kind, id, path, name, key;
    bool instrument = false;
};
juce::var makeDrag (const Item&);
bool parseDrag (const juce::var&, DragItem&);

enum class DropTarget
{
    newTrack,  // empty space of the Session View: a new MIDI track
    track,     // a track header or a slot of a track
    slot,      // a clip slot (track + scene)
    chainGap,  // between devices of the chain of a track (index)
    device     // one device
};

struct DropResult
{
    bool ok = false;
    int64_t trackId = -1;  // the track that was created or changed
    int64_t deviceId = -1; // the device that was created or changed
    juce::String message;  // why nothing happened, for the info bar
};

class BrowserActions
{
public:
    BrowserActions (ProjectModel&, AudioEngine&, library::UserLibrary&);

    // Whether the target may light up for this item. Cheap enough to ask on every mouse move.
    bool accepts (const DragItem&, DropTarget, int64_t trackId = -1, int64_t deviceId = -1) const;

    // `scene` for slots, `index` for chain gaps (0 = before the instrument).
    DropResult drop (const DragItem&, DropTarget, int64_t trackId, int scene = -1, int index = -1, int64_t deviceId = -1);
    // A .mid file from Finder or from the browser's MIDI list.
    DropResult dropMidiFile (const juce::File&, DropTarget, int64_t trackId, int scene = -1);
    // Sound files (docs/spec/05-audio-recording.md, task 5.6), from the browser or from Finder, as one undo transaction.
    // Session View: onto a slot of an audio track (several files fill that slot and the ones below it, one per scene; with
    // `alternate` each file gets a track of its own next to it instead), or onto empty space (new audio tracks).
    DropResult dropAudioFiles (const juce::Array<juce::File>&, DropTarget, int64_t trackId, int scene, bool alternate = false);
    // Arrangement: onto the lane of an audio track at `beat` (trackId -1: new audio tracks at the end). Several files go
    // under each other, one track each (the first on the lane that was hit); with `alternate` one after another on it.
    DropResult dropAudioOnTimeline (const juce::Array<juce::File>&, int64_t trackId, double beat, bool alternate = false);
    // Anything from the browser onto the Arrangement: onto a lane (trackId) or the empty space below (-1). Sounds and
    // MIDI files become clips at `beat`; instruments, effects, presets and chains go into the track's chain, or make a
    // new track that holds them.
    bool acceptsOnTimeline (const DragItem&, int64_t trackId) const;
    DropResult dropOnTimeline (const DragItem&, int64_t trackId, double beat, bool alternate = false);
    // The project folder, made if there is none yet, so dropped files can be copied into it. Set by the window.
    std::function<juce::File()> ensureProjectFolder;

    // Double click and Enter in the browser: the item goes to the selected track (or to a new one).
    DropResult useOnTrack (const DragItem&, int64_t selectedTrackId);

    // 1.6: the library. Return the file written (invalid File and `error` on failure).
    juce::File savePreset (int64_t deviceId, const juce::String& name, juce::String& error);
    juce::File saveChain (int64_t trackId, const juce::String& name, juce::String& error);

    static bool isPrismaDevice (const DeviceRef& d) { return d.kind() == "prisma"; }

private:
    struct DeviceSpec
    {
        bool valid = false;
        juce::String kind;       // "prisma" | "plugin"
        juce::String deviceName; // "Prisma" or the plugin's name
        juce::String presetName; // for the transaction name and the new track
        juce::String pluginDesc, state;
        juce::String typeId;     // "prisma" or the plugin's identifier string
        bool instrument = true;
    };
    struct ChainSpec
    {
        bool valid = false;
        juce::String name;
        int colour = 0;
        juce::Array<juce::ValueTree> devices;
    };

    DeviceSpec resolve (const DragItem&) const;
    ChainSpec resolveChain (const DragItem&) const;
    juce::String factoryState (int program) const;
    bool deviceHasType (const DeviceRef&, const juce::String& typeId) const;
    DeviceRef addSpec (const TrackRef&, const DeviceSpec&, int index);
    DeviceRef addNode (const TrackRef&, const juce::ValueTree& node, int index);
    void removeAllDevices (const TrackRef&);
    static juce::String describe (const DeviceSpec&);
    TrackRef newTrackAt (int64_t afterTrackId, const juce::String& name, const juce::String& type = "midi");
    // Imports the files (skipping what cannot be read); false with r.message set if none could be.
    bool importAll (const juce::Array<juce::File>&, std::vector<struct ImportedSound>&, DropResult& r);
    double beatsPerBar() const;

    ProjectModel& model;
    AudioEngine& engine;
    library::UserLibrary& library;
    mutable std::map<int, juce::String> factoryStates;
};
} // namespace browser
