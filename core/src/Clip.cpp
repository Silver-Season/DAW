#include "daw/engine/Clip.h"

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

    // Emits the notes of one pass over the clip-content range [from, to). The pass begins `passStart` beats
    // after the launch; the render window is [la, lb) in beats after the launch.
    void renderPass (const MidiClip& clip, double from, double to, double passStart, double la, double lb, double clipStart,
                     double originBeat, double samplesPerBeat, int numSamples, std::vector<MidiEvent>& out)
    {
        const double passEnd = passStart + (to - from);
        if (passEnd <= la || passStart >= lb)
            return;
        for (const auto& n : clip.notes)
        {
            if (n.start < from || n.start >= to)
                continue;
            const double on = passStart + (n.start - from);
            const double off = passStart + (std::min (n.start + std::max (n.length, 1.0e-3), to) - from);
            if (on >= la && on < lb)
                out.push_back (MidiEvent::noteOn (toOffset (clipStart + on, originBeat, samplesPerBeat, numSamples), n.pitch,
                                                  std::clamp (n.velocity, 1, 127)));
            if (off >= la && off < lb)
                out.push_back (MidiEvent::noteOff (toOffset (clipStart + off, originBeat, samplesPerBeat, numSamples), n.pitch));
        }
    }

    // Window [a, b) in absolute beats, offsets measured from originBeat.
    void renderWindow (const MidiClip& clip, double clipStart, double a, double b, double originBeat,
                       double samplesPerBeat, int numSamples, std::vector<MidiEvent>& out)
    {
        if (b <= a)
            return;

        const double la = std::max (0.0, a - clipStart), lb = b - clipStart; // beats after the launch
        if (lb <= 0.0)
            return;

        const double end = clip.endBeat(), first = clip.firstPassBeats();
        // pass 0: from the start point to the end of the clip
        renderPass (clip, clip.playStart(), end, 0.0, la, lb, clipStart, originBeat, samplesPerBeat, numSamples, out);
        if (! clip.loop || lb <= first)
            return;

        // later passes: the loop brace over and over
        const double brace = clip.braceBeats(), from = clip.braceStart();
        const long firstK = std::max (0L, (long) std::floor ((la - first) / brace) - 1);
        const long lastK = (long) std::floor ((lb - first) / brace);
        for (long k = firstK; k <= lastK; ++k)
            renderPass (clip, from, end, first + (double) k * brace, la, lb, clipStart, originBeat, samplesPerBeat, numSamples, out);
    }
} // namespace

double MidiClip::positionAfter (double elapsed) const noexcept
{
    const double first = firstPassBeats();
    if (elapsed < 0.0)
        return playStart();
    if (elapsed < first)
        return playStart() + elapsed;
    if (! loop)
        return endBeat();
    return braceStart() + std::fmod (elapsed - first, braceBeats());
}

void renderClip (const MidiClip& clip, double clipStartBeat, double blockStartBeat, double blockEndBeat,
                 double samplesPerBeat, int numSamples, std::vector<MidiEvent>& out)
{
    renderWindow (clip, clipStartBeat, blockStartBeat, blockEndBeat, blockStartBeat, samplesPerBeat, numSamples, out);
}

void sortEvents (std::vector<MidiEvent>& events)
{
    // std::stable_sort allocates a temporary buffer, which is not allowed on the audio thread.
    const auto before = [] (const MidiEvent& x, const MidiEvent& y) {
        if (x.sampleOffset != y.sampleOffset)
            return x.sampleOffset < y.sampleOffset;
        const bool xOff = (x.status & 0xF0) == 0x80, yOff = (y.status & 0xF0) == 0x80;
        return xOff && ! yOff;
    };
    for (size_t i = 1; i < events.size(); ++i)
    {
        const MidiEvent v = events[i];
        size_t j = i;
        while (j > 0 && before (v, events[j - 1]))
        {
            events[j] = events[j - 1];
            --j;
        }
        events[j] = v;
    }
}

//==============================================================================
void ClipPlayer::launch (int slot, const MidiClip* clip, double nowBeat, double quantiseBeats)
{
    queued = slot;
    queuedClip = clip;
    queuedBeat = nextQuantisedBeat (nowBeat, quantiseBeats);
    stopQueued = false;
}

void ClipPlayer::stop (double nowBeat, double quantiseBeats)
{
    queued = kNoClip;
    queuedClip = nullptr;
    if (playing != kNoClip)
    {
        stopQueued = true;
        stopBeat = nextQuantisedBeat (nowBeat, quantiseBeats);
    }
}

void ClipPlayer::stopImmediately()
{
    playing = queued = kNoClip;
    playingClip = queuedClip = nullptr;
    stopQueued = false;
    // held notes are released on the next process() call
}

void ClipPlayer::clipChanged (int slot, const MidiClip* clip)
{
    if (slot == playing)
        playingClip = clip;
    if (slot == queued)
        queuedClip = clip;
    if (slot == playing && clip == nullptr)
        playing = kNoClip;
    if (slot == queued && clip == nullptr)
        queued = kNoClip;
}

void ClipPlayer::relocate (int newPlayingSlot, int newQueuedSlot) noexcept
{
    if (playing != kNoClip)
        playing = newPlayingSlot;
    if (queued != kNoClip)
        queued = newQueuedSlot;
}

void ClipPlayer::releaseHeld (int offset, std::vector<MidiEvent>& out)
{
    for (int n = 0; n < 128; ++n)
        while (held[(size_t) n] > 0)
        {
            out.push_back (MidiEvent::noteOff (offset, n));
            --held[(size_t) n];
        }
}

void ClipPlayer::track (const std::vector<MidiEvent>& events, size_t from)
{
    for (size_t i = from; i < events.size(); ++i)
    {
        const auto& e = events[i];
        const int type = e.status & 0xF0;
        if (type == 0x90 && e.data2 > 0)
            held[e.data1] = (uint8_t) std::min (255, held[e.data1] + 1);
        else if ((type == 0x80 || type == 0x90) && held[e.data1] > 0)
            --held[e.data1];
    }
}

void ClipPlayer::process (double a, double b, double spb, int numSamples, std::vector<MidiEvent>& out)
{
    const size_t first = out.size();
    double cursor = a;

    if (playing == kNoClip && queued == kNoClip)
    {
        releaseHeld (0, out);
        return;
    }

    if (stopQueued && stopBeat < b)
    {
        const double at = std::max (a, stopBeat);
        if (playingClip != nullptr)
            renderWindow (*playingClip, startBeat, cursor, at, a, spb, numSamples, out);
        track (out, first);
        releaseHeld (toOffset (at, a, spb, numSamples), out);
        playing = kNoClip;
        playingClip = nullptr;
        stopQueued = false;
        return;
    }

    if (queued != kNoClip && queuedBeat < b)
    {
        const double at = std::max (a, queuedBeat);
        if (playingClip != nullptr)
            renderWindow (*playingClip, startBeat, cursor, at, a, spb, numSamples, out);
        track (out, first);
        releaseHeld (toOffset (at, a, spb, numSamples), out);

        playing = queued;
        playingClip = queuedClip;
        startBeat = at;
        queued = kNoClip;
        queuedClip = nullptr;
        cursor = at;
    }

    const size_t mark = out.size();
    if (playingClip != nullptr)
        renderWindow (*playingClip, startBeat, cursor, b, a, spb, numSamples, out);
    track (out, mark);

    // A clip with the loop off ends after its first pass, like a one-shot in Live.
    if (playingClip != nullptr && ! playingClip->loop)
    {
        const double endAt = startBeat + playingClip->firstPassBeats();
        if (endAt <= b)
        {
            releaseHeld (toOffset (endAt, a, spb, numSamples), out);
            playing = kNoClip;
            playingClip = nullptr;
        }
    }
}

} // namespace daw::engine
