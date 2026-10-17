#include "TrackTransfer.h"

#include <functional>

namespace transfer
{
std::vector<TrackInfo> listTracks (const ProjectModel& other)
{
    std::vector<TrackInfo> out;
    for (const auto& t : other.tracks())
    {
        TrackInfo info;
        info.id = t.id();
        info.name = t.name();
        info.numDevices = t.numDevices();
        for (int s = 0; s < t.numSlots(); ++s)
            info.numSessionClips += t.clipAt (s).isValid() ? 1 : 0;
        info.numArrangementClips = t.numArrangementClips();
        out.push_back (info);
    }
    return out;
}

TrackRef importTrack (ProjectModel& dest, const ProjectModel& source, int64_t trackId, const juce::String& tag)
{
    const auto from = source.findTrack (trackId);
    if (! from.isValid())
        return {};

    // The last clip of the session decides how many scenes the copy needs.
    int needed = 0;
    for (int s = 0; s < from.numSlots(); ++s)
        if (from.clipAt (s).isValid())
            needed = s + 1;
    while (dest.numScenes() < needed)
        dest.insertScene (-1);

    auto copy = dest.copyWithNewIds (from.state);
    TrackRef track (copy);
    track.setName (from.name() + " (" + tag + ")", nullptr);
    track.setArm (false, nullptr);

    auto slots = copy.getChildWithName (ids::SLOTS);
    if (! slots.isValid())
    {
        slots = juce::ValueTree (ids::SLOTS);
        copy.appendChild (slots, nullptr);
    }
    while (slots.getNumChildren() > dest.numScenes())
        slots.removeChild (slots.getNumChildren() - 1, nullptr);
    while (slots.getNumChildren() < dest.numScenes())
        slots.appendChild (juce::ValueTree (ids::SLOT), nullptr);
    if (! copy.getChildWithName (ids::DEVICES).isValid())
        copy.appendChild (juce::ValueTree (ids::DEVICES), nullptr);
    if (! copy.getChildWithName (ids::ARRANGEMENT).isValid())
        copy.appendChild (juce::ValueTree (ids::ARRANGEMENT), nullptr);

    dest.tracksNode().addChild (copy, -1, &dest.undo);

    // Audio clips play files of the source's pool: the ones `dest` lacks come along, under their own ids (ids are never
    // reused within a project's history, so a version's FILE id is that same file or no file of `dest` at all).
    const std::function<void (const juce::ValueTree&)> bringFiles = [&] (const juce::ValueTree& v) {
        if (v.hasType (ids::CLIP) && v.hasProperty (ids::file))
        {
            const auto fileId = (int64_t) (juce::int64) v.getProperty (ids::file);
            if (! dest.findAudioFile (fileId).isValid())
                if (const auto f = source.findAudioFile (fileId); f.isValid())
                {
                    auto pool = dest.audioFilesNode();
                    if (! pool.isValid())
                    {
                        pool = juce::ValueTree (ids::AUDIOFILES);
                        dest.root.appendChild (pool, &dest.undo);
                    }
                    pool.appendChild (f.state.createCopy(), &dest.undo);
                }
        }
        for (const auto& c : v)
            bringFiles (c);
    };
    bringFiles (copy);
    return track;
}
} // namespace transfer
