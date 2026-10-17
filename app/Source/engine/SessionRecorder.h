#pragma once

#include "../model/ProjectModel.h"
#include "AudioEngine.h"
#include "ArrangementRecorder.h"

#include "daw/engine/Capture.h"
#include "daw/engine/Recording.h"

#include <map>
#include <vector>

// The message-thread half of MIDI recording and of "always recording" capture (docs/spec/02-midi-recording.md,
// tasks 2.2, 2.3 and 2.5).
//
// The engine decides when a recording begins and ends (it counts in, waits for the launch quantisation and watches
// the transport); this class tells it to start and stop, collects the live events the audio thread queued, builds
// the notes and, when a recording is finished, writes the clip to the model as ONE undo transaction.
// poll() does the work: call it from a 30 Hz timer (startPolling()) or directly, as the tests do.
class SessionRecorder final : private juce::Timer
{
public:
    SessionRecorder (AudioEngine&, ProjectModel&);
    ~SessionRecorder() override;

    // Recording onto the Arrangement timeline (task 3.4). It shares the live events this class drains from the engine.
    ArrangementRecorder& arrangement() { return arr; }
    const ArrangementRecorder& arrangement() const { return arr; }

    void startPolling() { startTimerHz (30); }
    void poll();
    // Forgets recordings in flight (the project was replaced: its track ids mean other tracks now).
    void reset()
    {
        recs.clear();
        arr.reset();
    }

    //==========================================================================
    // Recording into slots
    // Starts recording into the slot (a new clip, or an overdub if OVR is on and the slot's clip plays). False if the
    // track cannot record (no MIDI track, audio source, already recording).
    bool startRecording (int64_t trackId, int scene);
    // Ends the recording of the track at the next bar line (an overdub at once); cancels one that has not begun.
    bool stopRecording (int64_t trackId);
    // A click on the slot: stop if the track is recording, else start.
    bool toggleRecording (int64_t trackId, int scene);
    // The record button of the transport: if anything is recording, stops everything; else records into the first
    // empty slot from `fromScene` on every armed track (the selected track if none is armed).
    bool sessionRecord (int fromScene);

    bool isRecording (int64_t trackId, int scene) const;
    bool isTrackRecording (int64_t trackId) const { return recs.count (trackId) > 0; }
    // The scene the track records into, -1 if it does not record.
    int recordingScene (int64_t trackId) const
    {
        const auto it = recs.find (trackId);
        return it != recs.end() ? it->second.scene : -1;
    }
    bool isAnythingRecording() const { return ! recs.empty(); }

    // What the slot draws while a recording runs.
    struct Progress
    {
        bool waiting = true;        // the start beat has not come yet
        bool overdub = false;
        double lengthBeats = 0.0;   // how long the recording has run (a new recording: from the start beat)
        double startBeat = 0.0;
        std::vector<daw::engine::Note> notes; // beats from the start of the recording; open notes run to `lengthBeats`
    };
    bool getProgress (int64_t trackId, Progress& out) const;

    //==========================================================================
    // Capture ("always recording")
    struct CaptureSummary
    {
        bool ok = false;
        juce::String info; // the line for the hint bar, also set on failure
        int scene = -1;
        int bars = 0, numNotes = 0;
    };
    // Turns the last phrase played on `trackId` (or on no track) into a clip in the first empty slot from `sceneHint` on.
    CaptureSummary capture (int64_t trackId, int sceneHint);

    // A note-on of the capture buffer, for the ribbon of the transport. Newest last.
    struct NoteMark
    {
        int64_t engineSample = 0;
        int velocity = 0;
        int64_t trackId = 0;
    };
    void getRecentNoteOns (std::vector<NoteMark>& out) const;
    // Bumped whenever the capture buffer got new events, so that the ribbon only repaints when needed.
    uint32_t getCaptureVersion() const { return captureVersion; }
    const daw::engine::CaptureBuffer& getCaptureBuffer() const { return capture_; }

private:
    struct Rec
    {
        int scene = 0;
        bool sawState = false; // the audio thread has taken the command (the state left idle at least once)
        juce::uint32 createdMs = 0;
        bool overdubStarted = false;
        bool overdub = false;
        double startBeat = 0.0, endBeat = TrackRuntime::kNoEnd;
        bool waiting = true;
        std::vector<daw::engine::InputEvent> events; // this track's events since the command, not yet used up
        size_t fed = 0;
        daw::engine::NoteAssembler display; // new recording: notes so far, for drawing
        // overdub
        std::vector<daw::engine::Note> pendingNotes;               // finished, in beats of the transport
        std::vector<daw::engine::ControllerEvent> pendingControllers;
        daw::engine::NoteAssembler assembler;
        long lastPass = 0;
    };

    void timerCallback() override { poll(); }
    void drainEvents();
    bool stepNew (int64_t trackId, Rec&, TrackRuntime&, int state);
    bool stepOverdub (int64_t trackId, Rec&, TrackRuntime&, int state);
    void commitNew (int64_t trackId, Rec&);
    void flushOverdub (Rec&, const ClipRef&, const daw::engine::MidiClip& shape, double playStart, bool all, long beforePass);
    static long passOf (const daw::engine::MidiClip&, double elapsed);
    daw::engine::MidiClip shapeOf (const ClipRef&) const;

    AudioEngine& engine;
    ProjectModel& model;
    ArrangementRecorder arr;
    std::map<int64_t, Rec> recs; // by track
    daw::engine::CaptureBuffer capture_;
    std::vector<daw::engine::InputEvent> scratch;
    uint32_t captureVersion = 0;
    double positionAtPoll = 0.0;
};
