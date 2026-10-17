#include "daw/engine/Arrangement.h"

#include <algorithm>
#include <cmath>

namespace daw::engine
{

namespace
{
    int toOffset (double beat, double originBeat, double samplesPerBeat, int numSamples)
    {
        const int off = (int) std::llround ((beat - originBeat) * samplesPerBeat);
        return std::clamp (off, 0, std::max (0, numSamples - 1));
    }

    // Where in the clip's content the arrangement clip begins. An offset beyond the end of the content lands in the
    // loop brace (the content has run through once and repeats); without looping there is nothing left to play.
    bool entryPoint (const MidiClip& c, double offsetBeats, bool loop, double& entry)
    {
        const double end = c.endBeat(), offset = std::max (0.0, offsetBeats);
        if (offset < end)
        {
            entry = offset;
            return true;
        }
        if (! loop)
            return false;
        entry = c.braceStart() + std::fmod (offset - end, c.braceBeats());
        return true;
    }

    // The first clip that ends after `beat`. Clips are sorted and do not overlap, so the ends are sorted, too.
    ArrangementClips::const_iterator firstEndingAfter (const ArrangementClips& clips, double beat)
    {
        return std::partition_point (clips.begin(), clips.end(), [beat] (const ArrangementClipPlayback& c) { return c.endBeat <= beat; });
    }

    // Renders the part of one arrangement clip that lies in the window [wa, wb), offsets relative to wa.
    void renderOne (const ArrangementClipPlayback& a, double wa, double wb, double spb, int numSamples, std::vector<MidiEvent>& out, bool chase)
    {
        double entry = 0.0;
        if (a.clip == nullptr || ! entryPoint (*a.clip, a.offsetBeats, a.loop, entry))
            return;
        const double from = std::max (wa, a.startBeat), to = std::min (wb, a.endBeat);
        if (to <= from)
            return;
        // The chase only matters for a clip that was already under the window start.
        renderClipFrom (*a.clip, entry, a.loop, a.startBeat, from, to, wa, spb, numSamples, out, chase && a.startBeat < wa, a.endBeat <= wb);
    }
} // namespace

void renderArrangement (const ArrangementClips& clips, double blockStartBeat, double blockEndBeat, double spb, int numSamples,
                        std::vector<MidiEvent>& out)
{
    if (blockEndBeat <= blockStartBeat)
        return;
    for (auto it = firstEndingAfter (clips, blockStartBeat); it != clips.end() && it->startBeat < blockEndBeat; ++it)
        renderOne (*it, blockStartBeat, blockEndBeat, spb, numSamples, out, false);
}

double normaliseOffset (const MidiClip& clip, double offsetBeats, bool loop)
{
    if (! loop)
        return std::max (0.0, offsetBeats);
    if (offsetBeats < 0.0)
    {
        // Going back through the repeats of the brace until the offset is positive again
        const double brace = clip.braceBeats();
        offsetBeats += std::ceil (-offsetBeats / brace) * brace;
        offsetBeats = std::max (0.0, offsetBeats);
    }
    double entry = 0.0;
    return entryPoint (clip, offsetBeats, true, entry) ? entry : 0.0;
}

FlattenedContent flattenArrangementClip (const MidiClip& clip, double offsetBeats, bool loop, double lengthBeats)
{
    FlattenedContent out;
    double entry = 0.0;
    if (lengthBeats <= 0.0 || ! entryPoint (clip, offsetBeats, loop, entry))
        return out;

    const double end = clip.endBeat();
    const auto addPass = [&] (double from, double to, double passStart) {
        for (const auto& n : clip.notes)
        {
            if (n.start < from || n.start >= to)
                continue;
            const double at = passStart + (n.start - from);
            if (at >= lengthBeats)
                continue;
            Note copy = n;
            copy.start = at;
            copy.length = std::min ({ std::max (n.length, 1.0e-3), to - n.start, lengthBeats - at });
            out.notes.push_back (copy);
        }
        for (const auto& c : clip.controllers)
        {
            if (c.time < from || c.time >= to)
                continue;
            const double at = passStart + (c.time - from);
            if (at < lengthBeats)
            {
                ControllerEvent copy = c;
                copy.time = at;
                out.controllers.push_back (copy);
            }
        }
    };
    addPass (entry, end, 0.0);
    if (loop)
        for (double passStart = end - entry; passStart < lengthBeats; passStart += clip.braceBeats())
            addPass (clip.braceStart(), end, passStart);

    std::stable_sort (out.notes.begin(), out.notes.end(), [] (const Note& a, const Note& b) { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });
    std::stable_sort (out.controllers.begin(), out.controllers.end(), [] (const auto& a, const auto& b) { return a.time < b.time; });
    return out;
}

//==============================================================================
void ArrangementPlayer::track (const std::vector<MidiEvent>& events, size_t from)
{
    for (size_t i = from; i < events.size(); ++i)
    {
        const auto& e = events[i];
        const int type = e.status & 0xF0;
        if (type == 0xE0 || (type == 0xB0 && e.data1 == 64))
        {
            resetControllers = true;
            continue;
        }
        if (type == 0xB0 || type == 0xD0)
            continue;
        if (type == 0x90 && e.data2 > 0)
            held[e.data1] = (uint8_t) std::min (255, held[e.data1] + 1);
        else if ((type == 0x80 || type == 0x90) && held[e.data1] > 0)
            --held[e.data1];
    }
}

void ArrangementPlayer::releaseAll (int offset, std::vector<MidiEvent>& out)
{
    if (resetControllers)
    {
        // A clip that moved the sustain pedal or the pitch wheel must not leave them where its last event put them.
        out.push_back (MidiEvent::controller (offset, 64, 0));
        out.push_back ({ offset, 0xE0, 0x00, 0x40 });
        resetControllers = false;
    }
    for (int n = 0; n < 128; ++n)
        while (held[(size_t) n] > 0)
        {
            out.push_back (MidiEvent::noteOff (offset, n));
            --held[(size_t) n];
        }
}

void ArrangementPlayer::reset() noexcept
{
    std::fill (held.begin(), held.end(), (uint8_t) 0);
    resetControllers = false;
    invalidate();
}

bool ArrangementPlayer::hasHeldNotes() const noexcept
{
    return std::any_of (held.begin(), held.end(), [] (uint8_t n) { return n > 0; });
}

void ArrangementPlayer::process (const ArrangementClips& clips, double wa, double wb, double spb, int windowOffset, int windowSamples,
                                 std::vector<MidiEvent>& out)
{
    if (wb <= wa || windowSamples <= 0)
        return;

    const bool jumped = std::isnan (expectedBeat) || std::abs (wa - expectedBeat) > 1.0e-6;
    expectedBeat = wb;

    // Everything is rendered with offsets relative to the window and moved into the block at the end.
    const size_t first = out.size();
    if (jumped)
        releaseAll (0, out);

    for (auto it = firstEndingAfter (clips, wa); it != clips.end() && it->startBeat < wb; ++it)
    {
        const size_t mark = out.size();
        renderOne (*it, wa, wb, spb, windowSamples, out, jumped);
        track (out, mark);
        if (it->endBeat <= wb)
            releaseAll (toOffset (it->endBeat, wa, spb, windowSamples), out); // the clip is cut off: nothing may hang on
    }

    for (size_t i = first; i < out.size(); ++i)
        out[i].sampleOffset = std::min (out[i].sampleOffset + windowOffset, windowOffset + windowSamples - 1);
}

} // namespace daw::engine
