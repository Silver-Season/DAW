#pragma once

#include "../model/ProjectModel.h"

#include <juce_audio_formats/juce_audio_formats.h>

class AudioEngine;

// Sound files entering the project (docs/spec/05-audio-recording.md, tasks 5.1 and 5.6): the pool entry, the copy into
// the project folder, and the length a new clip gets.
namespace audioimport
{
// WAV, AIFF, FLAC, MP3, Ogg (and on macOS M4A/AAC through CoreAudio): whatever the engine's format manager reads.
bool isSoundFile (const juce::File&);

struct Imported
{
    bool ok = false;
    juce::String error;
    int64_t fileId = -1;
    juce::String name;        // the file name without extension: the name of the clip
    double lengthBeats = 4.0; // the whole file, at its own tempo if known, else at the project tempo
    double fileTempo = 0.0;   // 0 = unknown
    bool rateDiffers = false; // the file's sample rate is not the engine's (it is resampled while it plays)
};

// Puts the file into the pool, inside the caller's undo transaction. A file outside the project folder is copied to
// Samples/Imported first when `copyIntoProject` (and the project has a folder). A file whose content is already in the
// pool (same hash) is not added again.
Imported importFile (ProjectModel&, AudioEngine&, const juce::File&, bool copyIntoProject);

// FNV-1a 64 of the first megabyte plus the length: cheap, and enough to recognise the same file in another folder.
juce::String hashOf (const juce::File&);

// The tempo of a loop: the acid chunk of a WAV, the tempo of an AIFF, else a number before "bpm" in the name. 0 = unknown.
double detectTempo (const juce::File&, const juce::StringPairArray& metadata);

// "Find Missing Files…": looks for every FILE that cannot be found by name and hash in `folder` (recursively) and
// points it there (one undo transaction is the caller's). Returns how many were found.
int relinkMissing (ProjectModel&, AudioEngine&, const juce::File& folder);
// The FILE ids no clip refers to any more (recordings that were undone).
juce::Array<int64_t> unusedFiles (const ProjectModel&);
} // namespace audioimport
