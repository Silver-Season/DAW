#pragma once

#include "model/ProjectModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

class AudioEngine;

// "Inspiration": parts and sounds made up by the generators of core (daw::gen) in the scale and metre of the project.
// Every action is one undo step and puts its result into the next empty slot, so nothing that exists is overwritten.
namespace inspiration
{
enum class Part
{
    drums,
    chords,
    bass,
    melody,
    arp
};

struct Result
{
    bool ok = false;
    juce::String message; // for the info bar
    int64_t trackId = -1;
    int scene = -1;
};

// A part into the selected track if it fits (a drum machine for drums, a MIDI track otherwise), else into a new track
// with a fitting instrument. `style` is one of daw::gen::styleNames().
Result makePart (ProjectModel&, AudioEngine&, Part, const juce::String& style, int64_t selectedTrack, int fromScene);
// A new Prisma sound of the kind ("bass", "pad", ...) on the selected MIDI track's Prisma, or on a new track.
Result makeSound (ProjectModel&, AudioEngine&, const juce::String& kind, int64_t selectedTrack);
// A whole idea in a new scene: drums, bass, chords and a melody that share one progression.
Result makeIdea (ProjectModel&, AudioEngine&, const juce::String& style);

// The submenu of Create; `done` reports each result (info bar, selection).
juce::PopupMenu menu (ProjectModel&, AudioEngine&, std::function<int64_t()> selectedTrack, std::function<int()> selectedScene,
                      std::function<void (const Result&)> done);
} // namespace inspiration
