#include "daw/engine/WarpMap.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace daw::engine
{
WarpMap::WarpMap (double o, double framesPerBeat) : origin (o), fpb (framesPerBeat > 0.0 ? framesPerBeat : 1.0) {}

WarpMap::WarpMap (std::vector<WarpMarker> markers, double o, double framesPerBeat) : WarpMap (o, framesPerBeat)
{
    std::sort (markers.begin(), markers.end(), [] (const WarpMarker& a, const WarpMarker& b) { return a.beat < b.beat; });
    for (const auto& m : markers)
    {
        if (! std::isfinite (m.beat) || ! std::isfinite (m.frame))
            continue;
        if (! points.empty() && (m.beat - points.back().beat < kMinBeats || m.frame - points.back().frame < kMinFrames))
            continue;
        points.push_back (m);
    }
    if (points.size() < 2)
        points.clear();
}

namespace
{
    // Index of the segment [i, i + 1] that holds `x` (clamped to the edge segments), by binary search on `key`.
    template <typename Key>
    size_t segmentOf (const std::vector<WarpMarker>& p, double x, Key key) noexcept
    {
        size_t lo = 0, hi = p.size() - 1; // p.size() >= 2
        while (hi - lo > 1)
        {
            const size_t mid = (lo + hi) / 2;
            (key (p[mid]) <= x ? lo : hi) = mid;
        }
        return lo;
    }
} // namespace

double WarpMap::frameAt (double beat) const noexcept
{
    if (points.empty())
        return origin + beat * fpb;
    const size_t i = segmentOf (points, beat, [] (const WarpMarker& m) { return m.beat; });
    const auto &a = points[i], &b = points[i + 1];
    return a.frame + (beat - a.beat) * (b.frame - a.frame) / (b.beat - a.beat);
}

double WarpMap::beatAt (double frame) const noexcept
{
    if (points.empty())
        return (frame - origin) / fpb;
    const size_t i = segmentOf (points, frame, [] (const WarpMarker& m) { return m.frame; });
    const auto &a = points[i], &b = points[i + 1];
    return a.beat + (frame - a.frame) * (b.beat - a.beat) / (b.frame - a.frame);
}

double WarpMap::framesPerBeatAt (double beat) const noexcept
{
    if (points.empty())
        return fpb;
    const size_t i = segmentOf (points, beat, [] (const WarpMarker& m) { return m.beat; });
    return (points[i + 1].frame - points[i].frame) / (points[i + 1].beat - points[i].beat);
}

WarpMap WarpMap::scaledInBeats (double factor) const
{
    factor = factor > 0.0 ? factor : 1.0;
    auto scaled = points;
    for (auto& m : scaled)
        m.beat *= factor;
    return WarpMap (std::move (scaled), origin, fpb / factor);
}

uint64_t WarpMap::hash() const noexcept
{
    uint64_t h = 1469598103934665603ULL;
    const auto add = [&h] (double v) {
        h ^= std::bit_cast<uint64_t> (v);
        h *= 1099511628211ULL;
    };
    add (origin);
    add (fpb);
    for (const auto& m : points)
    {
        add (m.frame);
        add (m.beat);
    }
    return h;
}

std::vector<Segment> sliceByOnsets (const std::vector<double>& onsets, double from, double to, double minLength)
{
    std::vector<Segment> out;
    if (! (to > from))
        return out;
    minLength = std::max (0.0, minLength);
    auto sorted = onsets;
    std::sort (sorted.begin(), sorted.end());
    double start = from;
    for (const double o : sorted)
    {
        if (o <= start + minLength || o >= to - minLength)
            continue;
        out.push_back ({ start, o });
        start = o;
    }
    out.push_back ({ start, to });
    return out;
}

std::vector<Segment> sliceByGrid (double from, double to, double division)
{
    std::vector<Segment> out;
    if (! (to > from) || ! (division > 0.0))
        return out;
    double start = from;
    for (double b = (std::floor (from / division + 1.0e-9) + 1.0) * division; b < to - 1.0e-9; b += division)
    {
        out.push_back ({ start, b });
        start = b;
    }
    out.push_back ({ start, to });
    return out;
}
} // namespace daw::engine
