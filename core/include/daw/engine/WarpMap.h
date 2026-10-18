#pragma once

#include <cstdint>
#include <vector>

namespace daw::engine
{
// One warp marker (docs/spec/07-warp.md, task 7.3): this frame of the file plays at this content beat of the clip.
struct WarpMarker
{
    double frame = 0.0; // file frames at the file's own rate
    double beat = 0.0;  // content beats of the clip
};

// How the content beats of an audio clip map to the frames of its file. Between adjacent markers the mapping is
// linear; before the first and after the last marker it continues with the slope of the edge segment. With fewer than
// two markers it is the straight line `frame = origin + beat * framesPerBeat` of a clip without markers (its
// `sampleStart` and the file frames per beat at its original tempo).
//
// Built on the message thread, read on the audio thread: the lookups are allocation and lock free.
class WarpMap
{
public:
    WarpMap() = default;
    WarpMap (double origin, double framesPerBeat);
    // Markers in any order; ones that would make the map run backwards (a later beat on an earlier frame) are dropped.
    WarpMap (std::vector<WarpMarker> markers, double origin, double framesPerBeat);

    double frameAt (double beat) const noexcept;
    double beatAt (double frame) const noexcept;
    // File frames per content beat around a beat (the slope of its segment).
    double framesPerBeatAt (double beat) const noexcept;

    bool isLinear() const noexcept { return points.size() < 2; }
    const std::vector<WarpMarker>& markers() const noexcept { return points; }
    double defaultFramesPerBeat() const noexcept { return fpb; }

    // The same map with every beat multiplied by factor (the clip's content was stretched in time, x2 or /2).
    WarpMap scaledInBeats (double factor) const;

    // A hash of the mapping, for caches of rendered copies.
    uint64_t hash() const noexcept;

    // Minimum distance between two markers, in beats and in frames, so that no segment is degenerate.
    static constexpr double kMinBeats = 1.0 / 256.0, kMinFrames = 16.0;

private:
    std::vector<WarpMarker> points; // ascending in both beat and frame
    double origin = 0.0, fpb = 1.0;
};

// Splits a list of onset positions (or any boundaries) into segments [start, end) no shorter than minLength, the
// first starting at `from` and the last ending at `to` (task 7.6). Boundaries closer than minLength to the previous
// one are skipped. Used to cut a clip at its transients and to place drum machine pads.
struct Segment
{
    double start = 0.0, end = 0.0;
};
std::vector<Segment> sliceByOnsets (const std::vector<double>& onsets, double from, double to, double minLength);
// Segments of `division` beats over [from, to), aligned to multiples of `division` (the first and last may be shorter).
std::vector<Segment> sliceByGrid (double from, double to, double division);

} // namespace daw::engine
