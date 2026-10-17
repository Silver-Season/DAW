#pragma once

#include "daw/engine/Clip.h"

#include <algorithm>
#include <cmath>
#include <vector>

// Quantisation as pure functions (docs/spec/02-midi-recording.md, tasks 2.3 and 2.4). Positions are beats
// from the start of the clip, which sits on a bar line, so a triplet grid is counted from the bar.
namespace daw::engine
{

// Grid line number `index` (0, 1, 2 ...). Every second line is moved later by swing * grid / 2, so 50 % swing
// on eighths puts the off-beat eighths a thirty-second late, and 100 % would reach the triplet position.
inline double gridLine (long index, double grid, double swing)
{
    return (double) index * grid + ((index & 1L) != 0 ? swing * grid * 0.5 : 0.0);
}

// The closest line of the (swung) grid; two equally close lines resolve to the earlier one.
inline double nearestGridLine (double beat, double grid, double swing)
{
    const long k = (long) std::floor (beat / grid + 0.5);
    double best = gridLine (k, grid, swing);
    for (long i = k - 1; i <= k + 1; ++i)
    {
        const double line = gridLine (i, grid, swing);
        if (std::abs (line - beat) < std::abs (best - beat) - 1.0e-12)
            best = line;
    }
    return best;
}

// strength 0..1: how far the start moves towards its line. swing 0..0.75. With quantizeEnds the end of the
// note is moved the same way; otherwise the length is kept.
inline Note quantizeNote (const Note& n, double grid, double strength, double swing, bool quantizeEnds)
{
    if (grid <= 0.0)
        return n;
    Note out = n;
    out.start = n.start + strength * (nearestGridLine (n.start, grid, swing) - n.start);
    if (quantizeEnds)
    {
        const double end = n.start + n.length;
        const double newEnd = end + strength * (nearestGridLine (end, grid, swing) - end);
        out.length = newEnd - out.start > 1.0e-9 ? newEnd - out.start : grid; // a note never shrinks to nothing
    }
    return out;
}

inline std::vector<Note> quantizeNotes (const std::vector<Note>& notes, double grid, double strength, double swing, bool quantizeEnds)
{
    std::vector<Note> out;
    out.reserve (notes.size());
    for (const auto& n : notes)
        out.push_back (quantizeNote (n, grid, strength, swing, quantizeEnds));
    return out;
}

} // namespace daw::engine
