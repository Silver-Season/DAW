#include "BrowserActions.h"

#include "../Strings.h"
#include "../engine/AudioEngine.h"
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
    if (needsTrack && ! model.findTrack (trackId).isValid() && target != DropTarget::device)
        return false;

    if (item.kind == kinds::midiFile)
        return target == DropTarget::newTrack || target == DropTarget::slot;
    if (item.kind == kinds::audioFile)
        return false; // audio tracks arrive with stage 5

    if (item.kind == kinds::chain)
        return target != DropTarget::device;

    const bool isPreset = item.kind == kinds::prismaPreset || item.kind == kinds::userPreset;
    if (item.kind == kinds::prisma || item.kind == kinds::prismaPreset || item.kind == kinds::userPreset || item.kind == kinds::plugin)
    {
        if (item.kind == kinds::userPreset && ! library::UserLibrary::readPreset (juce::File (item.path)).valid)
            return false;
        // An effect only makes sense on a track that exists: an empty MIDI track would have no sound.
        if (target == DropTarget::newTrack)
            return item.instrument || item.kind == kinds::prisma || item.kind == kinds::prismaPreset;
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
    if (spec.kind == "plugin")
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

TrackRef BrowserActions::newTrackAt (int64_t afterTrackId, const juce::String& name)
{
    const int index = afterTrackId >= 0 ? model.indexOfTrack (afterTrackId) + 1 : -1;
    auto track = model.addTrack ("midi", index);
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
        auto track = newTrackAt (-1, spec.presetName.isNotEmpty() ? spec.presetName : spec.deviceName);
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
    const auto track = model.findTrack (trackId);
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
        at = juce::jlimit (juce::jmin (1, track.numDevices()), track.numDevices(), at);
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

DropResult BrowserActions::useOnTrack (const DragItem& item, int64_t selectedTrackId)
{
    const auto track = model.findTrack (selectedTrackId);
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
