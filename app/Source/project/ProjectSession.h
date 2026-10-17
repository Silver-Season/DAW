#pragma once

#include "ProjectFile.h"
#include "RecoveryCheck.h"

#include "../engine/AudioEngine.h"
#include "../library/UserLibrary.h"

#include <atomic>

namespace project
{
// The open document: which folder it lives in, whether it has unsaved changes, and everything that writes it
// (save, save as, autosave every 60 seconds, history, the lock file for crash recovery).
//
// The model is serialised on the message thread; the files are written on one background thread, in order.
// No dialogs here: the window asks the user and calls these.
class ProjectSession final : private juce::Timer
{
public:
    ProjectSession (AudioEngine&, ProjectModel&, Settings&, library::UserLibrary&, const juce::File& musicFolder);
    ~ProjectSession() override;

    //==========================================================================
    // State
    juce::File folder() const { return projectFolder; }         // invalid until the first autosave or save
    // The folder, made now if the project has none yet (an import or a recording needs a place for its sound files).
    juce::File ensureFolder();
    juce::File mainFile() const;                                 // the .prisma of a saved project
    bool hasFile() const { return named; }                       // saved or opened from a file
    juce::String name() const;                                   // for the window title
    bool isDirty() const;
    // Called by the window for every edit of the model. Not for the state of plugins (see isDirty()).
    void noteEdited() { edited = true; }

    std::function<void()> onDocumentChanged;                     // a new project was opened / created
    std::function<void (const juce::String&)> onWriteFailed;     // message thread

    //==========================================================================
    // Documents. All of them replace the model's content and clear the undo history.
    void newProject();                                           // from User Library/Templates/Default.prisma, else the default project
    void adoptNewTree (const juce::ValueTree&);                  // an unsaved project made elsewhere (the demo)
    juce::Result openFile (const juce::File& prismaFile);
    juce::Result openRecovered (const RecoveryCheck::Result&);   // the autosave of a project that was lost, kept dirty
    juce::Result save();                                         // fails if the project has no file yet
    // `target` is the folder to create ("My track Project"); its name decides the project's name.
    juce::Result saveAs (const juce::File& targetFolder);
    juce::Result saveAsDefaultTemplate();

    //==========================================================================
    // History
    std::vector<VersionRecord> versions();                       // waits for pending writes first
    bool setVersionLabel (int number, const juce::String& label);
    // Saves the current state as a "restore" version, then loads the chosen one.
    juce::Result restoreVersion (int number);
    // Task 3.5. Loads a version into memory as a model of its own (the engine never sees it), nullptr with `error` set if
    // it cannot be read. The model can be asked for its tracks (transfer::listTracks).
    std::unique_ptr<ProjectModel> loadVersionModel (int number, juce::String& error);
    // Adds the track `trackId` of version `number` to the open project as a new track named "<name> (v<number>)": devices,
    // session clips and arrangement clips with fresh ids, one undo transaction.
    juce::Result takeTrackFromVersion (int number, int64_t trackId);
    // "v14 · now", "v14 · 5 min"
    juce::String versionButtonText();

    //==========================================================================
    // Autosave and the lock
    void setAutosaveSeconds (int seconds);
    void autosaveNow();                                          // what the timer does; tests call it
    // Waits until everything handed to the writer is on disk.
    void flush();
    // Normal quit: writes nothing, removes the lock.
    void closeCleanly();
    juce::File lockFile() const { return lock; }
    bool isLoading() const { return engine.isAnyDeviceLoading(); }

    // The lock follows the project; public for the startup code that checks for recovery.
    static juce::File lockFileFor (Settings&);

private:
    void timerCallback() override;
    void adopt (juce::ValueTree tree, const juce::File& folder, bool isNamed, bool dirtyAfter);
    void submit (std::function<juce::String()> job);
    juce::String currentText();
    void updateLock();
    void rebaseline();
    static juce::uint64 hashOf (const juce::String&);

    AudioEngine& engine;
    ProjectModel& model;
    Settings& settings;
    library::UserLibrary& library;
    juce::File musicFolder, projectFolder, lock;
    bool hasDocument = false; // set by the first adopt
    bool named = false;
    bool edited = false;
    bool baselinePending = false;
    juce::uint32 baselineAfterMs = 0;
    juce::uint64 baselineHash = 0, persistedHash = 0;
    int autosaveSeconds = 60;
    juce::uint32 lastAutosaveMs = juce::Time::getMillisecondCounter();

    juce::ThreadPool writer { 1 };
    std::atomic<int> pendingWrites { 0 };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    juce::int64 lastVersionCheckMs = 0;
    int lastVersion = 0;
    juce::int64 lastSaveTimeMs = 0;
};
} // namespace project
