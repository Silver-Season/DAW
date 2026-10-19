#pragma once

#include "../model/ProjectModel.h"

#include "daw/project/VersionHistory.h"

#include <functional>
#include <vector>

// The project folder and its files (docs/spec/01-layout-project.md, task 1.7):
//
//   My track Project/
//   |- My track.prisma          the model as XML
//   |- Backup/autosave.prisma   the autosave
//   '- History/index.xml, v0001.prisma ...
//
// Everything here is plain file work that can run on any thread; the callers hand it ready-made text.
namespace project
{
inline const char* const kExtension = ".prisma";

//==============================================================================
// Parsing, schema versions, migration
struct LoadResult
{
    juce::ValueTree tree;
    juce::String error; // text for the user; empty on success
    bool ok() const { return tree.isValid() && error.isEmpty(); }
};

// Step N -> N+1. migrations()[0] turns a version 1 tree into version 2, and so on.
using Migration = std::function<void (juce::ValueTree&)>;
// The migrations of this build. Empty while the schema is at version 1.
const std::vector<Migration>& migrations();

// Brings `tree` from its own schemaVersion up to `target`, one step at a time. A tree newer than `target`
// or older than 1 is an error and is left alone.
bool migrate (juce::ValueTree& tree, int target, const std::vector<Migration>& steps, juce::String& error);

LoadResult parseProject (const juce::String& xmlText);
LoadResult parseProject (const juce::String& xmlText, int target, const std::vector<Migration>& steps);
LoadResult loadProjectFile (const juce::File&);

// UTF-8 XML with indentation, so that a diff of two saves is readable.
juce::String serialise (const juce::ValueTree&);

// Writes through a temporary file next to the target, so that a crash never leaves half a file.
bool writeAtomically (const juce::File& target, const juce::String& text, juce::String& error);

//==============================================================================
// Folder layout
juce::File mainFile (const juce::File& folder, const juce::String& name);
// The .prisma file in the root of a project folder, or an invalid File if there is none (never saved).
juce::File findMainFile (const juce::File& folder);
juce::File autosaveFile (const juce::File& folder);
juce::File historyFolder (const juce::File& folder);
juce::File versionFile (const juce::File& folder, int number);
// "My track Project" -> "My track"
juce::String projectNameOfFolder (const juce::File& folder);

// The project file named on a command line (a double click in Finder, "open -a DAW x.prisma", a terminal): the first
// argument that is an existing .prisma file, or a project folder that has one; an invalid File if there is none.
// Arguments may be quoted; system flags such as -psn_... or -NSDocumentRevisionsDebugMode are skipped.
juce::File projectFileFromCommandLine (const juce::String& commandLine);
// ~/Music/Prisma DAW/Unsaved/<date time> Project
juce::File newUnsavedFolder (const juce::File& musicFolder, juce::Time now);

//==============================================================================
// History
struct VersionRecord
{
    int number = 0;
    juce::int64 timeMs = 0;
    daw::project::VersionKind kind = daw::project::VersionKind::autosave;
    juce::String label;
};

juce::String kindToString (daw::project::VersionKind);
daw::project::VersionKind kindFromString (const juce::String&);

std::vector<VersionRecord> readVersions (const juce::File& folder); // ascending by number
// Writes vNNNN.prisma, adds it to index.xml and thins the history. Returns the number, or -1 with `error` set.
int addVersion (const juce::File& folder, const juce::String& xmlText, daw::project::VersionKind, juce::int64 nowMs, juce::String& error);
bool setVersionLabel (const juce::File& folder, int number, const juce::String& label);
LoadResult loadVersion (const juce::File& folder, int number);

// The three writes of the document, each ending with a new version:
//   save      <Name>.prisma + version "save"
//   autosave  Backup/autosave.prisma + version "autosave"
//   restore   version "restore" only (the state a return to an older version is about to replace)
bool writeSave (const juce::File& folder, const juce::String& name, const juce::String& xmlText, juce::int64 nowMs, juce::String& error);
bool writeAutosave (const juce::File& folder, const juce::String& xmlText, juce::int64 nowMs, juce::String& error);
bool writeRestorePoint (const juce::File& folder, const juce::String& xmlText, juce::int64 nowMs, juce::String& error);

// Copies a project folder for "Save as": everything but the autosave, the main file renamed.
bool copyProjectFolder (const juce::File& from, const juce::File& to, juce::String& error);
} // namespace project
