#pragma once

#include "daw/engine/Clip.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace project
{
struct ImportedClip
{
    juce::String name;
    std::vector<daw::engine::Note> notes;
    double lengthBeats = 4.0;
};

// A clip from the first track of a .mid file that has notes (docs/spec/01-layout-project.md, task 1.5).
// The length is rounded up to whole bars of `beatsPerBar`. False with `error` if the file cannot be read,
// has no notes, or uses SMPTE timing.
bool importMidiFile (const juce::File&, double beatsPerBar, ImportedClip& result, juce::String& error);
bool importMidiStream (juce::InputStream&, const juce::String& name, double beatsPerBar, ImportedClip& result, juce::String& error);
} // namespace project
