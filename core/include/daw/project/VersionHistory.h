#pragma once

#include <cstdint>
#include <vector>

// Pure logic of the project history (docs/spec/01-layout-project.md, task 1.7): which versions to keep.
// The files and the index live in the app layer; this part only decides.
namespace daw::project
{
enum class VersionKind
{
    save,     // Cmd+S: kept for ever
    autosave, // thinned out
    restore   // the state saved right before a return to an older version: kept for ever, it is the user's safety net
};

struct VersionInfo
{
    int number = 0;
    int64_t timeMs = 0; // wall clock, milliseconds since the epoch
    VersionKind kind = VersionKind::autosave;
    bool labelled = false;
};

// How many of the newest autosaves are kept one by one; older ones are kept one per hour.
inline constexpr int kRecentAutosavesKept = 50;

// Numbers of the versions to delete. Saves, restores and labelled versions are never deleted. Of the
// unlabelled autosaves the newest `kRecentAutosavesKept` stay; of the older ones the newest in every
// clock hour stays. The input need not be sorted; the result is sorted ascending.
std::vector<int> versionsToPrune (const std::vector<VersionInfo>& versions);
} // namespace daw::project
