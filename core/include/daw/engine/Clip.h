#pragma once

#include "daw/synth/PrismaSynth.h" // daw::MidiEvent

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace daw::engine
{

struct Note
{
    double start = 0.0;  // beats from clip start
    double length = 1.0; // beats
    int pitch = 60;
    int velocity = 100;
};

// A controller movement stored in a clip: time in beats from the clip start. status is 0xB0 (data1 = controller
// number, data2 = value), 0xE0 (pitch bend, data1 = low 7 bits, data2 = high 7 bits) or 0xD0 (channel pressure, data1 = value).
struct ControllerEvent
{
    double time = 0.0;
    uint8_t status = 0xB0, data1 = 0, data2 = 0;
};

// A clip plays from `startOffset` to its end (`lengthBeats`). With `loop` on it then repeats the brace
// [loopStart, lengthBeats) for as long as it is playing; with `loop` off it plays that one pass and stops.
// Everything is in beats from the start of the clip's content.
struct MidiClip
{
    std::string name;
    uint32_t colour = 0xff5fa8d3;
    double lengthBeats = 4.0; // the end of the clip, which is the end of the loop brace
    double loopStart = 0.0;   // start of the loop brace
    double startOffset = 0.0; // where playback begins when the clip is launched
    bool loop = true;
    std::vector<Note> notes;
    std::vector<ControllerEvent> controllers; // sorted by time
    int64_t id = 0; // model clip id (0 = none): lets the engine recognise "the same clip" in a new playback graph

    // Sanitised copies of the three positions: 0 <= loopStart < end, loopStart <= ... , start < end.
    double endBeat() const noexcept { return lengthBeats < kMinLength ? kMinLength : lengthBeats; }
    double braceStart() const noexcept
    {
        const double e = endBeat();
        return loopStart < 0.0 ? 0.0 : (loopStart > e - kMinLength ? e - kMinLength : loopStart);
    }
    double playStart() const noexcept
    {
        const double e = endBeat();
        return startOffset < 0.0 ? 0.0 : (startOffset >= e ? braceStart() : startOffset);
    }
    // Beats from launch until the first pass reaches the end of the clip.
    double firstPassBeats() const noexcept { return endBeat() - playStart(); }
    double braceBeats() const noexcept { return endBeat() - braceStart(); }
    // Position inside the content `elapsed` beats after the launch (looping clips wrap, others stay at the end).
    double positionAfter (double elapsed) const noexcept;

    static constexpr double kMinLength = 1.0 / 64.0;
};

// Renders a looping clip into MIDI events for one audio block.
// The clip started playing at `clipStartBeat`; the block covers [blockStartBeat, blockEndBeat).
// Events are appended with sample offsets relative to the block start.
void renderClip (const MidiClip& clip, double clipStartBeat, double blockStartBeat, double blockEndBeat,
                 double samplesPerBeat, int numSamples, std::vector<MidiEvent>& out);

// The building block of arrangement playback (Arrangement.h): renders [a, b) (absolute beats) of a clip that starts
// playing at clipStartBeat by entering its content at `entry` (beats from the start of the content) instead of at
// startOffset, and then repeating the loop brace if `repeat`. Offsets are measured from originBeat. With `chase`,
// notes that began before `a` and are still sounding get a note-on at the first sample. With `closeAtEnd`, `b` is
// where the clip is cut off and notes still sounding there get a note-off at the last sample.
void renderClipFrom (const MidiClip& clip, double entry, bool repeat, double clipStartBeat, double a, double b, double originBeat,
                     double samplesPerBeat, int numSamples, std::vector<MidiEvent>& out, bool chase, bool closeAtEnd);

// Sorts events by offset; at equal offsets note-offs come before note-ons. Stable, never allocates
// (insertion sort: a block holds a handful of events), so it is safe on the audio thread.
void sortEvents (std::vector<MidiEvent>& events);

// Session View slot logic for one track: launch quantisation, looping, stop, hanging-note cleanup.
class ClipPlayer
{
public:
    static constexpr int kNoClip = -1;

    // Queues a launch; it starts on the next multiple of `quantiseBeats` (0 = immediately).
    void launch (int slot, const MidiClip* clip, double nowBeat, double quantiseBeats);
    // Starts the clip as if it had been launched at `startBeat`, which may already lie a little in the past by
    // the time the clip reached the graph. Notes that began within a quarter beat before the block in which
    // the launch happens are played at its first sample, so the downbeat of a freshly recorded loop is not lost.
    void launchAt (int slot, const MidiClip* clip, double startBeat);
    void stop (double nowBeat, double quantiseBeats);
    void stopImmediately();

    // Produces the events for the block and applies queued launches/stops that fall inside it.
    void process (double blockStartBeat, double blockEndBeat, double samplesPerBeat, int numSamples, std::vector<MidiEvent>& out);

    int playingSlot() const noexcept { return playing; }
    int queuedSlot() const noexcept { return queued; }
    bool isStopQueued() const noexcept { return stopQueued; }
    double playingStartBeat() const noexcept { return startBeat; }

    // The clip pointer must stay valid while it plays; call this when it is edited or deleted.
    void clipChanged (int slot, const MidiClip* clip);

    // The clips the player currently points at (may be null). The engine compares them with the slots of a
    // newly published graph to find out whether the playing clip moved, changed or went away.
    const MidiClip* playingClipPtr() const noexcept { return playingClip; }
    const MidiClip* queuedClipPtr() const noexcept { return queuedClip; }

    // Scenes were inserted or removed: the same clips now sit in other slots. Nothing restarts.
    void relocate (int newPlayingSlot, int newQueuedSlot) noexcept;

private:
    void releaseHeld (int offset, std::vector<MidiEvent>& out);
    void track (const std::vector<MidiEvent>& events, size_t from);

    static constexpr double kCatchUpBeats = 0.25;

    int playing = kNoClip, queued = kNoClip;
    const MidiClip* playingClip = nullptr;
    const MidiClip* queuedClip = nullptr;
    double startBeat = 0.0, queuedBeat = 0.0, stopBeat = 0.0;
    bool stopQueued = false, catchUp = false;
    bool resetControllers = false; // the clip moved sustain or pitch bend, so they are reset when it ends
    std::vector<uint8_t> held = std::vector<uint8_t> (128, 0);
};

// Sample-accurate transport. Position is kept as an integer sample count from a tempo anchor,
// so beat positions never drift through floating-point accumulation.
//
// There are two clocks. The monotonic clock (Block::startBeat/endBeat) runs from 0 at every start and never jumps:
// Session View clip launches, clip phase, session recording and the count-in all live on it. The song clock
// (Block::windows) is the position in the arrangement: it starts at the play start, can be moved by seek, and
// jumps from the loop end to the loop start. A block that contains a jump is cut into windows, each a stretch of
// the song that runs without a jump, with its own sample offset and length (the lengths add up to the block size).
class Transport
{
public:
    // A loop or a scheduled seek cuts a block into at most three windows (two in practice: a quantised seek that
    // falls into the same block as the loop end is the only way to get three).
    static constexpr int kMaxWindows = 3;
    // Shorter loops are ignored: a loop must be longer than any audio block.
    static constexpr double kMinLoopBeats = 0.25;

    struct SongWindow
    {
        int sampleOffset = 0; // from the start of the block
        int numSamples = 0;
        double startBeat = 0.0, endBeat = 0.0; // song beats, [start, end)
    };

    struct Block
    {
        double startBeat, endBeat; // monotonic clock
        double songStartBeat = 0.0; // song position at the first sample of the block
        int numWindows = 0;         // 0 while the transport is stopped
        SongWindow windows[kMaxWindows];
    };

    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        reanchor();
    }
    void setTempo (double newBpm) noexcept
    {
        reanchor();
        bpm = newBpm < 20.0 ? 20.0 : (newBpm > 999.0 ? 999.0 : newBpm);
    }
    double getTempo() const noexcept { return bpm; }

    // Starts the transport. The song begins at the song start (setSongStart) or, with `continueSong`, where it stopped;
    // a count-in (startAt) already placed it.
    void play (bool continueSong = false) noexcept
    {
        if (! playing && ! songPrimed && ! continueSong)
            setSongAnchor (songStart);
        songPrimed = false;
        playing = true;
    }
    // Starts from `beat` (negative for a count-in) as if the transport had been stopped, but keeps the play state.
    // The song runs from `songStart + beat`, so it reaches the song start when the monotonic clock reaches 0.
    void startAt (double beat) noexcept
    {
        anchorBeat = beat;
        anchorSample = positionSamples;
        if (! playing)
        {
            setSongAnchor (songStart + beat);
            songPrimed = true;
        }
    }
    // Stopping keeps the song position (isPlaying() false, getSongPositionBeats() where it stopped), so that the
    // transport can continue from there; the monotonic clock starts again from 0.
    void stop() noexcept
    {
        // A stop during a count-in leaves the song at its start, not before it.
        const double where = beatAt (positionSamples) < 0.0 ? songStart : songBeatAt (positionSamples);
        playing = false;
        anchorBeat = 0.0;
        anchorSample = positionSamples = 0;
        songAnchorBeat = where;
        songAnchorSample = 0;
        songPrimed = false;
        seekPending = false;
    }
    bool isPlaying() const noexcept { return playing; }
    // The monotonic position (see above).
    double getPositionBeats() const noexcept { return beatAt (positionSamples); }
    // The position in the song: where the next block starts, or where the song stopped.
    double getSongPositionBeats() const noexcept { return songBeatAt (positionSamples); }
    double samplesPerBeat() const noexcept { return sampleRate * 60.0 / bpm; }

    // Where the song begins when the transport starts from a stop (the arrangement play start).
    void setSongStart (double beat) noexcept { songStart = beat; }
    // Moves the song position now (the monotonic clock is not touched). The next block starts there.
    void seekSong (double beat) noexcept
    {
        setSongAnchor (beat);
        seekPending = false;
    }
    // Moves the song to `songBeat` at the sample where the monotonic clock reaches `monoBeat` (a launch quantisation
    // boundary). The jump is exact to the sample, like the loop. A later call replaces an earlier one.
    void scheduleSeek (double monoBeat, double songBeat) noexcept
    {
        seekPending = true;
        seekMonoBeat = monoBeat;
        seekSongBeat = songBeat;
    }
    bool isSeekScheduled() const noexcept { return seekPending; }

    // Loop of the song: when the song position reaches `end` it continues at `start`. Ignored when off or too short.
    void setLoop (double start, double end, bool on) noexcept
    {
        loopStart = start;
        loopEnd = end;
        loopOn = on && end - start >= kMinLoopBeats;
    }
    bool isLoopOn() const noexcept { return loopOn; }

    // Returns the beat windows of the next block and advances (only while playing).
    Block advance (int numSamples) noexcept
    {
        Block b { beatAt (positionSamples), beatAt (positionSamples + numSamples), songBeatAt (positionSamples), 0, {} };
        if (! playing)
            return b;

        const int64_t blockEnd = positionSamples + numSamples;
        int64_t p = positionSamples;
        while (p < blockEnd && b.numWindows < kMaxWindows)
        {
            // The earliest jump inside the rest of the block. A seek wins over a loop jump at the same sample.
            int64_t jumpAt = blockEnd;
            double target = 0.0;
            bool isSeek = false, isLoop = false;
            if (seekPending)
            {
                const int64_t s = std::max (p, anchorSample + (int64_t) std::llround ((seekMonoBeat - anchorBeat) * samplesPerBeat()));
                if (s < jumpAt)
                {
                    jumpAt = s;
                    target = seekSongBeat;
                    isSeek = true;
                }
            }
            if (loopOn)
            {
                // Only a loop end that lies ahead counts: a song position that already is behind it plays on.
                const int64_t s = songAnchorSample + (int64_t) std::llround ((loopEnd - songAnchorBeat) * samplesPerBeat());
                if (s >= p && s < jumpAt)
                {
                    jumpAt = s;
                    target = loopStart;
                    isLoop = true;
                    isSeek = false;
                }
            }

            if (jumpAt > p)
            {
                auto& w = b.windows[b.numWindows++];
                w.sampleOffset = (int) (p - positionSamples);
                w.numSamples = (int) (jumpAt - p);
                w.startBeat = songBeatAt (p);
                w.endBeat = songBeatAt (jumpAt);
            }
            if (! isSeek && ! isLoop)
                break;
            songAnchorBeat = target;
            songAnchorSample = jumpAt;
            if (isSeek)
                seekPending = false;
            p = jumpAt;
        }
        positionSamples = blockEnd;
        return b;
    }

private:
    double beatAt (int64_t sample) const noexcept { return anchorBeat + (double) (sample - anchorSample) / samplesPerBeat(); }
    double songBeatAt (int64_t sample) const noexcept { return songAnchorBeat + (double) (sample - songAnchorSample) / samplesPerBeat(); }
    void setSongAnchor (double beat) noexcept
    {
        songAnchorBeat = beat;
        songAnchorSample = positionSamples;
    }
    void reanchor() noexcept
    {
        anchorBeat = beatAt (positionSamples);
        anchorSample = positionSamples;
        songAnchorBeat = songBeatAt (positionSamples);
        songAnchorSample = positionSamples;
    }

    double sampleRate = 48000.0, bpm = 120.0, anchorBeat = 0.0;
    int64_t positionSamples = 0, anchorSample = 0;
    bool playing = false;

    double songAnchorBeat = 0.0, songStart = 0.0;
    int64_t songAnchorSample = 0;
    bool songPrimed = false; // a count-in has already put the song where it has to start
    bool loopOn = false;
    double loopStart = 0.0, loopEnd = 16.0;
    bool seekPending = false;
    double seekMonoBeat = 0.0, seekSongBeat = 0.0;
};

inline double nextQuantisedBeat (double now, double quantise)
{
    if (quantise <= 0.0)
        return now;
    const double q = std::ceil (now / quantise - 1.0e-9) * quantise;
    return q;
}

} // namespace daw::engine
