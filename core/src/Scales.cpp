#include "daw/music/Scales.h"

namespace daw::music
{
namespace
{
// A scale from the semitone offsets of its notes.
Scale make (const char* key, const char* name, std::initializer_list<int> offsets)
{
    Scale s { key, name, {} };
    for (int o : offsets)
        s.pitches[(size_t) o] = true;
    return s;
}
} // namespace

const std::vector<Scale>& allScales()
{
    static const std::vector<Scale> list {
        make ("major", "мажор", { 0, 2, 4, 5, 7, 9, 11 }),
        make ("minor", "минор", { 0, 2, 3, 5, 7, 8, 10 }),
        make ("dorian", "дорийский", { 0, 2, 3, 5, 7, 9, 10 }),
        make ("phrygian", "фригийский", { 0, 1, 3, 5, 7, 8, 10 }),
        make ("lydian", "лидийский", { 0, 2, 4, 6, 7, 9, 11 }),
        make ("mixolydian", "миксолидийский", { 0, 2, 4, 5, 7, 9, 10 }),
        make ("locrian", "локрийский", { 0, 1, 3, 5, 6, 8, 10 }),
        make ("harmonicMinor", "гармонический минор", { 0, 2, 3, 5, 7, 8, 11 }),
        make ("melodicMinor", "мелодический минор", { 0, 2, 3, 5, 7, 9, 11 }),
        make ("majorPentatonic", "мажорная пентатоника", { 0, 2, 4, 7, 9 }),
        make ("minorPentatonic", "минорная пентатоника", { 0, 3, 5, 7, 10 }),
        make ("blues", "блюзовый", { 0, 3, 5, 6, 7, 10 }),
        make ("chromatic", "хроматический", { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }),
    };
    return list;
}

const Scale* findScale (const char* key)
{
    if (key == nullptr || key[0] == 0)
        return nullptr;
    for (const auto& s : allScales())
    {
        const char* a = s.key;
        const char* b = key;
        while (*a != 0 && *a == *b)
        {
            ++a;
            ++b;
        }
        if (*a == 0 && *b == 0)
            return &s;
    }
    return nullptr;
}

int notesPerOctave (const Scale& scale)
{
    int n = 0;
    for (bool p : scale.pitches)
        n += p ? 1 : 0;
    return n;
}

int snapToScale (int note, int root, const Scale& scale, int direction)
{
    if (inScale (note, root, scale) || notesPerOctave (scale) == 0)
        return note;
    for (int d = 1; d < 12; ++d)
    {
        const bool upHit = inScale (note + d, root, scale), downHit = inScale (note - d, root, scale);
        if (direction > 0 && upHit)
            return note + d;
        if (direction < 0 && downHit)
            return note - d;
        if (direction == 0 && (upHit || downHit))
            return downHit ? note - d : note + d; // a tie goes down
    }
    return note;
}

int degreeToNote (int degree, int root, const Scale& scale, int baseOctaveNote)
{
    const int count = notesPerOctave (scale);
    if (count == 0)
        return baseOctaveNote;
    const int tonic = baseOctaveNote - pitchClass (baseOctaveNote, root);
    const int octave = degree >= 0 ? degree / count : -((-degree + count - 1) / count);
    int index = degree - octave * count; // 0 .. count-1
    int note = tonic + 12 * octave;
    for (int pc = 0; pc < 12; ++pc)
        if (scale.pitches[(size_t) pc])
        {
            if (index == 0)
                return note + pc;
            --index;
        }
    return note;
}

} // namespace daw::music
