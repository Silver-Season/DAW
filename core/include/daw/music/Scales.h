#pragma once

#include <array>
#include <cstddef>
#include <vector>

// Musical scales in one place (docs/spec/02-midi-recording.md, tasks 2.4 and 2.7): the piano roll shades the
// rows by them, the project and every clip can name one, the computer keyboard can play only their notes and
// transposition steps along them.
namespace daw::music
{

struct Scale
{
    const char* key;    // stored in the project ("" = no scale)
    const char* nameRu; // shown in the menus
    std::array<bool, 12> pitches; // pitch classes above the root that belong to the scale; [0] is the root itself
};

// All scales in menu order; the first one is the major scale, the last one the chromatic scale.
const std::vector<Scale>& allScales();

// The scale with this key, or nullptr for "" and for unknown keys (= scale off).
const Scale* findScale (const char* key);

inline int pitchClass (int note, int root)
{
    return (((note - root) % 12) + 12) % 12;
}

inline bool inScale (int note, int root, const Scale& scale)
{
    return scale.pitches[(size_t) pitchClass (note, root)];
}

// The nearest note of the scale. direction 0: the closest one (a tie goes down), > 0: this note or the next
// one above, < 0: this note or the next one below. The result is not limited to 0..127.
int snapToScale (int note, int root, const Scale& scale, int direction);

// The note on the given scale degree. Degree 0 is the root in the octave of `baseOctaveNote` (the highest
// note with the root's pitch class that is not above it); degrees count up through the scale and past its
// octave, and negative degrees go down.
int degreeToNote (int degree, int root, const Scale& scale, int baseOctaveNote);

// The number of notes in one octave of the scale.
int notesPerOctave (const Scale& scale);

} // namespace daw::music
