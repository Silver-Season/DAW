#pragma once

#include "../model/ProjectModel.h"
#include "AudioEngine.h"

#include "daw/engine/Capture.h"
#include "daw/engine/Recording.h"

#include <map>
#include <vector>

class AudioRecorder;

// The message-thread half of recording onto the Arrangement timeline (docs/spec/03-arrangement.md, task 3.4).
//
// The engine runs the transport (count-in, start on the launch quantisation) and logs, while a recording is armed,
// where the song position jumps and which Session View clips start and stop. This class collects that log and the
// live MIDI events of the tracks, and when the recording ends turns them into arrangement clips in ONE undo transaction:
//
//  - live notes become one clip per stretch of recording on each track that took notes, from the song position of the
//    start; with the loop of the arrangement on, each pass of the loop is a take of that clip (the last pass that has
//    notes is on top, the others sit in ACLIP/TAKES); with OVR on the notes are added to the clips they fall into;
//  - Session View launches become copies of the launched clips, from launch to stop or next launch, entered at the
//    phase the clip had in the session;
//  - punch-in / punch-out cut everything to the loop brace.
//
// Times: live events carry monotonic transport beats (the clock of Session View), the clips live on song beats. The
// anchors of the log (start, seek, loop jump) map one to the other exactly, so a note that crosses a loop jump is cut at
// the end of its pass and nothing is ever placed by guesswork.
//
// poll() is called from SessionRecorder's 30 Hz poll (SessionRecorder owns this class and passes the live events);
// the tests drive it with beginRun / feedEngineEvent / feedInput / finishRun and no engine at all.
// What the record button of the transport does (docs/spec/03-arrangement.md, task 3.4). There is one record
// button for the timeline and the session, so it follows the view:
//   a recording onto the timeline runs                  -> ends it
//   a recording into slots runs                         -> ends it (stage 2)
//   Arrangement View is shown                           -> record onto the timeline
//   Session View, the transport plays                   -> record the session into the timeline (launches and live notes)
//   Session View, the transport stands still            -> stage 2: record into the first empty slot of the scene
// F9 and the item "Record to Timeline" always record onto the timeline; the record buttons of the slots and the item
// "Record into Session Slot" always record into slots.
enum class RecordButtonAction
{
    stopTimeline,
    stopSlots,
    recordTimeline,
    recordSlot
};
RecordButtonAction recordButtonAction (bool timelineRecording, bool slotRecording, bool arrangementShown, bool transportPlaying);

class ArrangementRecorder
{
public:
    ArrangementRecorder (AudioEngine&, ProjectModel&);
    // Recording of sound (docs/spec/05-audio-recording.md, task 5.4): armed tracks that record sound get a file each, which
    // becomes an audio clip (with a take per pass of the loop) when the recording is written. Without it only MIDI records.
    void setAudioRecorder (AudioRecorder* r) { sound = r; }

    //==========================================================================
    // Commands (the transport's record button, F9)
    // Arms the recording. False if the project has no MIDI track to record on or one is running already.
    bool start();
    // Ends the recording: the engine closes it and poll() writes the clips. Also cancels one that has not begun.
    bool stop();
    bool toggle() { return active ? stop() : start(); }
    // Armed, counting in, or recording.
    bool isActive() const { return active; }
    // Armed but not yet recording (the count-in runs, or the launch quantisation is being waited for).
    bool isWaiting() const { return active && ! running; }
    // Forgets a recording in flight (the project was replaced).
    void reset();

    //==========================================================================
    // Polling. SessionRecorder reads the state first, then drains the live events (feedInput), then calls process().
    struct Snapshot
    {
        int state = AudioEngine::arrangementIdle;
        double start = 0.0, end = TrackRuntime::kNoEnd;
        double songPosition = 0.0, monoPosition = 0.0;
    };
    Snapshot snapshot() const;
    void process (const Snapshot&);
    void feedInput (const daw::engine::InputEvent&);

    //==========================================================================
    // What the lanes draw while the recording runs: the clip that grows on the track.
    struct Progress
    {
        bool waiting = false;
        double startBeat = 0.0;  // song beats
        double endBeat = 0.0;    // where the recording is now (end of the newest pass)
        int pass = 1;            // the pass of the loop that runs, 1-based
        int numPasses = 1;
        std::vector<daw::engine::Note> notes; // of the newest pass, beats from startBeat; held notes run to endBeat
    };
    bool getProgress (int64_t trackId, Progress& out) const;

    //==========================================================================
    // The recording as a log, for the tests and for process()
    // Begins a recording whose first sample is at monotonic beat `startMono`.
    void beginRun (double startMono);
    void feedEngineEvent (const EngineEvent&);
    struct Summary
    {
        int numClips = 0;      // arrangement clips written
        int numTakes = 0;      // takes kept under them
        int numLaunches = 0;   // of the clips: copies of session clips
        bool wrote = false;
        juce::String warning;  // a recording of sound lost frames (the disk was too slow)
    };
    // Ends the recording at monotonic beat `endMono` and writes the clips (one undo transaction). Nothing is opened when
    // nothing was recorded.
    Summary finishRun (double endMono);
    Summary getLastSummary() const { return lastSummary; }

    // Called once, when a recording was written: the window shows the summary in its info bar.
    std::function<void (const Summary&)> onFinished;

private:
    struct Run
    {
        double startMono = 0.0, endMono = TrackRuntime::kNoEnd;
        std::vector<EngineEvent> events;
        std::map<int64_t, std::vector<daw::engine::InputEvent>> input;
        std::vector<int64_t> soundTracks; // tracks that record sound into a file
    };

    Summary commit();
    void cancel();
    bool soundReady() const;
    void finishAndReport();
    bool isSoundTrack (int64_t trackId) const;

    AudioEngine& engine;
    ProjectModel& model;
    Run run;
    bool active = false;     // between start() and the end of the commit
    bool running = false;    // the engine has begun recording
    bool sawState = false;   // the audio thread has taken the command
    bool stopSent = false;
    bool collecting = false; // events are being kept (between beginRun and the end of finishRun)
    juce::uint32 createdMs = 0;
    AudioRecorder* sound = nullptr;
    bool awaitingSound = false; // the engine finished; the files of the sound tracks are still being completed
    juce::uint32 awaitingSinceMs = 0;
    double songNow = 0.0, monoNow = 0.0;
    Summary lastSummary;
};
