#pragma once

#include "ProjectModel.h"

#include <vector>

// "Take a track from a version" (docs/spec/03-arrangement.md, task 3.5): a track of another project (an older version
// loaded into memory, never into the engine) is copied into the open project with everything it holds.
namespace transfer
{
struct TrackInfo
{
    int64_t id = 0;
    juce::String name;
    int numDevices = 0;
    int numSessionClips = 0;
    int numArrangementClips = 0;
};

// The tracks of `other`, in order.
std::vector<TrackInfo> listTracks (const ProjectModel& other);

// Appends a copy of the track `trackId` of `source` to the end of `dest`: devices with their saved state, the clips of
// Session View and the clips of the arrangement (takes included), all with fresh ids, so nothing can clash with what
// `dest` holds. The copy is called "<name> (<tag>)", e.g. "Reese (v12)", and is not armed.
//
// The scenes of the two projects need not match. The copy gets exactly as many slots as `dest` has scenes: slots
// beyond them that are empty are dropped, and if the source has clips beyond the last scene of `dest`, scenes are
// added at the end of `dest` for them (they are numbered like any new scene), so no clip is lost.
//
// Writes through dest.undo and opens no transaction: the caller opens one, so the whole import is one undo step.
// Returns an invalid ref if the track does not exist.
TrackRef importTrack (ProjectModel& dest, const ProjectModel& source, int64_t trackId, const juce::String& tag);
} // namespace transfer
