#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class ProjectModel;

namespace ui
{
// The position field of the transport. Bar, beat and sixteenth while the song plays; during a count-in the beats
// are negative and the field shows what is left as "−1. 3" (a real minus sign, U+2212): the bar before the
// recording starts and the beat in it.
juce::String formatPosition (double beats, double beatsPerBar);

// The menus of the right mouse button on the metronome and on the record button (docs/spec/02, tasks 2.1 and 2.3).
// The items write straight to the project's transport preferences; they are not undoable edits of the music.
juce::PopupMenu makeMetronomeMenu (ProjectModel&);
juce::PopupMenu makeRecordMenu (ProjectModel&);

// The values of the submenus, so that the tests and the menus agree.
const std::vector<int>& metronomeVolumesDb(); // 0, -3, -6, -12, -18, -24, -60 (off)
const std::vector<int>& countInChoices();     // 0, 1, 2, 4 bars
const std::vector<int>& fixedLengthChoices(); // 0 (off), 1, 2, 4, 8, 16 bars
struct GridChoice
{
    juce::String label;
    double beats; // 0 = off
};
const std::vector<GridChoice>& recordQuantiseChoices();
} // namespace ui
