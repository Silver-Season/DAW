#pragma once

#include "ProjectModel.h"

#include <set>
#include <vector>

// Edits of the Arrangement timeline (docs/spec/03-arrangement.md, tasks 3.1 and 3.3): clips on tracks, markers and
// the song length. Like ClipEdit.h, none of these opens an undo transaction: the caller does, once per action (a mouse
// gesture is one transaction). Every change goes through model.undo, so undoing the transaction restores everything.
//
// The rule behind all of it: ACLIPs of one track never overlap. Whatever creates, moves or stretches a clip calls
// resolveOverlaps, and the clip that was placed wins: the one underneath is cut in two, trimmed or removed.
// Beats are song beats (quarter notes from the start of the song).
namespace arrangement
{
using Clips = std::vector<ArrangementClipRef>;

constexpr double kMinClipBeats = 1.0 / 16.0;  // the shortest clip a gesture can make
constexpr double kSliverBeats = 1.0 / 64.0;   // pieces shorter than this that an overlap leaves behind are dropped
constexpr int kMinSongBars = 16;              // the song is at least this long
constexpr int kEmptyBarsAfterEnd = 8;         // the view shows this much empty time after the end

//==============================================================================
// Queries
ArrangementClipRef findClip (const ProjectModel&, int64_t aclipId);
TrackRef trackOf (const ProjectModel&, const ArrangementClipRef&);
// The clip that covers `beat` (start <= beat < end) on the track, or an invalid ref.
ArrangementClipRef clipAt (const TrackRef&, double beat);
// The clips that overlap [from, to), sorted by start.
Clips clipsIn (const TrackRef&, double from, double to);
// The end of the last clip or marker, at least kMinSongBars bars. The view adds kEmptyBarsAfterEnd bars.
double songLengthBeats (const ProjectModel&);

//==============================================================================
// Overlaps
// Makes room for the clip `winnerId` on `track`: every other clip that overlaps it is cut in two (the part after the
// winner becomes a new clip with its own copy of the content), trimmed at the near edge (offset follows) or removed.
void resolveOverlaps (ProjectModel&, const TrackRef& track, int64_t winnerId);
// The same for a bare time range: removes whatever lies in [from, to) on the track (Delete on a time selection).
void clearRange (ProjectModel&, const TrackRef& track, double from, double to);

//==============================================================================
// Creating clips (each returns the new clip; its content is always its own copy with fresh ids)
ArrangementClipRef addEmptyClip (ProjectModel&, const TrackRef&, double start, double lengthBeats, const juce::String& name = {});
// A copy of a Session View clip. Length: the clip length; the loop flag follows the clip's.
ArrangementClipRef addSlotClip (ProjectModel&, const TrackRef&, const ClipRef& source, double start);
// A copy of an arrangement clip (Alt-drag, Cmd+V from live clips) on `dest` at `start`.
ArrangementClipRef copyClip (ProjectModel&, const ArrangementClipRef& source, const TrackRef& dest, double start);
// A copy of a clip that was copied earlier as a free-standing ACLIP tree (the clipboard of the view: source.state.createCopy()).
ArrangementClipRef pasteClip (ProjectModel&, const TrackRef& dest, const juce::ValueTree& snapshot, double start);
// Cmd+D on a clip: a copy right after it.
ArrangementClipRef duplicateAfter (ProjectModel&, const ArrangementClipRef&);
// Cmd+D on a time selection: copies what lies in [from, to) on these tracks to [to, to + (to - from)).
Clips duplicateRange (ProjectModel&, const std::vector<TrackRef>&, double from, double to);

//==============================================================================
// Moving and stretching
struct Move
{
    ArrangementClipRef clip;
    TrackRef track; // the track it ends up on (may be the one it is on)
    double start = 0.0;
};
// Moves clips, also between tracks, as one group: no clip of the group cuts another, all of them cut the others. If
// one would start before 0, the whole group shifts right by the missing amount.
void moveClips (ProjectModel&, const std::vector<Move>&);
// Drags the right edge: longer than the content repeats it (when the clip loops). Minimum kMinClipBeats.
void resizeRight (ProjectModel&, const ArrangementClipRef&, double newEnd);
// Drags the left edge: the content stays where it is in time, so the offset changes. A clip that does not loop cannot
// grow past the start of its content.
void resizeLeft (ProjectModel&, const ArrangementClipRef&, double newStart);

//==============================================================================
// Cutting and joining
// Cuts the clip at `beat` (strictly inside it). Returns the new right-hand clip, invalid if nothing was cut.
ArrangementClipRef splitAt (ProjectModel&, const ArrangementClipRef&, double beat);
// Cuts every clip on the tracks that covers one of the beats. Returns how many cuts were made.
int splitTracks (ProjectModel&, const std::vector<TrackRef>&, const std::vector<double>& beats);
// Several clips of one track become one clip from the start of the first to the end of the last. The notes (as they
// play, so repeats and offsets are written out) are moved into it, gaps stay empty. Returns the new clip; invalid if
// fewer than two clips are given.
ArrangementClipRef joinClips (ProjectModel&, const Clips&);
void deleteClips (ProjectModel&, const Clips&);

//==============================================================================
// Time
// Inserts `lengthBeats` of empty time at `atBeat` on all tracks: everything from there on moves right, a clip that
// the insert point cuts becomes two, markers move. The loop of the arrangement is left alone.
void insertTime (ProjectModel&, double atBeat, double lengthBeats);
// Removes [fromBeat, toBeat) on all tracks: the content there is deleted, everything after it moves left, markers
// inside are deleted and later ones move.
void removeTime (ProjectModel&, double fromBeat, double toBeat);

//==============================================================================
// Session View
// Puts a copy of the clip, as it plays in the arrangement, into the first empty slot of its track at or after
// `fromScene`; a scene is added at the end if there is none. Returns the scene index (-1 on failure).
int sendToSession (ProjectModel&, const ArrangementClipRef&, int fromScene);

//==============================================================================
// Markers
MarkerRef addMarker (ProjectModel&, double beat, const juce::String& name = {});
void renameMarker (ProjectModel&, const MarkerRef&, const juce::String& name);
void moveMarker (ProjectModel&, const MarkerRef&, double beat);
void setMarkerColour (ProjectModel&, const MarkerRef&, int paletteIndex);
void deleteMarker (ProjectModel&, const MarkerRef&);
// The nearest marker strictly before / after `beat`, or an invalid ref.
MarkerRef previousMarker (const ProjectModel&, double beat);
MarkerRef nextMarker (const ProjectModel&, double beat);
} // namespace arrangement
