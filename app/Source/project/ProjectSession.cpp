#include "ProjectSession.h"

#include "../Strings.h"
#include "../model/TrackTransfer.h"

namespace project
{
ProjectSession::ProjectSession (AudioEngine& e, ProjectModel& m, Settings& s, library::UserLibrary& l, const juce::File& music)
    : engine (e), model (m), settings (s), library (l), musicFolder (music), lock (lockFileFor (s))
{
    if (const auto env = juce::SystemStats::getEnvironmentVariable ("DAW_AUTOSAVE_SECONDS", {}); env.getIntValue() > 0)
        autosaveSeconds = env.getIntValue(); // for quality checks: waiting a minute for the autosave is slow
    startTimer (500);
}

ProjectSession::~ProjectSession()
{
    alive->store (false);
    flush();
}

juce::File ProjectSession::lockFileFor (Settings& s)
{
    return s.getPropertiesFile().getFile().getParentDirectory().getChildFile ("session.lock");
}

juce::uint64 ProjectSession::hashOf (const juce::String& text)
{
    juce::uint64 h = 1469598103934665603ULL;
    for (auto p = text.getCharPointer(); ! p.isEmpty(); ++p)
    {
        h ^= (juce::uint64) *p;
        h *= 1099511628211ULL;
    }
    return h;
}

//==============================================================================
juce::File ProjectSession::mainFile() const
{
    return named && projectFolder != juce::File() ? project::mainFile (projectFolder, name()) : juce::File();
}

juce::String ProjectSession::name() const
{
    const auto n = model.name();
    return n.isNotEmpty() ? n : (projectFolder != juce::File() ? projectNameOfFolder (projectFolder) : juce::String());
}

bool ProjectSession::isDirty() const
{
    return edited || (! baselinePending && persistedHash != 0 && persistedHash != baselineHash);
}

juce::String ProjectSession::currentText()
{
    engine.captureDeviceStates (model.root);
    return serialise (model.root);
}

void ProjectSession::submit (std::function<juce::String()> job)
{
    ++pendingWrites;
    writer.addJob ([this, flag = alive, job = std::move (job)] {
        const auto error = job();
        if (error.isNotEmpty())
            juce::MessageManager::callAsync ([this, flag, error] {
                if (flag->load() && onWriteFailed)
                    onWriteFailed (error);
            });
        --pendingWrites;
    });
}

void ProjectSession::flush()
{
    while (pendingWrites.load() > 0)
        juce::Thread::sleep (2);
}

void ProjectSession::updateLock()
{
    if (projectFolder != juce::File())
        RecoveryCheck::writeLock (lock, projectFolder);
    else
        RecoveryCheck::removeLock (lock);
}

void ProjectSession::closeCleanly()
{
    flush();
    RecoveryCheck::removeLock (lock);
}

//==============================================================================
void ProjectSession::adopt (juce::ValueTree tree, const juce::File& folder, bool isNamed, bool dirtyAfter)
{
    flush();
    hasDocument = true;
    projectFolder = folder;
    named = isNamed;
    if (tree.getProperty (ids::name).toString().isEmpty() && folder != juce::File() && isNamed)
        tree.setProperty (ids::name, projectNameOfFolder (folder), nullptr);
    model.adopt (tree);
    engine.reviveMissingDevices();
    edited = dirtyAfter;
    baselinePending = true;
    baselineAfterMs = juce::Time::getMillisecondCounter() + 1500;
    persistedHash = baselineHash = 0;
    lastVersion = 0;
    lastSaveTimeMs = 0;
    lastVersionCheckMs = 0;
    updateLock();
    if (onDocumentChanged)
        onDocumentChanged();
}

void ProjectSession::newProject()
{
    juce::ValueTree tree;
    if (const auto file = library.defaultTemplate(); file.existsAsFile())
        if (const auto loaded = loadProjectFile (file); loaded.ok())
        {
            tree = loaded.tree;
            tree.setProperty (ids::name, "", nullptr);
        }
    if (! tree.isValid())
        tree = ProjectModel::createDefault().root.createCopy();
    adopt (tree, {}, false, false);
}

void ProjectSession::adoptNewTree (const juce::ValueTree& tree)
{
    adopt (tree.createCopy(), {}, false, false);
}

juce::Result ProjectSession::openFile (const juce::File& file)
{
    auto loaded = loadProjectFile (file);
    if (! loaded.ok())
        return juce::Result::fail (loaded.error);
    adopt (loaded.tree, file.getParentDirectory(), true, false);
    // the file's own name is the project's name, wherever the folder is called
    model.root.setProperty (ids::name, file.getFileNameWithoutExtension(), nullptr);
    settings.addRecentProject (file);
    settings.save();
    return juce::Result::ok();
}

juce::Result ProjectSession::openRecovered (const RecoveryCheck::Result& r)
{
    auto loaded = loadProjectFile (r.autosave);
    if (! loaded.ok())
        return juce::Result::fail (loaded.error);
    const bool hasMain = r.projectFile.existsAsFile();
    adopt (loaded.tree, r.folder, hasMain, true);
    if (hasMain)
        model.root.setProperty (ids::name, r.projectFile.getFileNameWithoutExtension(), nullptr);
    return juce::Result::ok();
}

//==============================================================================
juce::Result ProjectSession::save()
{
    if (! named || projectFolder == juce::File())
        return juce::Result::fail ("no file");
    const auto text = currentText();
    const auto dir = projectFolder;
    const auto projectName = name();
    const auto now = juce::Time::currentTimeMillis();
    submit ([=] {
        juce::String error;
        writeSave (dir, projectName, text, now, error);
        return error;
    });
    edited = false;
    baselinePending = false;
    baselineHash = persistedHash = hashOf (text);
    lastSaveTimeMs = now;
    settings.addRecentProject (mainFile());
    settings.save();
    return juce::Result::ok();
}

juce::Result ProjectSession::saveAs (const juce::File& target)
{
    flush();
    const auto newName = projectNameOfFolder (target);
    if (newName.isEmpty())
        return juce::Result::fail (Strings::errorWriteFile (target.getFullPathName()));
    if (target.exists() && findMainFile (target).existsAsFile())
        return juce::Result::fail (Strings::errorFolderExists (target.getFullPathName()));

    juce::String error;
    const auto old = projectFolder;
    if (old != juce::File() && old.isDirectory() && old != target)
    {
        if (! copyProjectFolder (old, target, error))
            return juce::Result::fail (error);
        // the temporary home of a project that was never saved is no longer needed
        if (! named && old.getParentDirectory().getFileName() == "Unsaved")
            old.deleteRecursively();
    }
    else if (! target.createDirectory())
        return juce::Result::fail (Strings::errorWriteFile (target.getFullPathName()));

    projectFolder = target;
    named = true;
    model.root.setProperty (ids::name, newName, nullptr);
    updateLock();
    const auto result = save();
    if (onDocumentChanged)
        onDocumentChanged();
    return result;
}

juce::Result ProjectSession::saveAsDefaultTemplate()
{
    const auto text = currentText();
    juce::String error;
    auto copy = juce::ValueTree::fromXml (*juce::XmlDocument::parse (text));
    copy.setProperty (ids::name, "", nullptr);
    if (! writeAtomically (library.defaultTemplate(), serialise (copy), error))
        return juce::Result::fail (error);
    return juce::Result::ok();
}

//==============================================================================
void ProjectSession::setAutosaveSeconds (int seconds)
{
    autosaveSeconds = juce::jmax (1, seconds);
}

void ProjectSession::rebaseline()
{
    const auto text = currentText();
    baselineHash = persistedHash = hashOf (text);
    baselinePending = false;
}

void ProjectSession::timerCallback()
{
    if (baselinePending && ! edited && juce::Time::getMillisecondCounter() >= baselineAfterMs && ! engine.isAnyDeviceLoading())
        rebaseline();

    if (juce::Time::getMillisecondCounter() - lastAutosaveMs >= (juce::uint32) autosaveSeconds * 1000u)
    {
        lastAutosaveMs = juce::Time::getMillisecondCounter();
        autosaveNow();
    }
}

void ProjectSession::autosaveNow()
{
    if (! hasDocument)
        return; // nothing is open yet: the startup question about a lost session is still open, and its lock must stay
    if (baselinePending && ! edited)
        return; // still settling after a load: plugins are not back yet
    const auto text = currentText();
    const auto h = hashOf (text);
    if (h == persistedHash)
        return;
    persistedHash = h;

    if (projectFolder == juce::File())
    {
        projectFolder = newUnsavedFolder (musicFolder, juce::Time::getCurrentTime());
        updateLock();
    }
    const auto dir = projectFolder;
    const auto now = juce::Time::currentTimeMillis();
    submit ([=] {
        juce::String error;
        writeAutosave (dir, text, now, error);
        return error;
    });
}

//==============================================================================
std::vector<VersionRecord> ProjectSession::versions()
{
    flush();
    return projectFolder != juce::File() ? readVersions (projectFolder) : std::vector<VersionRecord>();
}

bool ProjectSession::setVersionLabel (int number, const juce::String& label)
{
    flush();
    return projectFolder != juce::File() && project::setVersionLabel (projectFolder, number, label);
}

juce::Result ProjectSession::restoreVersion (int number)
{
    flush();
    if (projectFolder == juce::File())
        return juce::Result::fail ("no history");
    const auto loaded = loadVersion (projectFolder, number);
    if (! loaded.ok())
        return juce::Result::fail (loaded.error);
    juce::String error;
    if (! writeRestorePoint (projectFolder, currentText(), juce::Time::currentTimeMillis(), error))
        return juce::Result::fail (error);
    lastSaveTimeMs = juce::Time::currentTimeMillis();
    const auto dir = projectFolder;
    const bool isNamed = named;
    auto tree = loaded.tree;
    adopt (tree, dir, isNamed, true); // the older state is not the saved one until the user saves it
    return juce::Result::ok();
}

std::unique_ptr<ProjectModel> ProjectSession::loadVersionModel (int number, juce::String& error)
{
    flush();
    if (projectFolder == juce::File())
    {
        error = "no history";
        return nullptr;
    }
    const auto loaded = loadVersion (projectFolder, number);
    if (! loaded.ok())
    {
        error = loaded.error;
        return nullptr;
    }
    return std::make_unique<ProjectModel> (loaded.tree);
}

juce::Result ProjectSession::takeTrackFromVersion (int number, int64_t trackId)
{
    juce::String error;
    const auto older = loadVersionModel (number, error);
    if (older == nullptr)
        return juce::Result::fail (error);
    if (! older->findTrack (trackId).isValid())
        return juce::Result::fail ("no such track");
    model.undo.beginNewTransaction (Strings::arrRec::undoTakeTrack);
    const auto added = transfer::importTrack (model, *older, trackId, "v" + juce::String (number));
    if (! added.isValid())
        return juce::Result::fail ("no such track");
    noteEdited();
    return juce::Result::ok();
}

juce::String ProjectSession::versionButtonText()
{
    const auto now = juce::Time::currentTimeMillis();
    if (projectFolder == juce::File())
        return Strings::transport::versionNone;
    if (now - lastVersionCheckMs > 2000)
    {
        lastVersionCheckMs = now;
        const auto list = readVersions (projectFolder);
        lastVersion = list.empty() ? 0 : list.back().number;
        lastSaveTimeMs = 0;
        for (const auto& v : list)
            if (v.kind != daw::project::VersionKind::autosave)
                lastSaveTimeMs = juce::jmax (lastSaveTimeMs, v.timeMs);
    }
    if (lastVersion == 0)
        return Strings::transport::versionNone;
    return Strings::transport::versionText (lastVersion, lastSaveTimeMs > 0 ? now - lastSaveTimeMs : -1);
}
} // namespace project
