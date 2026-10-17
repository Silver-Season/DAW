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

//==============================================================================
bool isLaneEvent (const daw::engine::ControllerEvent& e, ControllerLane lane)
{
    switch (lane)
    {
        case ControllerLane::modwheel: return (e.status & 0xF0) == 0xB0 && e.data1 == 1;
        case ControllerLane::pitchBend: return (e.status & 0xF0) == 0xE0;
        case ControllerLane::sustain: return (e.status & 0xF0) == 0xB0 && e.data1 == 64;
    }
    return false;
}

double laneValue (const daw::engine::ControllerEvent& e, ControllerLane lane)
{
    switch (lane)
    {
        case ControllerLane::modwheel: return (double) e.data2 / 127.0;
        case ControllerLane::pitchBend: return (double) ((e.data2 << 7) | e.data1) / 16383.0;
        case ControllerLane::sustain: return e.data2 >= 64 ? 1.0 : 0.0;
    }
    return 0.0;
}

daw::engine::ControllerEvent laneEvent (ControllerLane lane, double timeBeats, double value01)
{
    daw::engine::ControllerEvent e;
    e.time = timeBeats;
    const double v = juce::jlimit (0.0, 1.0, value01);
    switch (lane)
    {
        case ControllerLane::modwheel:
            e.status = 0xB0;
            e.data1 = 1;
            e.data2 = (uint8_t) juce::roundToInt (v * 127.0);
            break;
        case ControllerLane::pitchBend:
        {
            // a little magnet at the centre, so that the wheel can be put back to rest with the mouse
            const int bend = std::abs (v - 0.5) < 0.03 ? 8192 : juce::roundToInt (v * 16383.0);
            e.status = 0xE0;
            e.data1 = (uint8_t) (bend & 0x7F);
            e.data2 = (uint8_t) (bend >> 7);
            break;
        }
        case ControllerLane::sustain:
            e.status = 0xB0;
            e.data1 = 64;
            e.data2 = v >= 0.5 ? 127 : 0;
            break;
    }
    return e;
}

std::vector<daw::engine::ControllerEvent> drawControllerStroke (const std::vector<daw::engine::ControllerEvent>& original, ControllerLane lane,
                                                                const std::vector<std::pair<double, double>>& stroke, double clipLengthBeats)
{
    if (stroke.empty())
        return original;
    const auto snap = [clipLengthBeats] (double beat) {
        return juce::jlimit (0.0, juce::jmax (0.0, clipLengthBeats), std::round (beat * 32.0) / 32.0);
    };
    auto points = stroke;
    for (auto& p : points)
        p.first = snap (p.first);
    std::stable_sort (points.begin(), points.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
    const double lo = points.front().first, hi = points.back().first;

    std::vector<daw::engine::ControllerEvent> out;
    out.reserve (original.size() + points.size());
    for (const auto& e : original)
        if (! (isLaneEvent (e, lane) && e.time >= lo - 1.0e-9 && e.time <= hi + 1.0e-9))
            out.push_back (e);

    std::vector<daw::engine::ControllerEvent> drawn;
    for (const auto& [beat, value] : points)
    {
        const auto e = laneEvent (lane, beat, value);
        if (! drawn.empty() && std::abs (drawn.back().time - beat) < 1.0e-9)
            drawn.back() = e; // the same slot twice: the later point wins
        else if (drawn.empty() || drawn.back().status != e.status || drawn.back().data1 != e.data1 || drawn.back().data2 != e.data2)
            drawn.push_back (e);
    }
    out.insert (out.end(), drawn.begin(), drawn.end());
    std::stable_sort (out.begin(), out.end(), [] (const auto& a, const auto& b) { return a.time < b.time; });
    return out;
}
} // namespace clipedit
