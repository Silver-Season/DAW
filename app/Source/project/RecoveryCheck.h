#pragma once

#include <juce_core/juce_core.h>

#include <optional>

namespace project
{
// "The DAW was closed unexpectedly. Restore unsaved changes?" (task 1.7). While the app runs, a lock file
// holds the path of the open project folder; a normal quit deletes it. A lock left behind plus an autosave
// that is newer than the project file means that work was lost.
struct RecoveryCheck
{
    // The rule itself. Times are milliseconds; nullopt = the file does not exist.
    static bool needsRecovery (bool lockExists, std::optional<juce::int64> autosaveTimeMs, std::optional<juce::int64> projectTimeMs);

    struct Result
    {
        bool needed = false;
        juce::File folder;      // the project folder named in the lock
        juce::File autosave;    // Backup/autosave.prisma
        juce::File projectFile; // the saved .prisma, invalid when the project was never saved
    };
    static Result check (const juce::File& lockFile);

    static void writeLock (const juce::File& lockFile, const juce::File& projectFolder);
    static void removeLock (const juce::File& lockFile);
};
} // namespace project
