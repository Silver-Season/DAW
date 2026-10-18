#include "ProjectModel.h"

#include "../Strings.h"

#include <algorithm>

namespace
{
// New tracks walk around the palette so that neighbours differ: 0, 2, 4, ... 14, then 1, 3, ... 15.
int paletteIndexForTrack (int ordinal)
{
    const int n = ordinal % 16;
    return n < 8 ? n * 2 : (n - 8) * 2 + 1;
}

void collectMaxId (const juce::ValueTree& v, int64_t& maxId)
{
    if (v.hasProperty (ids::id))
        maxId = juce::jmax (maxId, (int64_t) (juce::int64) v.getProperty (ids::id));
    for (const auto& c : v)
        collectMaxId (c, maxId);
}
} // namespace

//==============================================================================
juce::ValueTree ProjectModel::createEmptyTree()
{
    juce::ValueTree p (ids::PROJECT);
    p.setProperty (ids::schemaVersion, kSchemaVersion, nullptr);
    p.setProperty (ids::name, "", nullptr);
    p.setProperty (ids::tempo, 120.0, nullptr);
    p.setProperty (ids::sigNum, 4, nullptr);
    p.setProperty (ids::sigDen, 4, nullptr);
    p.setProperty (ids::launchQuantise, 4.0, nullptr);
    p.setProperty (ids::nextId, (juce::int64) 1, nullptr);
    p.setProperty (ids::selectedTrack, (juce::int64) -1, nullptr);

    p.appendChild (juce::ValueTree (ids::TRACKS), nullptr);

    juce::ValueTree master (ids::MASTER);
    master.setProperty (ids::volumeDb, 0.0, nullptr);
    master.appendChild (juce::ValueTree (ids::DEVICES), nullptr);
    p.appendChild (master, nullptr);

    p.appendChild (juce::ValueTree (ids::SCENES), nullptr);
    p.appendChild (juce::ValueTree (ids::MARKERS), nullptr);
    p.appendChild (juce::ValueTree (ids::SNAPSHOTS), nullptr);
    p.appendChild (juce::ValueTree (ids::AUDIOFILES), nullptr);
    return p;
}

void ProjectModel::normalise (juce::ValueTree& p)
{
    const auto ensure = [&p] (const juce::Identifier& type) {
        auto child = p.getChildWithName (type);
        if (! child.isValid())
        {
            child = juce::ValueTree (type);
            p.appendChild (child, nullptr);
        }
        return child;
    };
    ensure (ids::TRACKS);
    auto master = ensure (ids::MASTER);
    if (! master.getChildWithName (ids::DEVICES).isValid())
        master.appendChild (juce::ValueTree (ids::DEVICES), nullptr);
    auto scenes = ensure (ids::SCENES);
    ensure (ids::MARKERS);
    ensure (ids::SNAPSHOTS);
    ensure (ids::AUDIOFILES);

    const int numScenes = scenes.getNumChildren();
    for (auto track : p.getChildWithName (ids::TRACKS))
    {
        if (! track.getChildWithName (ids::DEVICES).isValid())
            track.appendChild (juce::ValueTree (ids::DEVICES), nullptr);
        if (! track.getChildWithName (ids::ARRANGEMENT).isValid()) // files of schema 1 may lack it
            track.appendChild (juce::ValueTree (ids::ARRANGEMENT), nullptr);
        auto slots = track.getChildWithName (ids::SLOTS);
        if (! slots.isValid())
        {
            slots = juce::ValueTree (ids::SLOTS);
            track.appendChild (slots, nullptr);
        }
        while (slots.getNumChildren() < numScenes)
            slots.appendChild (juce::ValueTree (ids::SLOT), nullptr);
        while (slots.getNumChildren() > numScenes)
            slots.removeChild (slots.getNumChildren() - 1, nullptr);
    }
}

ProjectModel::ProjectModel (juce::ValueTree projectTree) : root (std::move (projectTree))
{
    normalise (root);
    int64_t maxId = 0;
    collectMaxId (root, maxId);
    highWater = maxId + 1;
    undo.clearUndoHistory();
}

void ProjectModel::adopt (juce::ValueTree projectTree)
{
    normalise (projectTree);
    int64_t maxId = 0;
    collectMaxId (projectTree, maxId);
    highWater = maxId + 1;
    undo.clearUndoHistory();
    root = projectTree; // tells the listeners of `root` through valueTreeRedirected
}

ProjectModel::ProjectModel (DefaultTag) : root (createEmptyTree())
{
    for (int i = 0; i < 8; ++i)
        insertScene (-1);
    for (int i = 0; i < 2; ++i)
    {
        auto track = addTrack ("midi");
        addDevice (track, "prisma", "Prisma");
    }
    setSelectedTrack (trackAt (0).id());
    undo.clearUndoHistory();
}

ProjectModel ProjectModel::createDefault()
{
    return ProjectModel (DefaultTag {});
}

ProjectModel ProjectModel::createEmpty()
{
    return ProjectModel (createEmptyTree());
}

int64_t ProjectModel::newId()
{
    // The property is written through the undo manager so that undoing the creation of an object also
    // restores the counter exactly (the XML after undo equals the XML before). highWater keeps ids from
    // being reused anyway.
    const int64_t next = juce::jmax ((int64_t) (juce::int64) root.getProperty (ids::nextId, 1), highWater);
    highWater = next + 1;
    root.setProperty (ids::nextId, (juce::int64) (next + 1), &undo);
    return next;
}

juce::ValueTree ProjectModel::copyWithNewIds (const juce::ValueTree& tree)
{
    auto copy = tree.createCopy();
    reassignIds (copy);
    return copy;
}

std::vector<MarkerRef> ProjectModel::markers() const
{
    std::vector<MarkerRef> out;
    for (const auto& m : markersNode())
        if (m.hasType (ids::MARKER))
            out.emplace_back (m);
    std::stable_sort (out.begin(), out.end(), [] (const auto& a, const auto& b) { return a.beat() < b.beat(); });
    return out;
}

void ProjectModel::reassignIds (juce::ValueTree& v)
{
    if (v.hasProperty (ids::id))
        v.setProperty (ids::id, (juce::int64) newId(), nullptr);
    for (auto c : v)
        reassignIds (c);
}

//==============================================================================
juce::Array<TrackRef> ProjectModel::tracks() const
{
    juce::Array<TrackRef> out;
    for (const auto& t : tracksNode())
        out.add (TrackRef (t));
    return out;
}

TrackRef ProjectModel::findTrack (int64_t id) const
{
    for (const auto& t : tracksNode())
        if ((int64_t) (juce::int64) t.getProperty (ids::id, -1) == id)
            return TrackRef (t);
    return {};
}

int ProjectModel::indexOfTrack (int64_t id) const
{
    int i = 0;
    for (const auto& t : tracksNode())
    {
        if ((int64_t) (juce::int64) t.getProperty (ids::id, -1) == id)
            return i;
        ++i;
    }
    return -1;
}

juce::ValueTree ProjectModel::makeSlot()
{
    return juce::ValueTree (ids::SLOT);
}

TrackRef ProjectModel::addTrack (const juce::String& type, int insertIndex)
{
    juce::ValueTree t (ids::TRACK);
    t.setProperty (ids::id, (juce::int64) newId(), nullptr);
    t.setProperty (ids::type, type, nullptr);
    t.setProperty (ids::name, type == "audio" ? Strings::defaultAudioTrackName (numTracks() + 1) : Strings::defaultTrackName (numTracks() + 1), nullptr);
    t.setProperty (ids::colour, paletteIndexForTrack (numTracks()), nullptr);
    t.setProperty (ids::volumeDb, 0.0, nullptr);
    t.setProperty (ids::pan, 0.0, nullptr);
    t.setProperty (ids::mute, false, nullptr);
    t.setProperty (ids::solo, false, nullptr);
    t.setProperty (ids::arm, false, nullptr);
    const bool audio = type == "audio";
    t.setProperty (ids::input, audio ? "audio:0-1" : "all-midi", nullptr);
    t.setProperty (ids::monitor, "auto", nullptr);
    t.setProperty (ids::recordSource, audio ? "audio" : "midi", nullptr);
    t.setProperty (ids::width, 120, nullptr);

    t.appendChild (juce::ValueTree (ids::DEVICES), nullptr);
    juce::ValueTree slots (ids::SLOTS);
    for (int i = 0; i < numScenes(); ++i)
        slots.appendChild (makeSlot(), nullptr);
    t.appendChild (slots, nullptr);
    t.appendChild (juce::ValueTree (ids::ARRANGEMENT), nullptr);

    const int index = insertIndex < 0 ? -1 : juce::jmin (insertIndex, numTracks());
    tracksNode().addChild (t, index, &undo);
    return TrackRef (t);
}

void ProjectModel::deleteTrack (int64_t id)
{
    const int index = indexOfTrack (id);
    if (index >= 0)
        tracksNode().removeChild (index, &undo);
}

TrackRef ProjectModel::duplicateTrack (int64_t id)
{
    const auto src = findTrack (id);
    if (! src.isValid())
        return {};
    auto copy = src.state.createCopy();
    reassignIds (copy);
    tracksNode().addChild (copy, indexOfTrack (id) + 1, &undo);
    return TrackRef (copy);
}

//==============================================================================
SceneRef ProjectModel::insertScene (int index)
{
    const int n = numScenes();
    const int at = (index < 0 || index > n) ? n : index;

    juce::ValueTree s (ids::SCENE);
    s.setProperty (ids::id, (juce::int64) newId(), nullptr);
    s.setProperty (ids::name, Strings::defaultSceneName (n + 1), nullptr);
    s.setProperty (ids::colour, -1, nullptr);
    s.setProperty (ids::tempo, -1.0, nullptr);
    scenesNode().addChild (s, at, &undo);

    for (const auto& t : tracks())
        t.slotsNode().addChild (makeSlot(), at, &undo);
    return SceneRef (s);
}

void ProjectModel::deleteScene (int index)
{
    if (index < 0 || index >= numScenes())
        return;
    scenesNode().removeChild (index, &undo);
    for (const auto& t : tracks())
        t.slotsNode().removeChild (index, &undo);
}

void ProjectModel::duplicateScene (int index)
{
    if (index < 0 || index >= numScenes())
        return;
    const auto src = sceneAt (index);
    auto copy = insertScene (index + 1);
    copy.setName (src.name(), &undo);
    copy.setColour (src.colour(), &undo);
    copy.setTempo (src.tempo(), &undo);
    for (const auto& t : tracks())
    {
        const auto clip = t.clipAt (index);
        if (clip.isValid())
            setClip (t, index + 1, clip);
    }
}

//==============================================================================
ClipRef ProjectModel::createClip (const TrackRef& track, int scene, double lengthBeats, const juce::String& name)
{
    juce::ValueTree c (ids::CLIP);
    c.setProperty (ids::id, (juce::int64) newId(), nullptr);
    c.setProperty (ids::name, name.isEmpty() ? Strings::defaultClipName : name, nullptr);
    c.setProperty (ids::colour, track.colour(), nullptr);
    c.setProperty (ids::lengthBeats, lengthBeats, nullptr);
    c.setProperty (ids::loopStart, 0.0, nullptr);
    c.setProperty (ids::loopEnd, lengthBeats, nullptr);
    c.setProperty (ids::startOffset, 0.0, nullptr);
    c.setProperty (ids::loop, true, nullptr);
    // A new clip starts out with the project's scale; it follows the project until "own scale" is switched on.
    c.setProperty (ids::ownScale, false, nullptr);
    c.setProperty (ids::scaleRoot, scaleRoot(), nullptr);
    c.setProperty (ids::scaleMode, scaleMode(), nullptr);

    auto slot = track.slotsNode().getChild (scene);
    jassert (slot.isValid());
    if (! slot.isValid())
        return {};
    slot.removeAllChildren (&undo);
    slot.appendChild (c, &undo);
    return ClipRef (c);
}

juce::ValueTree ProjectModel::makeAudioClipNode (const TrackRef& track, int64_t fileId, double lengthBeats, const juce::String& name)
{
    juce::ValueTree c (ids::CLIP);
    c.setProperty (ids::id, (juce::int64) newId(), nullptr);
    c.setProperty (ids::name, name.isEmpty() ? Strings::defaultClipName : name, nullptr);
    c.setProperty (ids::colour, track.colour(), nullptr);
    c.setProperty (ids::lengthBeats, lengthBeats, nullptr);
    c.setProperty (ids::loopStart, 0.0, nullptr);
    c.setProperty (ids::loopEnd, lengthBeats, nullptr);
    c.setProperty (ids::startOffset, 0.0, nullptr);
    c.setProperty (ids::loop, true, nullptr);
    c.setProperty (ids::file, (juce::int64) fileId, nullptr);
    c.setProperty (ids::sampleStart, 0.0, nullptr);
    c.setProperty (ids::gainDb, 0.0, nullptr);
    c.setProperty (ids::transpose, 0, nullptr);
    // A loop whose tempo is known follows the project tempo from the start (Re-Pitch until stage 7 brings warping).
    c.setProperty (ids::warp, findAudioFile (fileId).tempo() > 0.0, nullptr);
    return c;
}

ClipRef ProjectModel::createAudioClip (const TrackRef& track, int scene, int64_t fileId, double lengthBeats, const juce::String& name)
{
    auto slot = track.slotsNode().getChild (scene);
    jassert (slot.isValid());
    if (! slot.isValid())
        return {};
    auto c = makeAudioClipNode (track, fileId, lengthBeats, name);
    slot.removeAllChildren (&undo);
    slot.appendChild (c, &undo);
    return ClipRef (c);
}

AudioFileRef ProjectModel::findAudioFile (int64_t id) const
{
    for (const auto& f : audioFilesNode())
        if ((int64_t) (juce::int64) f.getProperty (ids::id, -1) == id)
            return AudioFileRef (f);
    return {};
}

AudioFileRef ProjectModel::findAudioFileByHash (const juce::String& hash) const
{
    if (hash.isNotEmpty())
        for (const auto& f : audioFilesNode())
            if (f.getProperty (ids::hash).toString() == hash)
                return AudioFileRef (f);
    return {};
}

AudioFileRef ProjectModel::addAudioFile (const AudioFileInfo& info)
{
    juce::ValueTree f (ids::FILE);
    f.setProperty (ids::id, (juce::int64) newId(), nullptr);
    f.setProperty (ids::name, info.name, nullptr);
    f.setProperty (ids::path, info.path, nullptr);
    f.setProperty (ids::absPath, info.absPath, nullptr);
    f.setProperty (ids::sampleRate, info.sampleRate, nullptr);
    f.setProperty (ids::channels, info.channels, nullptr);
    f.setProperty (ids::lengthSamples, info.lengthSamples, nullptr);
    f.setProperty (ids::hash, info.hash, nullptr);
    if (info.tempo > 0.0)
        f.setProperty (ids::tempo, info.tempo, nullptr);
    auto pool = audioFilesNode();
    if (! pool.isValid())
    {
        pool = juce::ValueTree (ids::AUDIOFILES);
        root.appendChild (pool, &undo);
    }
    pool.appendChild (f, &undo);
    return AudioFileRef (f);
}

ClipRef ProjectModel::setClip (const TrackRef& track, int scene, const ClipRef& source)
{
    auto slot = track.slotsNode().getChild (scene);
    jassert (slot.isValid());
    if (! slot.isValid() || ! source.isValid())
        return {};
    auto copy = source.state.createCopy();
    reassignIds (copy);
    slot.removeAllChildren (&undo);
    slot.appendChild (copy, &undo);
    return ClipRef (copy);
}

void ProjectModel::deleteClip (const TrackRef& track, int scene)
{
    auto slot = track.slotsNode().getChild (scene);
    if (slot.isValid())
        slot.removeAllChildren (&undo);
}

void ProjectModel::moveClip (int64_t fromTrack, int fromScene, int64_t toTrack, int toScene, bool copy)
{
    const auto src = findTrack (fromTrack), dst = findTrack (toTrack);
    if (! src.isValid() || ! dst.isValid() || (src == dst && fromScene == toScene))
        return;
    const auto clip = src.clipAt (fromScene);
    if (! clip.isValid() || ! dst.slotsNode().getChild (toScene).isValid())
        return;

    if (copy)
    {
        setClip (dst, toScene, clip);
        return;
    }
    auto tree = clip.state;
    auto fromSlot = src.slotsNode().getChild (fromScene);
    auto toSlot = dst.slotsNode().getChild (toScene);
    fromSlot.removeChild (tree, &undo);
    toSlot.removeAllChildren (&undo);
    toSlot.appendChild (tree, &undo);
}

ClipRef ProjectModel::findClip (int64_t id) const
{
    for (const auto& t : tracksNode())
    {
        for (const auto& slot : t.getChildWithName (ids::SLOTS))
            for (const auto& c : slot)
                if ((int64_t) (juce::int64) c.getProperty (ids::id, -1) == id)
                    return ClipRef (c);
        // the content of an arrangement clip is a clip, too (the piano roll opens it)
        for (const auto& a : t.getChildWithName (ids::ARRANGEMENT))
            for (const auto& c : a)
                if (c.hasType (ids::CLIP) && (int64_t) (juce::int64) c.getProperty (ids::id, -1) == id)
                    return ClipRef (c);
    }
    return {};
}

//==============================================================================
DeviceRef ProjectModel::addDevice (juce::ValueTree devicesNode, const juce::String& kind, const juce::String& name, int index)
{
    juce::ValueTree d (ids::DEVICE);
    d.setProperty (ids::id, (juce::int64) newId(), nullptr);
    d.setProperty (ids::kind, kind, nullptr);
    d.setProperty (ids::name, name, nullptr);
    d.setProperty (ids::enabled, true, nullptr);
    d.setProperty (ids::folded, false, nullptr);
    devicesNode.addChild (d, index < 0 ? -1 : juce::jmin (index, devicesNode.getNumChildren()), &undo);
    return DeviceRef (d);
}

DeviceRef ProjectModel::addDevice (const TrackRef& track, const juce::String& kind, const juce::String& name, int index)
{
    return addDevice (track.devicesNode(), kind, name, index);
}

void ProjectModel::removeDevice (const DeviceRef& d)
{
    auto parent = d.state.getParent();
    if (parent.isValid())
        parent.removeChild (d.state, &undo);
}

void ProjectModel::moveDevice (const DeviceRef& d, int newIndex)
{
    auto parent = d.state.getParent();
    if (parent.isValid())
        parent.moveChild (parent.indexOf (d.state), juce::jlimit (0, parent.getNumChildren() - 1, newIndex), &undo);
}

DeviceRef ProjectModel::findDevice (int64_t id) const
{
    const auto search = [id] (const juce::ValueTree& devices) {
        for (const auto& d : devices)
            if ((int64_t) (juce::int64) d.getProperty (ids::id, -1) == id)
                return DeviceRef (d);
        return DeviceRef();
    };
    for (const auto& t : tracksNode())
        if (auto d = search (t.getChildWithName (ids::DEVICES)); d.isValid())
            return d;
    return search (masterDevicesNode());
}
