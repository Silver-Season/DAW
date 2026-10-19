#pragma once

#include "ProjectModel.h"

#include "daw/engine/Automation.h"

// Automation lanes in the model (docs/spec/06-effects-mixing.md, task 6.7):
//   TRACK/AUTOMATION/LANE target="mixer:volume" | "mixer:pan" | "device:<deviceId>:<paramIndex>" | "song:tempo"
//        └── POINT beat value (0..1) curve (-1..1)
// The points of a lane are kept sorted by beat. Every edit goes through the caller's undo transaction.
namespace automation
{
inline const juce::String volumeTarget = "mixer:volume";
inline const juce::String panTarget = "mixer:pan";
// The tempo of the song (docs/spec/07-warp.md, task 7.5), a lane of the master only. Steps are two points on one beat.
inline const juce::String tempoTarget = "song:tempo";
constexpr double kTempoMin = 40.0, kTempoMax = 240.0; // the range of the lane, 0..1
inline double tempoFromNormalised (float v) { return kTempoMin + (kTempoMax - kTempoMin) * (double) juce::jlimit (0.0f, 1.0f, v); }
inline float normalisedTempo (double bpm) { return (float) juce::jlimit (0.0, 1.0, (bpm - kTempoMin) / (kTempoMax - kTempoMin)); }
inline juce::String deviceTarget (int64_t deviceId, int paramIndex) { return "device:" + juce::String (deviceId) + ":" + juce::String (paramIndex); }
// For a device target: the device id and parameter index; false for the mixer.
bool parseDeviceTarget (const juce::String& target, int64_t& deviceId, int& paramIndex);

juce::ValueTree findLane (const TrackRef&, const juce::String& target);
juce::ValueTree getOrAddLane (ProjectModel&, const TrackRef&, const juce::String& target);
std::vector<juce::ValueTree> lanes (const TrackRef&);

std::vector<daw::engine::AutomationPoint> points (const juce::ValueTree& lane);
// Adds a point (keeps the order); returns its node.
juce::ValueTree addPoint (ProjectModel&, const juce::ValueTree& lane, const daw::engine::AutomationPoint&);
// Moves a point; the lane is re-sorted if it passed a neighbour.
void movePoint (ProjectModel&, const juce::ValueTree& point, double beat, float value);
void setCurve (ProjectModel&, const juce::ValueTree& point, float curve);
void removePoint (ProjectModel&, const juce::ValueTree& point);
void removePointsIn (ProjectModel&, const juce::ValueTree& lane, double from, double to);

// The normalised value of the mixer for a track (volume: dB mapped like the fader, pan: -1..1 -> 0..1).
float normalisedVolume (float db);
float volumeDbFromNormalised (float v);
} // namespace automation
