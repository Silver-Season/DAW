#include "RecoveryCheck.h"

#include "ProjectFile.h"

namespace project
{
bool RecoveryCheck::needsRecovery (bool lockExists, std::optional<juce::int64> autosaveTimeMs, std::optional<juce::int64> projectTimeMs)
{
    if (! lockExists || ! autosaveTimeMs.has_value())
        return false;
    return ! projectTimeMs.has_value() || *autosaveTimeMs > *projectTimeMs;
}

RecoveryCheck::Result RecoveryCheck::check (const juce::File& lockFile)
{
    Result r;
    if (! lockFile.existsAsFile())
        return r;
    const auto path = lockFile.loadFileAsString().trim();
    if (path.isEmpty())
        return r;

    r.folder = juce::File (path);
    r.autosave = autosaveFile (r.folder);
    r.projectFile = findMainFile (r.folder);

    std::optional<juce::int64> autosaveTime, projectTime;
    if (r.autosave.existsAsFile())
        autosaveTime = r.autosave.getLastModificationTime().toMilliseconds();
    if (r.projectFile.existsAsFile())
        projectTime = r.projectFile.getLastModificationTime().toMilliseconds();
    r.needed = needsRecovery (true, autosaveTime, projectTime);
    return r;
}

void RecoveryCheck::writeLock (const juce::File& lockFile, const juce::File& projectFolder)
{
    lockFile.getParentDirectory().createDirectory();
    lockFile.replaceWithText (projectFolder.getFullPathName());
}

void RecoveryCheck::removeLock (const juce::File& lockFile)
{
    lockFile.deleteFile();
}
} // namespace project
