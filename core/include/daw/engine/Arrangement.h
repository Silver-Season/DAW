#pragma once

#include "daw/engine/Clip.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace daw::engine
{

// One clip of a track's arrangement, as the audio thread plays it (docs/spec/03-arrangement.md, tasks 3.1 and 3.2).
// It occupies the song beats [startBeat, endBeat). At startBeat the content of `clip` is entered at `offsetBeats`
// (beats from the start of the clip's content); with `loop` it then repeats its loop brace for as long as the
// arrangement clip lasts, otherwise it plays one pass and falls silent. Notes still sounding at endBeat are cut off.
struct ArrangementClipPlayback
{
    double startBeat = 0.0, endBeat = 0.0;
    double offsetBeats = 0.0;
    bool loop = true;
    std::shared_ptr<const MidiClip> clip;
};

// Sorted by startBeat and not overlapping (the model guarantees both).
using ArrangementClips = std::vector<ArrangementClipPlayback>;

// Renders the arrangement clips for the song window [blockStartBeat, blockEndBeat) into MIDI events whose offsets are
// relative to the window start. Stateless: no chase after a jump and no tracking of held notes, so it suits an
// offline render that goes from the start without jumps (and the tests). Notes still sounding at the end of an
// arrangement clip get their note-off there. Live playback uses ArrangementPlayer.
void renderArrangement (const ArrangementClips& clips, double blockStartBeat, double blockEndBeat, double samplesPerBeat, int numSamples,
                        std::vector<MidiEvent>& out);

// An offset into a clip's content brought into its canonical range: with `loop`, into [0, endBeat) (an offset past the
// end counts the repeats of the loop brace, a negative one goes back through them); without, simply not below 0.
// Playing from the result sounds exactly like playing from the original offset.
double normaliseOffset (const MidiClip& clip, double offsetBeats, bool loop);

// What an arrangement clip plays during its first `lengthBeats`, as the content of one clip: note and controller
// times are relative to the start of the arrangement clip, notes are cut at the end of each pass and at lengthBeats.
// This is what "Join" turns several arrangement clips into.
struct FlattenedContent
{
    std::vector<Note> notes;
    std::vector<ControllerEvent> controllers;
};
FlattenedContent flattenArrangementClip (const MidiClip& clip, double offsetBeats, bool loop, double lengthBeats);

// Plays the arrangement of one track block by block. It notices when the song position does not continue where the
// last window ended (a seek, a loop jump, a start in the middle, the return from Session View) and then silences
// what sounds and "chases": notes that began before the new position and end after it get a note-on at the first
// sample, so a long pad is still there after a jump. Allocation free as long as `out` has capacity.
class ArrangementPlayer
{
public:
    // Renders one window of the song. `windowOffset`/`windowSamples` place it inside the audio block; the events get
    // offsets relative to the block start.
    void process (const ArrangementClips& clips, double windowStartBeat, double windowEndBeat, double samplesPerBeat, int windowOffset,
                  int windowSamples, std::vector<MidiEvent>& out);

    // Note-offs (and a reset of sustain and pitch bend if a clip moved them) for everything that sounds.
    void releaseAll (int offset, std::vector<MidiEvent>& out);
    // The next window counts as a jump: held notes are released and the notes under the position are chased. Call it
    // when the track's clips changed or when the track returns to the arrangement.
    void invalidate() noexcept { expectedBeat = std::numeric_limits<double>::quiet_NaN(); }
    // Forgets what is held without sending anything (the engine has silenced the instrument by other means).
    void reset() noexcept;
    bool hasHeldNotes() const noexcept;

private:
    void track (const std::vector<MidiEvent>& events, size_t from);

    double expectedBeat = std::numeric_limits<double>::quiet_NaN();
    bool resetControllers = false;
    std::vector<uint8_t> held = std::vector<uint8_t> (128, 0);
};

} // namespace daw::engine
