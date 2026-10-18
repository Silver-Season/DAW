#pragma once

#include "../model/ProjectModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

class AudioEngine;

// The input and monitoring choices of a track (docs/spec/05-audio-recording.md, task 5.3): the text the track card shows
// and the menus it opens. An audio track picks channels of the sound card ("Input 1 · Mic", "Inputs 1/2"), the
// master ("Resampling") or nothing; a MIDI track picks its MIDI source.
namespace inputmenus
{
// "Inputs 1/2 · Guitar", "Input 3", "Resampling", "All MIDI Inputs", ...
juce::String inputLabel (AudioEngine&, const TrackRef&);
juce::String monitorLabel (const TrackRef&);

// Each choice is one undo step.
void showInputMenu (AudioEngine&, ProjectModel&, const TrackRef&, juce::Component& target);
void showMonitorMenu (ProjectModel&, const TrackRef&, juce::Component& target);
} // namespace inputmenus
