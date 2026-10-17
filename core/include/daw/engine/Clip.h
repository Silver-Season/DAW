#pragma once

#include "daw/synth/PrismaSynth.h" // daw::MidiEvent

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

    int playing = kNoClip, queued = kNoClip;
    const MidiClip* playingClip = nullptr;
    const MidiClip* queuedClip = nullptr;
    double startBeat = 0.0, queuedBeat = 0.0, stopBeat = 0.0;
    bool stopQueued = false;
    std::vector<uint8_t> held = std::vector<uint8_t> (128, 0);
};

// Sample-accurate transport. Position is kept as an integer sample count from a tempo anchor,
// so beat positions never drift through floating-point accumulation.
class Transport
{
public:
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
    void play() noexcept { playing = true; }
    void stop() noexcept
    {
        playing = false;
        anchorBeat = 0.0;
        anchorSample = positionSamples = 0;
    }
    bool isPlaying() const noexcept { return playing; }
    double getPositionBeats() const noexcept { return beatAt (positionSamples); }
    double samplesPerBeat() const noexcept { return sampleRate * 60.0 / bpm; }

    struct Block
    {
        double startBeat, endBeat;
    };

    // Returns the beat window of the next block and advances (only while playing).
    Block advance (int numSamples) noexcept
    {
        const Block b { beatAt (positionSamples), beatAt (positionSamples + numSamples) };
        if (playing)
            positionSamples += numSamples;
        return b;
    }

private:
    double beatAt (int64_t sample) const noexcept { return anchorBeat + (double) (sample - anchorSample) / samplesPerBeat(); }
    void reanchor() noexcept
    {
        anchorBeat = beatAt (positionSamples);
        anchorSample = positionSamples;
    }

    double sampleRate = 48000.0, bpm = 120.0, anchorBeat = 0.0;
    int64_t positionSamples = 0, anchorSample = 0;
    bool playing = false;
};

inline double nextQuantisedBeat (double now, double quantise)
{
    if (quantise <= 0.0)
        return now;
    const double q = std::ceil (now / quantise - 1.0e-9) * quantise;
    return q;
}

} // namespace daw::engine
