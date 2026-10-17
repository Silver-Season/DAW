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
    // With catchUp, a note that began before the window but is still sounding at its start is emitted at the
    // window start (offset 0): used by the first block of a launch that arrived a few milliseconds late.
    void renderPass (const MidiClip& clip, double from, double to, double passStart, double la, double lb, double clipStart,
                     double originBeat, double samplesPerBeat, int numSamples, std::vector<MidiEvent>& out, bool catchUp)
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
            const bool late = catchUp && on < la && off >= la;
            if ((on >= la || late) && on < lb)
                out.push_back (MidiEvent::noteOn (toOffset (clipStart + on, originBeat, samplesPerBeat, numSamples), n.pitch,
                                                  std::clamp (n.velocity, 1, 127)));
            if (off >= la && off < lb)
                out.push_back (MidiEvent::noteOff (toOffset (clipStart + off, originBeat, samplesPerBeat, numSamples), n.pitch));
        }
        for (const auto& c : clip.controllers)
        {
            if (c.time < from || c.time >= to)
                continue;
            const double at = passStart + (c.time - from);
            if ((at < la && ! catchUp) || at >= lb)
                continue;
            const int offset = toOffset (clipStart + at, originBeat, samplesPerBeat, numSamples);
            out.push_back ({ offset, c.status, c.data1, c.data2 });
        }
    }

    // Window [a, b) in absolute beats, offsets measured from originBeat.
    void renderWindow (const MidiClip& clip, double clipStart, double a, double b, double originBeat,
                       double samplesPerBeat, int numSamples, std::vector<MidiEvent>& out, bool catchUp = false)
    {
        if (b <= a)
            return;

        const double la = std::max (0.0, a - clipStart), lb = b - clipStart; // beats after the launch
        if (lb <= 0.0)
            return;

        const double end = clip.endBeat(), first = clip.firstPassBeats();
        // pass 0: from the start point to the end of the clip
        renderPass (clip, clip.playStart(), end, 0.0, la, lb, clipStart, originBeat, samplesPerBeat, numSamples, out, catchUp);
        if (! clip.loop || lb <= first)
            return;

        // later passes: the loop brace over and over
        const double brace = clip.braceBeats(), from = clip.braceStart();
        const long firstK = std::max (0L, (long) std::floor ((la - first) / brace) - 1);
        const long lastK = (long) std::floor ((lb - first) / brace);
        for (long k = firstK; k <= lastK; ++k)
            renderPass (clip, from, end, first + (double) k * brace, la, lb, clipStart, originBeat, samplesPerBeat, numSamples, out, catchUp);
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
    catchUp = false;
}

void ClipPlayer::launchAt (int slot, const MidiClip* clip, double startBeatOfLaunch)
{
    queued = slot;
    queuedClip = clip;
    queuedBeat = startBeatOfLaunch;
    stopQueued = false;
    catchUp = true;
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
    catchUp = false;
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

void ClipPlayer::track (const std::vector<MidiEvent>& events, size_t from)
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

void ClipPlayer::process (double a, double b, double spb, int numSamples, std::vector<MidiEvent>& out)
{
    const size_t first = out.size();
    // During a count-in the block starts before beat 0: nothing plays there, but sample offsets stay relative to
    // the real block start.
    const double a0 = std::max (a, 0.0);
    double cursor = a0;
    bool lateStart = false;

    if (playing == kNoClip && queued == kNoClip)
    {
        releaseHeld (0, out);
        return;
    }

    if (stopQueued && stopBeat < b)
    {
        const double at = std::max (a0, stopBeat);
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
        const double at = std::max (a0, queuedBeat);
        if (playingClip != nullptr)
            renderWindow (*playingClip, startBeat, cursor, at, a, spb, numSamples, out);
        track (out, first);
        releaseHeld (toOffset (at, a, spb, numSamples), out);

        playing = queued;
        playingClip = queuedClip;
        // A launch that reached the graph late keeps its own beat so that the loop stays on the bar grid.
        startBeat = catchUp ? std::min (at, queuedBeat) : at;
        lateStart = catchUp && at - startBeat > 0.0 && at - startBeat < kCatchUpBeats;
        catchUp = false;
        queued = kNoClip;
        queuedClip = nullptr;
        cursor = at;
    }

    const size_t mark = out.size();
    if (playingClip != nullptr)
        renderWindow (*playingClip, startBeat, cursor, b, a, spb, numSamples, out, lateStart);
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
