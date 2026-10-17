#include "ClipEdit.h"

#include "daw/engine/Quantize.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace clipedit
{
Nodes allNodes (const ClipRef& clip)
{
    Nodes out;
    for (int i = 0; i < clip.numNotes(); ++i)
        out.push_back (clip.state.getChild (i));
    return out;
}

Nodes onlyValid (const ClipRef& clip, const Nodes& nodes)
{
    Nodes out;
    for (const auto& n : nodes)
        if (clip.hasNote (n))
            out.push_back (n);
    return out;
}

Nodes selectionOrAll (const ClipRef& clip, const Nodes& selection)
{
    auto valid = onlyValid (clip, selection);
    return valid.empty() ? allNodes (clip) : valid;
}

void deleteNotes (const ClipRef& clip, const Nodes& nodes, juce::UndoManager* um)
{
    for (const auto& n : nodes)
        clip.removeNote (n, um);
}

Nodes duplicateNotes (const ClipRef& clip, const Nodes& nodes, double offsetBeats, juce::UndoManager* um)
{
    Nodes out;
    for (const auto& n : onlyValid (clip, nodes))
    {
        auto note = ClipRef::readNote (n);
        note.start += offsetBeats;
        out.push_back (clip.addNote (note, um));
    }
    return out;
}

Nodes insertNotes (const ClipRef& clip, const std::vector<daw::engine::Note>& notes, juce::UndoManager* um)
{
    Nodes out;
    for (const auto& n : notes)
        out.push_back (clip.addNote (n, um));
    return out;
}

int shiftNotes (const ClipRef& clip, const Nodes& nodes, double dBeats, int dSemitones, juce::UndoManager* um)
{
    const auto valid = onlyValid (clip, nodes);
    if (valid.empty())
        return 0;
    double minStart = 1.0e9;
    int minPitch = 127, maxPitch = 0;
    for (const auto& n : valid)
    {
        const auto note = ClipRef::readNote (n);
        minStart = std::min (minStart, note.start);
        minPitch = std::min (minPitch, note.pitch);
        maxPitch = std::max (maxPitch, note.pitch);
    }
    dBeats = std::max (dBeats, -minStart);
    dSemitones = std::clamp (dSemitones, -minPitch, 127 - maxPitch);
    for (const auto& n : valid)
    {
        auto note = ClipRef::readNote (n);
        note.start += dBeats;
        note.pitch += dSemitones;
        clip.updateNote (n, note, um);
    }
    return dSemitones;
}

void resizeNotes (const ClipRef& clip, const Nodes& nodes, double dBeats, double minLength, juce::UndoManager* um)
{
    for (const auto& n : onlyValid (clip, nodes))
    {
        auto note = ClipRef::readNote (n);
        note.length = std::max (minLength, note.length + dBeats);
        clip.updateNote (n, note, um);
    }
}

void setVelocity (const ClipRef& clip, const juce::ValueTree& node, int velocity, juce::UndoManager* um)
{
    if (! clip.hasNote (node))
        return;
    auto note = ClipRef::readNote (node);
    note.velocity = std::clamp (velocity, 1, 127);
    clip.updateNote (node, note, um);
}

void quantizeNotes (const ClipRef& clip, const Nodes& nodes, double grid, double strength, double swing, bool quantizeEnds, juce::UndoManager* um)
{
    for (const auto& n : onlyValid (clip, nodes))
        clip.updateNote (n, daw::engine::quantizeNote (ClipRef::readNote (n), grid, strength, swing, quantizeEnds), um);
}

void transposeNotes (const ClipRef& clip, const Nodes& nodes, int semitones, const Scale& scale, juce::UndoManager* um)
{
    const bool stepByScale = scale.scale != nullptr && (semitones == 1 || semitones == -1);
    for (const auto& n : onlyValid (clip, nodes))
    {
        auto note = ClipRef::readNote (n);
        const int target = stepByScale ? daw::music::snapToScale (note.pitch + semitones, scale.root, *scale.scale, semitones)
                                       : note.pitch + semitones;
        if (target < 0 || target > 127)
            continue;
        note.pitch = target;
        clip.updateNote (n, note, um);
    }
}

Nodes splitNotes (const ClipRef& clip, const Nodes& nodes, double atBeat, juce::UndoManager* um)
{
    Nodes created;
    for (const auto& n : onlyValid (clip, nodes))
    {
        auto note = ClipRef::readNote (n);
        if (atBeat <= note.start + 1.0e-6 || atBeat >= note.start + note.length - 1.0e-6)
            continue;
        auto tail = note;
        tail.start = atBeat;
        tail.length = note.start + note.length - atBeat;
        note.length = atBeat - note.start;
        clip.updateNote (n, note, um);
        created.push_back (clip.addNote (tail, um));
    }
    return created;
}

Nodes joinNotes (const ClipRef& clip, const Nodes& nodes, juce::UndoManager* um)
{
    std::map<int, Nodes> byPitch;
    for (const auto& n : onlyValid (clip, nodes))
        byPitch[ClipRef::readNote (n).pitch].push_back (n);

    Nodes joined;
    for (auto& [pitch, group] : byPitch)
    {
        juce::ignoreUnused (pitch);
        if (group.size() < 2)
            continue;
        std::sort (group.begin(), group.end(), [] (const juce::ValueTree& a, const juce::ValueTree& b) {
            return ClipRef::readNote (a).start < ClipRef::readNote (b).start;
        });
        auto first = ClipRef::readNote (group.front());
        double end = first.start + first.length;
        for (const auto& n : group)
        {
            const auto note = ClipRef::readNote (n);
            end = std::max (end, note.start + note.length);
        }
        first.length = end - first.start;
        clip.updateNote (group.front(), first, um);
        for (size_t i = 1; i < group.size(); ++i)
            clip.removeNote (group[i], um);
        joined.push_back (group.front());
    }
    return joined;
}

double lastNoteEnd (const ClipRef& clip)
{
    double end = 0.0;
    for (int i = 0; i < clip.numNotes(); ++i)
    {
        const auto n = clip.note (i);
        end = std::max (end, n.start + n.length);
    }
    return end;
}

double lengthToFit (const ClipRef& clip, double endBeat, double beatsPerBar)
{
    const double current = clip.lengthBeats();
    if (endBeat <= current + 1.0e-9 || beatsPerBar <= 0.0)
        return current;
    return std::ceil (endBeat / beatsPerBar - 1.0e-9) * beatsPerBar;
}

Scale effectiveScale (const ClipRef& clip, int projectRoot, const juce::String& projectMode)
{
    const bool own = clip.isValid() && clip.ownScale();
    const int root = own ? clip.scaleRoot() : projectRoot;
    const auto mode = own ? clip.scaleMode() : projectMode;
    return { root, daw::music::findScale (mode.toRawUTF8()) };
}
} // namespace clipedit
