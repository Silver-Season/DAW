#include "Automation.h"

namespace automation
{
bool parseDeviceTarget (const juce::String& target, int64_t& deviceId, int& paramIndex)
{
    if (! target.startsWith ("device:"))
        return false;
    const auto rest = target.fromFirstOccurrenceOf ("device:", false, false);
    deviceId = rest.upToFirstOccurrenceOf (":", false, false).getLargeIntValue();
    paramIndex = rest.fromFirstOccurrenceOf (":", false, false).getIntValue();
    return true;
}

juce::ValueTree findLane (const TrackRef& track, const juce::String& target)
{
    const auto node = track.state.getChildWithName (ids::AUTOMATION);
    for (const auto& lane : node)
        if (lane.getProperty (ids::target).toString() == target)
            return lane;
    return {};
}

juce::ValueTree getOrAddLane (ProjectModel& model, const TrackRef& track, const juce::String& target)
{
    if (auto lane = findLane (track, target); lane.isValid())
        return lane;
    auto node = track.state.getChildWithName (ids::AUTOMATION);
    if (! node.isValid())
    {
        node = juce::ValueTree (ids::AUTOMATION);
        track.state.appendChild (node, &model.undo);
    }
    juce::ValueTree lane (ids::LANE);
    lane.setProperty (ids::target, target, nullptr);
    node.appendChild (lane, &model.undo);
    return lane;
}

std::vector<juce::ValueTree> lanes (const TrackRef& track)
{
    std::vector<juce::ValueTree> out;
    for (const auto& lane : track.state.getChildWithName (ids::AUTOMATION))
        out.push_back (lane);
    return out;
}

std::vector<daw::engine::AutomationPoint> points (const juce::ValueTree& lane)
{
    std::vector<daw::engine::AutomationPoint> out;
    for (const auto& p : lane)
        out.push_back ({ (double) p.getProperty (ids::beat, 0.0), (float) (double) p.getProperty (ids::value, 0.0),
                         (float) (double) p.getProperty (ids::curve, 0.0) });
    return out;
}

namespace
{
int insertIndexFor (const juce::ValueTree& lane, double beat, const juce::ValueTree& skip = {})
{
    int index = 0;
    for (const auto& p : lane)
    {
        if (p == skip)
            continue;
        if ((double) p.getProperty (ids::beat, 0.0) > beat)
            break;
        ++index;
    }
    return index;
}
} // namespace

juce::ValueTree addPoint (ProjectModel& model, const juce::ValueTree& lane, const daw::engine::AutomationPoint& point)
{
    juce::ValueTree p (ids::POINT);
    p.setProperty (ids::beat, juce::jmax (0.0, point.beat), nullptr);
    p.setProperty (ids::value, juce::jlimit (0.0f, 1.0f, point.value), nullptr);
    if (std::abs (point.curve) > 1.0e-4f)
        p.setProperty (ids::curve, juce::jlimit (-1.0f, 1.0f, point.curve), nullptr);
    auto l = lane;
    l.addChild (p, insertIndexFor (lane, point.beat), &model.undo);
    return p;
}

void movePoint (ProjectModel& model, const juce::ValueTree& point, double beat, float value)
{
    auto p = point;
    auto lane = p.getParent();
    beat = juce::jmax (0.0, beat);
    p.setProperty (ids::beat, beat, &model.undo);
    p.setProperty (ids::value, juce::jlimit (0.0f, 1.0f, value), &model.undo);
    const int now = lane.indexOf (p), wanted = insertIndexFor (lane, beat, p);
    if (now != wanted)
        lane.moveChild (now, wanted, &model.undo);
}

void setCurve (ProjectModel& model, const juce::ValueTree& point, float curve)
{
    auto p = point;
    p.setProperty (ids::curve, juce::jlimit (-1.0f, 1.0f, curve), &model.undo);
}

void removePoint (ProjectModel& model, const juce::ValueTree& point)
{
    auto lane = point.getParent();
    lane.removeChild (point, &model.undo);
}

void removePointsIn (ProjectModel& model, const juce::ValueTree& lane, double from, double to)
{
    auto l = lane;
    for (int i = l.getNumChildren(); --i >= 0;)
        if (const double b = l.getChild (i).getProperty (ids::beat, 0.0); b >= from && b < to)
            l.removeChild (i, &model.undo);
}

float normalisedVolume (float db) { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f); }
float volumeDbFromNormalised (float v) { return juce::jlimit (0.0f, 1.0f, v) * 66.0f - 60.0f; }
} // namespace automation
