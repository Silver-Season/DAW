#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// Automation (docs/spec/06-effects-mixing.md, task 6.7): a lane is a list of points on the song timeline. Between two
// points the value follows a curve: 0 is a straight line, positive bends it up (fast start), negative down. Before the
// first point the lane holds the first value, after the last the last one.
namespace daw::engine
{
struct AutomationPoint
{
    double beat = 0.0;
    float value = 0.0f; // normalised 0..1
    float curve = 0.0f; // -1..1, the bend of the segment that starts here
};

// Points must be sorted by beat. Returns `fallback` for an empty lane.
inline float automationValueAt (const std::vector<AutomationPoint>& points, double beat, float fallback = 0.0f)
{
    if (points.empty())
        return fallback;
    if (beat <= points.front().beat)
        return points.front().value;
    if (beat >= points.back().beat)
        return points.back().value;
    const auto it = std::upper_bound (points.begin(), points.end(), beat, [] (double b, const AutomationPoint& p) { return b < p.beat; });
    const auto& b = *it;
    const auto& a = *(it - 1);
    const double span = b.beat - a.beat;
    double t = span > 1.0e-12 ? (beat - a.beat) / span : 1.0;
    // The bend: an exponent between 1/4 and 4, so -1..1 covers slow and fast swells without a kink.
    if (std::abs (a.curve) > 1.0e-4f)
        t = std::pow (t, std::pow (4.0, (double) -a.curve));
    return (float) (a.value + (b.value - a.value) * t);
}
} // namespace daw::engine
