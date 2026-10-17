#pragma once

#include "daw/engine/Clip.h"

#include <cstdint>
#include <vector>

// Turns a stream of MIDI messages into notes and controller events (docs/spec/02-midi-recording.md, task 2.2).
// Pure C++: the recorder of the app and the capture algorithm share it.
//
// Times are whatever the caller feeds in (beats of the transport, beats of a clip, seconds): the assembler only
// subtracts them. A note-on opens a note; a note-off (or a note-on with velocity 0) closes the EARLIEST open note
// of the same pitch; notes still open when the recording ends are closed by closeAll().
namespace daw::engine
{

class NoteAssembler
{
public:
    struct OpenNote
    {
        double start = 0.0;
        int pitch = 60;
        int velocity = 100;
    };

    // The shortest note the assembler makes; a note-off at the very instant of its note-on still gives a note.
    static constexpr double kMinLength = 1.0e-3;

    void noteOn (double time, int pitch, int velocity);
    void noteOff (double time, int pitch);
    // A raw channel message of 1..3 bytes. Notes go to noteOn / noteOff; the controllers worth recording
    // (modulation wheel CC1, sustain CC64, pitch bend, channel pressure) are collected; everything else is ignored.
    void feed (double time, const uint8_t* bytes, int size);
    // Closes every open note at `endTime` (at least kMinLength long).
    void closeAll (double endTime);
    void clear();

    // Finished notes, in the order they were closed.
    const std::vector<Note>& notes() const noexcept { return finished; }
    const std::vector<OpenNote>& openNotes() const noexcept { return open; }
    const std::vector<ControllerEvent>& controllers() const noexcept { return controllerEvents; }

    // Moves the finished notes out (the open ones stay).
    std::vector<Note> takeNotes();
    std::vector<ControllerEvent> takeControllers();

    // True for the messages feed() records as controllers.
    static bool isRecordedController (const uint8_t* bytes, int size) noexcept;

private:
    std::vector<OpenNote> open; // in the order they were opened
    std::vector<Note> finished;
    std::vector<ControllerEvent> controllerEvents;
    // last value of each recorded controller, to skip repeats: [CC1, CC64, bend, pressure], -1 = none yet
    int lastValue[4] { -1, -1, -1, -1 };
};

} // namespace daw::engine
