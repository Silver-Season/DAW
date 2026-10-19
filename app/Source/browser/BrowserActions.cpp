#include "BrowserActions.h"

#include "../engine/DrumKitProcessor.h"
#include "daw/fx/Effect.h"

#include "../Strings.h"
#include "../engine/AudioEngine.h"
#include "../model/ArrangementEdit.h"
#include "../project/AudioImport.h"
#include "../project/MidiImport.h"

#include "../../../plugins/prisma/Source/PrismaProcessor.h"

namespace browser
{
juce::var makeDrag (const Item& item)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("kind", item.kind);
    o->setProperty ("id", item.id);
    o->setProperty ("path", item.path);
    o->setProperty ("name", item.name);
    o->setProperty ("key", item.key);
    o->setProperty ("instrument", item.instrument);
    return juce::var (o);
}

bool parseDrag (const juce::var& v, DragItem& out)
{
    auto* o = v.getDynamicObject();
    if (o == nullptr || ! o->hasProperty ("kind"))
        return false;
    out.kind = o->getProperty ("kind").toString();
    out.id = o->getProperty ("id").toString();
    out.path = o->getProperty ("path").toString();
    out.name = o->getProperty ("name").toString();
    out.key = o->getProperty ("key").toString();
    out.instrument = (bool) o->getProperty ("instrument");
    return out.kind.isNotEmpty();
}

//==============================================================================
BrowserActions::BrowserActions (ProjectModel& m, AudioEngine& e, library::UserLibrary& l) : model (m), engine (e), library (l) {}

double BrowserActions::beatsPerBar() const
{
    return (double) model.root.getProperty (ids::sigNum, 4) * 4.0 / (double) model.root.getProperty (ids::sigDen, 4);
}

juce::String BrowserActions::factoryState (int program) const
{
    if (const auto it = factoryStates.find (program); it != factoryStates.end())
        return it->second;
    PrismaProcessor p;
    p.setCurrentProgram (program);
    juce::MemoryBlock block;
    p.getStateInformation (block);
    return factoryStates[program] = block.toBase64Encoding();
}

BrowserActions::DeviceSpec BrowserActions::resolve (const DragItem& item) const
{
    DeviceSpec s;
    if (item.kind == kinds::prisma)
    {
        s.valid = true;
        s.kind = "prisma";
        s.deviceName = "Prisma";
        s.typeId = "prisma";
    }
    else if (item.kind == kinds::prismaPreset)
    {
        const int index = item.id.getIntValue();
        if (index < 0 || index >= (int) daw::synth::factoryPresets().size())
            return s;
        s.valid = true;
        s.kind = "prisma";
        s.deviceName = "Prisma";
        s.presetName = item.name;
        s.typeId = "prisma";
        s.state = factoryState (index);
    }
    else if (item.kind == kinds::userPreset)
    {
        const auto data = library::UserLibrary::readPreset (juce::File (item.path));
        if (! data.valid)
            return s;
        s.presetName = data.name;
        s.state = data.state;
        if (data.deviceKind == "prisma")
        {
            s.valid = true;
            s.kind = "prisma";
            s.deviceName = "Prisma";
            s.typeId = "prisma";
        }
        else if (const auto xml = juce::XmlDocument::parse (data.pluginDesc))
        {
            juce::PluginDescription desc;
            if (desc.loadFromXml (*xml))
            {
                s.valid = true;
                s.kind = "plugin";
                s.deviceName = desc.name;
                s.pluginDesc = data.pluginDesc;
                s.typeId = desc.createIdentifierString();
                s.instrument = desc.isInstrument;
            }
        }
    }
    else if (item.kind == kinds::drumKit)
    {
        s.valid = true;
        s.kind = "drums";
        s.deviceName = Strings::drums::deviceName;
        s.presetName = item.id;
        s.typeId = "drums";
        s.state = DrumKitProcessor::factoryStateBase64 (item.id);
    }
    else if (item.kind == kinds::builtinFx)
    {
        s.valid = daw::fx::createEffect (item.id.toStdString()) != nullptr;
        s.kind = "fx";
        s.deviceName = Strings::fx::effectName (item.id);
        s.pluginDesc = item.id;
        s.typeId = "fx:" + item.id;
        s.instrument = false;
    }
    else if (item.kind == kinds::plugin)
    {
        if (const auto desc = engine.getKnownPlugins().getTypeForIdentifierString (item.id))
        {
            s.valid = true;
            s.kind = "plugin";
            s.deviceName = desc->name;
            s.pluginDesc = desc->createXml()->toString (juce::XmlElement::TextFormat().singleLine());
            s.typeId = desc->createIdentifierString();
            s.instrument = desc->isInstrument;
        }
    }
    return s;
}

BrowserActions::ChainSpec BrowserActions::resolveChain (const DragItem& item) const
{
    ChainSpec c;
    const auto data = library::UserLibrary::readChain (juce::File (item.path));
    if (! data.valid || data.devices.isEmpty())
        return c;
    c.valid = true;
    c.name = data.name;
    c.colour = data.colour;
    c.devices = data.devices;
    return c;
}

juce::String BrowserActions::describe (const DeviceSpec& s)
{
    return s.presetName.isEmpty() ? s.deviceName : s.deviceName + juce::String::fromUTF8 (" \xc2\xb7 ") + s.presetName;
}

bool BrowserActions::deviceHasType (const DeviceRef& d, const juce::String& typeId) const
{
    if (typeId == "prisma")
        return d.kind() == "prisma";
    if (typeId == "drums")
        return d.kind() == "drums";
    if (typeId.startsWith ("fx:"))
        return d.kind() == "fx" && d.pluginDesc() == typeId.substring (3);
    if (d.kind() != "plugin" && d.kind() != "missing")
        return false;
    juce::PluginDescription desc;
    if (const auto xml = juce::XmlDocument::parse (d.pluginDesc()); xml != nullptr && desc.loadFromXml (*xml))
        return desc.createIdentifierString() == typeId;
    return false;
}

//==============================================================================
bool BrowserActions::accepts (const DragItem& item, DropTarget target, int64_t trackId, int64_t deviceId) const
{
    const bool needsTrack = target == DropTarget::track || target == DropTarget::slot || target == DropTarget::chainGap || target == DropTarget::device;
    // The master takes effects in its chain (track and gap targets), nothing that makes clips.
    const auto track = target == DropTarget::chainGap || target == DropTarget::track ? model.findTrackOrMaster (trackId) : model.findTrack (trackId);
    if (needsTrack && ! track.isValid() && target != DropTarget::device)
        return false;
    if (track.isMaster() && (item.kind == kinds::midiFile || item.kind == kinds::audioFile))
        return false;

    const bool audioTrack = track.isValid() && ! track.hasInstrument();
    if (item.kind == kinds::midiFile)
        return target == DropTarget::newTrack || (target == DropTarget::slot && ! audioTrack);
    if (item.kind == kinds::audioFile)
        return target == DropTarget::newTrack || ((target == DropTarget::slot || target == DropTarget::track) && audioTrack);

    if (item.kind == kinds::chain)
        return target != DropTarget::device && ! audioTrack; // a chain begins with an instrument

    const bool isPreset = item.kind == kinds::prismaPreset || item.kind == kinds::userPreset;
    if (item.kind == kinds::prisma || item.kind == kinds::prismaPreset || item.kind == kinds::userPreset || item.kind == kinds::plugin
        || item.kind == kinds::builtinFx || item.kind == kinds::drumKit)
    {
        if (item.kind == kinds::userPreset && ! library::UserLibrary::readPreset (juce::File (item.path)).valid)
            return false;
        // An effect only makes sense on a track that exists: an empty MIDI track would have no sound.
        if (target == DropTarget::newTrack)
            return true; // an effect makes an audio track that carries it
        if (item.kind == kinds::drumKit && target == DropTarget::device) // a kit replaces the kit of a drum machine
            return model.findDevice (deviceId).kind() == "drums";
        // An audio track has no instrument: only effects go on it (a preset only onto its own device, below).
        if (audioTrack && target != DropTarget::device && (item.instrument || item.kind == kinds::prisma || item.kind == kinds::prismaPreset))
            return false;
        if (target == DropTarget::device)
        {
            if (! isPreset)
                return false;
            const auto device = model.findDevice (deviceId);
            if (! device.isValid())
                return false;
            const auto spec = resolve (item);
            return spec.valid && deviceHasType (device, spec.typeId);
        }
        return true;
    }
    return false;
}

//==============================================================================
DeviceRef BrowserActions::addSpec (const TrackRef& track, const DeviceSpec& spec, int index)
{
    auto d = model.addDevice (track, spec.kind, spec.deviceName, index);
    if (spec.kind == "plugin" || spec.kind == "fx")
        d.setPluginDesc (spec.pluginDesc, &model.undo);
    if (spec.state.isNotEmpty())
        d.setStateBase64 (spec.state, &model.undo);
    return d;
}

DeviceRef BrowserActions::addNode (const TrackRef& track, const juce::ValueTree& node, int index)
{
    DeviceRef source (node);
    auto d = model.addDevice (track, source.kind(), source.name(), index);
    d.setEnabled (source.enabled(), &model.undo);
    d.setFolded (source.folded(), &model.undo);
    if (source.pluginDesc().isNotEmpty())
        d.setPluginDesc (source.pluginDesc(), &model.undo);
    if (source.stateBase64().isNotEmpty())
        d.setStateBase64 (source.stateBase64(), &model.undo);
    if (source.macroParams().isNotEmpty())
        d.setMacroParams (source.macroParams(), &model.undo);
    return d;
}

void BrowserActions::removeAllDevices (const TrackRef& track)
{
    for (int i = track.numDevices() - 1; i >= 0; --i)
        engine.removeDevice (track.device (i));
}

TrackRef BrowserActions::newTrackAt (int64_t afterTrackId, const juce::String& name, const juce::String& type)
{
    const int index = afterTrackId >= 0 ? model.indexOfTrack (afterTrackId) + 1 : -1;
    auto track = model.addTrack (type, index);
    if (name.isNotEmpty())
        track.setName (name, &model.undo);
    return track;
}

DropResult BrowserActions::drop (const DragItem& item, DropTarget target, int64_t trackId, int scene, int index, int64_t deviceId)
{
    DropResult r;
    if (! accepts (item, target, trackId, deviceId))
        return r;

    if (item.kind == kinds::midiFile)
        return dropMidiFile (juce::File (item.path), target, trackId, scene);
    if (item.kind == kinds::audioFile)
        return dropAudioFiles ({ juce::File (item.path) }, target, trackId, target == DropTarget::track ? -1 : scene);

    if (item.kind == kinds::chain)
    {
        const auto chain = resolveChain (item);
        if (! chain.valid)
        {
            r.message = Strings::errorChainRead (item.name);
            return r;
        }
        model.undo.beginNewTransaction (target == DropTarget::newTrack ? Strings::undoAddChain (chain.name) : Strings::undoReplaceChain (chain.name));
        TrackRef track;
        if (target == DropTarget::newTrack)
        {
            track = newTrackAt (-1, chain.name);
            track.setColour (juce::jlimit (0, 15, chain.colour), &model.undo);
        }
        else
        {
            track = model.findTrack (trackId);
            removeAllDevices (track);
        }
        for (const auto& node : chain.devices)
            addNode (track, node, -1);
        r.ok = true;
        r.trackId = track.id();
        return r;
    }

    const auto spec = resolve (item);
    if (! spec.valid)
    {
        r.message = Strings::errorPluginUnknown (item.name);
        return r;
    }

    if (target == DropTarget::newTrack)
    {
        model.undo.beginNewTransaction (Strings::undoAddDevice (describe (spec)));
        auto track = newTrackAt (-1, spec.presetName.isNotEmpty() ? spec.presetName : spec.deviceName, spec.instrument ? "midi" : "audio");
        r.deviceId = addSpec (track, spec, -1).id();
        r.trackId = track.id();
        r.ok = true;
        return r;
    }

    if (target == DropTarget::device)
    {
        const auto device = model.findDevice (deviceId);
        const auto track = TrackRef (device.state.getParent().getParent());
        const int at = device.state.getParent().indexOf (device.state);
        model.undo.beginNewTransaction (Strings::undoApplyPreset (describe (spec)));
        const bool enabled = device.enabled(), folded = device.folded();
        const auto macros = device.macroParams();
        engine.removeDevice (device);
        auto fresh = addSpec (track, spec, at);
        fresh.setEnabled (enabled, &model.undo);
        fresh.setFolded (folded, &model.undo);
        if (macros.isNotEmpty())
            fresh.setMacroParams (macros, &model.undo);
        r.ok = true;
        r.trackId = track.id();
        r.deviceId = fresh.id();
        return r;
    }

    // track, slot or gap. A preset replaces the device of its own type wherever it is in the chain.
    const auto track = model.findTrackOrMaster (trackId);
    const bool isPreset = item.kind == kinds::prismaPreset || item.kind == kinds::userPreset;
    int sameType = -1;
    if (isPreset)
        for (int i = 0; i < track.numDevices() && sameType < 0; ++i)
            if (deviceHasType (track.device (i), spec.typeId))
                sameType = i;

    if (sameType >= 0 || spec.instrument)
    {
        // Otherwise an instrument replaces the instrument (the first device).
        const int at = sameType >= 0 ? sameType : 0;
        const bool replacing = track.numDevices() > at;
        model.undo.beginNewTransaction (sameType >= 0 ? Strings::undoApplyPreset (describe (spec))
                                                      : (replacing ? Strings::undoReplaceInstrument (describe (spec)) : Strings::undoAddDevice (describe (spec))));
        bool enabled = true, folded = false;
        juce::String macros;
        if (replacing)
        {
            const auto old = track.device (at);
            enabled = old.enabled();
            folded = old.folded();
            macros = sameType >= 0 ? old.macroParams() : juce::String();
            engine.removeDevice (old);
        }
        auto fresh = addSpec (track, spec, at);
        fresh.setEnabled (enabled, &model.undo);
        fresh.setFolded (folded, &model.undo);
        if (macros.isNotEmpty())
            fresh.setMacroParams (macros, &model.undo);
        r.deviceId = fresh.id();
    }
    else
    {
        // an effect: at the gap, otherwise at the end; never in front of the instrument
        int at = (target == DropTarget::chainGap && index >= 0) ? index : track.numDevices();
        at = juce::jlimit (track.hasInstrument() ? juce::jmin (1, track.numDevices()) : 0, track.numDevices(), at);
        model.undo.beginNewTransaction (Strings::undoAddEffect (describe (spec)));
        r.deviceId = addSpec (track, spec, at).id();
    }
    r.ok = true;
    r.trackId = trackId;
    return r;
}

DropResult BrowserActions::dropMidiFile (const juce::File& file, DropTarget target, int64_t trackId, int scene)
{
    DropResult r;
    if (target != DropTarget::newTrack && target != DropTarget::slot)
        return r;
    if (! file.hasFileExtension ("mid;midi"))
        return r;

    project::ImportedClip clip;
    if (! project::importMidiFile (file, beatsPerBar(), clip, r.message))
        return r;

    model.undo.beginNewTransaction (Strings::undoAddMidiClip (clip.name));
    TrackRef track;
    int slot = scene;
    if (target == DropTarget::newTrack)
    {
        track = newTrackAt (-1, clip.name);
        addSpec (track, { true, "prisma", "Prisma", {}, {}, {}, "prisma", true }, -1);
        slot = 0;
    }
    else
        track = model.findTrack (trackId);
    if (! track.isValid() || slot < 0 || slot >= model.numScenes())
    {
        r.message = Strings::errorNoSlot;
        return r;
    }
    auto created = model.createClip (track, slot, clip.lengthBeats, clip.name);
    created.setNotes (clip.notes, &model.undo);
    r.ok = true;
    r.trackId = track.id();
    return r;
}

bool BrowserActions::acceptsOnTimeline (const DragItem& item, int64_t trackId) const
{
    const auto track = model.findTrack (trackId);
    if (item.kind == kinds::audioFile)
        return ! track.isValid() || track.isAudio();
    if (item.kind == kinds::midiFile)
        return ! track.isValid() || track.type() == "midi";
    return track.isValid() ? accepts (item, DropTarget::track, trackId, -1) : accepts (item, DropTarget::newTrack, -1, -1);
}

DropResult BrowserActions::dropOnTimeline (const DragItem& item, int64_t trackId, double beat, bool alternate)
{
    if (item.kind == kinds::audioFile)
        return dropAudioOnTimeline ({ juce::File (item.path) }, trackId, beat, alternate);
    if (item.kind != kinds::midiFile)
        return drop (item, trackId >= 0 ? DropTarget::track : DropTarget::newTrack, trackId, -1, -1, -1);

    DropResult r;
    project::ImportedClip clip;
    if (! project::importMidiFile (juce::File (item.path), beatsPerBar(), clip, r.message))
        return r;
    model.undo.beginNewTransaction (Strings::undoAddMidiClip (clip.name));
    auto track = model.findTrack (trackId);
    if (! track.isValid() || track.type() != "midi")
    {
        track = newTrackAt (-1, clip.name);
        addSpec (track, { true, "prisma", "Prisma", {}, {}, {}, "prisma", true }, -1);
    }
    auto a = arrangement::addEmptyClip (model, track, juce::jmax (0.0, beat), clip.lengthBeats, clip.name);
    if (a.clip().isValid())
        a.clip().setNotes (clip.notes, &model.undo);
    r.ok = a.isValid();
    r.trackId = track.id();
    return r;
}

bool BrowserActions::importAll (const juce::Array<juce::File>& files, std::vector<ImportedSound>& out, DropResult& r)
{
    if (engine.getProjectFolder() == juce::File() && ensureProjectFolder)
        ensureProjectFolder();
    const bool copy = engine.getSettings().copySamplesIntoProject();
    for (const auto& f : files)
    {
        const auto imported = audioimport::importFile (model, engine, f, copy);
        if (imported.ok)
            out.push_back ({ imported.fileId, imported.name, imported.lengthBeats });
    }
    if (out.empty())
        r.message = Strings::errorAudioRead;
    return ! out.empty();
}

DropResult BrowserActions::dropAudioFiles (const juce::Array<juce::File>& files, DropTarget target, int64_t trackId, int scene, bool alternate)
{
    DropResult r;
    auto track = model.findTrack (trackId);
    if (target != DropTarget::newTrack && ! (track.isValid() && track.isAudio()))
    {
        r.message = Strings::errorAudioOnMidi;
        return r;
    }
    if (files.isEmpty())
        return r;
    model.undo.beginNewTransaction (files.size() == 1 ? Strings::undoAddAudioClip (files[0].getFileNameWithoutExtension())
                                                      : Strings::undoAddAudioClips (files.size()));
    std::vector<ImportedSound> sounds;
    if (! importAll (files, sounds, r))
        return r;

    if (target == DropTarget::newTrack)
    {
        track = {};
        scene = 0;
    }
    else if (scene < 0) // onto the header: the first empty slot
    {
        scene = 0;
        while (scene < model.numScenes() && track.clipAt (scene).isValid())
            ++scene;
    }
    int64_t after = track.isValid() ? track.id() : -1;
    for (size_t i = 0; i < sounds.size(); ++i)
    {
        const auto& s = sounds[i];
        if (! track.isValid() || (alternate && i > 0))
        {
            track = newTrackAt (after, s.name, "audio");
            after = track.id();
        }
        const int slot = alternate ? scene : scene + (int) i;
        while (slot >= model.numScenes())
            model.insertScene (-1);
        model.createAudioClip (track, slot, s.fileId, s.lengthBeats, s.name);
        r.trackId = track.id();
    }
    model.adoptTempoOfFirstSound (sounds.front().fileId); // a tempo known from the name; else the analysis does it
    r.ok = true;
    return r;
}

DropResult BrowserActions::dropAudioOnTimeline (const juce::Array<juce::File>& files, int64_t trackId, double beat, bool alternate)
{
    DropResult r;
    auto track = model.findTrack (trackId);
    if (track.isValid() && ! track.isAudio())
    {
        r.message = Strings::errorAudioOnMidi;
        return r;
    }
    if (files.isEmpty())
        return r;
    model.undo.beginNewTransaction (files.size() == 1 ? Strings::undoAddAudioClip (files[0].getFileNameWithoutExtension())
                                                      : Strings::undoAddAudioClips (files.size()));
    std::vector<ImportedSound> sounds;
    if (! importAll (files, sounds, r))
        return r;

    beat = juce::jmax (0.0, beat);
    double at = beat;
    for (size_t i = 0; i < sounds.size(); ++i)
    {
        const auto& s = sounds[i];
        if (! track.isValid() || (! alternate && i > 0))
            track = newTrackAt (track.isValid() ? track.id() : -1, s.name, "audio");
        auto node = model.makeAudioClipNode (track, s.fileId, s.lengthBeats, s.name);
        auto a = arrangement::addSlotClip (model, track, ClipRef (node), alternate ? at : beat);
        a.setLoop (false, &model.undo); // a sound on the timeline plays once; stretching its end repeats it only if asked
        at += s.lengthBeats;
        r.trackId = track.id();
    }
    model.adoptTempoOfFirstSound (sounds.front().fileId); // a tempo known from the name; else the analysis does it
    r.ok = true;
    return r;
}

DropResult BrowserActions::useOnTrack (const DragItem& item, int64_t selectedTrackId)
{
    if (selectedTrackId == ProjectModel::kMasterTrackId && item.kind != kinds::audioFile && item.kind != kinds::midiFile)
    {
        if (accepts (item, DropTarget::track, selectedTrackId))
            return drop (item, DropTarget::track, selectedTrackId);
        return {};
    }
    const auto track = model.findTrack (selectedTrackId);
    if (item.kind == kinds::audioFile)
        return dropAudioFiles ({ juce::File (item.path) }, track.isValid() && track.isAudio() ? DropTarget::track : DropTarget::newTrack,
                               selectedTrackId, -1);
    if (item.kind == kinds::midiFile)
    {
        if (track.isValid())
            for (int s = 0; s < model.numScenes(); ++s)
                if (! track.clipAt (s).isValid())
                    return dropMidiFile (juce::File (item.path), DropTarget::slot, track.id(), s);
        return dropMidiFile (juce::File (item.path), DropTarget::newTrack, -1);
    }
    if (! track.isValid() || ! accepts (item, DropTarget::track, selectedTrackId))
        return drop (item, DropTarget::newTrack, -1);
    return drop (item, DropTarget::track, selectedTrackId);
}

//==============================================================================
juce::File BrowserActions::savePreset (int64_t deviceId, const juce::String& name, juce::String& error)
{
    const auto device = model.findDevice (deviceId);
    if (! device.isValid() || name.trim().isEmpty())
        return {};
    juce::String state = device.stateBase64();
    if (auto* processor = engine.findProcessor (deviceId))
    {
        juce::MemoryBlock block;
        processor->getStateInformation (block);
        state = block.toBase64Encoding();
    }
    const bool prisma = device.kind() == "prisma";
    return library.savePreset (prisma ? "Prisma" : device.name(), name, prisma ? "prisma" : "plugin", prisma ? juce::String() : device.pluginDesc(),
                               state, error);
}

juce::File BrowserActions::saveChain (int64_t trackId, const juce::String& name, juce::String& error)
{
    const auto track = model.findTrack (trackId);
    if (! track.isValid() || name.trim().isEmpty())
        return {};
    engine.captureDeviceStates (track.state);
    juce::Array<juce::ValueTree> nodes;
    for (const auto& d : track.devices())
        nodes.add (d.state);
    return library.saveChain (name, track.colour(), nodes, error);
}
} // namespace browser
