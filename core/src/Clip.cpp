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

    // Window [a, b) in absolute beats, offsets measured from originBeat.
    void renderWindow (const MidiClip& clip, double clipStart, double a, double b, double originBeat,
                       double samplesPerBeat, int numSamples, std::vector<MidiEvent>& out)
    {
        const double len = clip.lengthBeats;
        if (len <= 0.0 || b <= a)
            return;

        const double la = std::max (0.0, a - clipStart), lb = b - clipStart; // clip-local window
        if (lb <= 0.0)
            return;

        const long firstLoop = std::max (0L, (long) std::floor (la / len) - 1);
        const long lastLoop = (long) std::floor (lb / len);

        for (long k = firstLoop; k <= lastLoop; ++k)
        {
            const double loopStart = (double) k * len;
            for (const auto& n : clip.notes)
            {
                if (n.start < 0.0 || n.start >= len)
                    continue;
                const double on = loopStart + n.start;
                const double off = loopStart + std::min (n.start + std::max (n.length, 1.0e-3), len);

                if (on >= la && on < lb)
                    out.push_back (MidiEvent::noteOn (toOffset (clipStart + on, originBeat, samplesPerBeat, numSamples),
                                                      n.pitch, std::clamp (n.velocity, 1, 127)));
                if (off >= la && off < lb)
                    out.push_back (MidiEvent::noteOff (toOffset (clipStart + off, originBeat, samplesPerBeat, numSamples), n.pitch));
            }
        }
    }
} // namespace

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
}

} // namespace daw::engine
