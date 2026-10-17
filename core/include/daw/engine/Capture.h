#pragma once

#include "daw/engine/Clip.h"

#include <cstdint>
#include <vector>

// The "always recording" MIDI buffer and the capture algorithm (docs/spec/02-midi-recording.md, task 2.5).
// Pure C++: the engine's live-input queue, the recorder and the tests all use these types.
namespace daw::engine
{

// One live MIDI message as the audio thread saw it. POD, so it travels through a lock-free queue unchanged.
struct InputEvent
{
    int64_t engineSample = 0; // samples since the engine started, plus the offset in the block (not compensated)
    double beat = 0.0;        // transport beat of the event, corrected for the output latency (see AudioEngine)
    bool playing = false;     // the transport was running
    int64_t trackId = 0;      // the track the event went to, 0 = none
    uint8_t bytes[3] {};
    uint8_t size = 0;

    bool isNoteOn() const noexcept { return size == 3 && (bytes[0] & 0xF0) == 0x90 && bytes[2] > 0; }
    bool isNoteOff() const noexcept { return size == 3 && ((bytes[0] & 0xF0) == 0x80 || ((bytes[0] & 0xF0) == 0x90 && bytes[2] == 0)); }
};

// Ring buffer of the last events. Oldest events are dropped when it is full or when they are older than the window.
class CaptureBuffer
{
public:
    static constexpr size_t kCapacity = 200000;
    static constexpr double kWindowSeconds = 600.0; // "the last 10 minutes"

    CaptureBuffer() : storage (kCapacity) {}

    void push (const InputEvent&);
    // Drops events with engineSample < the given sample.
    void evictOlderThan (int64_t engineSample);
    void clear() noexcept { head = count = 0; }

    size_t size() const noexcept { return count; }
    // 0 = the oldest event.
    const InputEvent& operator[] (size_t index) const noexcept { return storage[(head + index) % kCapacity]; }

    std::vector<InputEvent> snapshot() const;

private:
    std::vector<InputEvent> storage;
    size_t head = 0, count = 0;
};

struct TempoEstimate
{
    double bpm = 120.0;
    bool confident = false;
};

// Tempo of a phrase from the times (seconds) of its note-ons: for every candidate from 60 to 200 BPM it counts how
// well the intervals between note-ons fall on a 1/16 grid (30 ms tolerance), takes the best and refines it by a
// least-squares fit of the onsets to the grid. Without enough evidence it returns `currentBpm` and confident = false.
TempoEstimate estimateTempo (const std::vector<double>& onsetSeconds, double currentBpm);

struct CaptureResult
{
    bool ok = false;
    std::vector<Note> notes;                  // beats from the start of the clip, sorted by start
    std::vector<ControllerEvent> controllers; // sorted by time
    double lengthBeats = 0.0;                 // a power-of-two number of bars
    double bpm = 120.0;                       // the tempo the beats are measured in
    bool tempoFromPhrase = false;             // transport stopped and estimateTempo was confident
    bool transportWasPlaying = false;
    int numNotes = 0;
    int numBars = 0;
};

// Finds the last phrase played on `targetTrackId` (or, if nothing went to that track, played into no track) and turns it
// into a clip. `events` are in time order, e.g. CaptureBuffer::snapshot(). See the spec for the rules.
CaptureResult capturePhrase (const std::vector<InputEvent>& events, int64_t targetTrackId, int64_t nowEngineSample, double sampleRate,
                             double beatsPerBar, double projectBpm);

} // namespace daw::engine
