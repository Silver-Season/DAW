#pragma once

#include "ModelRefs.h"

#include "daw/music/Scales.h"

#include <vector>

// Edits of the notes of one clip, as the piano roll makes them. A note is addressed by its NOTE node, which
// keeps its identity while it moves, so a selection survives the sorting of the clip. None of these opens an
// undo transaction: the caller does, once per gesture or key press. They never leave the clip unsorted.
namespace clipedit
{
using Nodes = std::vector<juce::ValueTree>;

struct Scale
{
    int root = 0;
    const daw::music::Scale* scale = nullptr; // nullptr = no scale
};

Nodes allNodes (const ClipRef&);
// The nodes that are still notes of this clip.
Nodes onlyValid (const ClipRef&, const Nodes&);
// `selection` if it holds any note of the clip, else every note (Quantise and Transpose act on all notes when nothing is selected).
Nodes selectionOrAll (const ClipRef&, const Nodes& selection);

void deleteNotes (const ClipRef&, const Nodes&, juce::UndoManager*);

// Copies with the start moved by `offsetBeats`; returns the new nodes.
Nodes duplicateNotes (const ClipRef&, const Nodes&, double offsetBeats, juce::UndoManager*);
Nodes insertNotes (const ClipRef&, const std::vector<daw::engine::Note>&, juce::UndoManager*);

// Moves by a time and a pitch difference. The group is limited so that no note starts before 0 or leaves 0..127.
// Returns the pitch difference that was applied.
int shiftNotes (const ClipRef&, const Nodes&, double dBeats, int dSemitones, juce::UndoManager*);
void resizeNotes (const ClipRef&, const Nodes&, double dBeats, double minLength, juce::UndoManager*);
void setVelocity (const ClipRef&, const juce::ValueTree& node, int velocity, juce::UndoManager*);

// Quantise the given notes (see daw/engine/Quantize.h).
void quantizeNotes (const ClipRef&, const Nodes&, double grid, double strength, double swing, bool quantizeEnds, juce::UndoManager*);

// Semitones: +-1 with a scale steps along the scale (the nearest note of the scale in that direction), every
// other amount (and +-12 always) is a plain shift. Notes that would leave 0..127 stay where they are.
void transposeNotes (const ClipRef&, const Nodes&, int semitones, const Scale&, juce::UndoManager*);

// Controller lanes of the clip editor (docs/spec/02, task 2.4). Each lane shows one kind of controller event as a step line.
enum class ControllerLane
{
    modwheel,  // CC 1, 0..127
    pitchBend, // 0..16383, centre 8192
    sustain    // CC 64, off / on
};
bool isLaneEvent (const daw::engine::ControllerEvent&, ControllerLane);
// 0..1 (pitch bend: 0.5 is the centre; sustain: 0 or 1)
double laneValue (const daw::engine::ControllerEvent&, ControllerLane);
daw::engine::ControllerEvent laneEvent (ControllerLane, double timeBeats, double value01);
// A mouse stroke over the lane ((beat, value 0..1) points in the order the mouse made them) turns into events: those
// of the lane's kind between the first and the last beat of the stroke are replaced by the drawn ones, everything
// else is kept. Values that do not change are not repeated (so a long flat stroke is one event). Beats are clamped to
// the clip and snapped to 1/32 of a beat. `original` is what the clip held before the stroke began, so a stroke can
// be redrawn from scratch as the mouse moves.
std::vector<daw::engine::ControllerEvent> drawControllerStroke (const std::vector<daw::engine::ControllerEvent>& original, ControllerLane,
                                                                const std::vector<std::pair<double, double>>& stroke, double clipLengthBeats);

Nodes splitNotes (const ClipRef&, const Nodes&, double atBeat, juce::UndoManager*);
// One note per pitch, from the earliest start to the latest end of the selected notes of that pitch. Returns the joined notes.
Nodes joinNotes (const ClipRef&, const Nodes&, juce::UndoManager*);

// The end of the last note.
double lastNoteEnd (const ClipRef&);
// The clip length (loop end) rounded up to a whole number of bars so that the note ending at `endBeat` fits;
// returns the current length if it already fits.
double lengthToFit (const ClipRef&, double endBeat, double beatsPerBar);

// The scale a clip shows: its own if "own scale" is on, else the project's.
Scale effectiveScale (const ClipRef&, int projectRoot, const juce::String& projectMode);
} // namespace clipedit
