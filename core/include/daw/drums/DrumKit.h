#pragma once

#include "daw/synth/PrismaSynth.h" // daw::MidiEvent

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace daw::drums
{

// ---------------------------------------------------------------------------------------------------------------
// Pads and notes
// ---------------------------------------------------------------------------------------------------------------

inline constexpr int kNumPads = 16;
inline constexpr int kFirstNote = 36; // C1 (C3 = 60), the first pad, like the bottom-left pad of most controllers
inline constexpr int kMaxChokeGroups = 8;
inline constexpr int kMaxPadPolyphony = 8;

// What each pad is for. The GM drum map is kept where it matters (36 kick, 38 snare, 39 clap, 42 closed hat,
// 46 open hat, 49 crash, 51 ride, the toms on 41/43/45), so General MIDI drum files and controller drum modes
// (Launchkey, Maschine: notes 36..51) land on the right sounds.
namespace Pad
{
inline constexpr int kick = 0;       // 36 C1
inline constexpr int rim = 1;        // 37 C#1
inline constexpr int snare = 2;      // 38 D1
inline constexpr int clap = 3;       // 39 D#1
inline constexpr int snare2 = 4;     // 40 E1  (snare 2 or snap)
inline constexpr int tomLow = 5;     // 41 F1
inline constexpr int closedHat = 6;  // 42 F#1
inline constexpr int tomMid = 7;     // 43 G1
inline constexpr int shaker = 8;     // 44 G#1 (the GM pedal-hat slot: a short hat-like sound)
inline constexpr int tomHigh = 9;    // 45 A1
inline constexpr int openHat = 10;   // 46 A#1
inline constexpr int cowbell = 11;   // 47 B1
inline constexpr int kick2 = 12;     // 48 C2  (second kick or a long 808)
inline constexpr int crash = 13;     // 49 C#2
inline constexpr int perc = 14;      // 50 D2
inline constexpr int ride = 15;      // 51 D#2
} // namespace Pad

struct PadSlot
{
    int note;
    const char* noteName; // C3 = 60 convention, as in the piano roll
    const char* role;     // English identifier; the UI shows its own translated name
};

// The fixed pad map: index = pad, notes 36..51.
const std::array<PadSlot, kNumPads>& padMap() noexcept;

inline int padForNote (int note) noexcept
{
    const int p = note - kFirstNote;
    return p >= 0 && p < kNumPads ? p : -1;
}

inline int noteForPad (int pad) noexcept { return kFirstNote + pad; }

// ---------------------------------------------------------------------------------------------------------------
// Samples and pad parameters
// ---------------------------------------------------------------------------------------------------------------

// Immutable once handed to the engine. `right` is used only when channels == 2 and it is as long as `left`.
struct Sample
{
    double rate = 48000.0;
    int channels = 1;
    std::vector<float> left, right;

    int64_t numFrames() const noexcept { return (int64_t) left.size(); }
};

using SamplePtr = std::shared_ptr<const Sample>;

struct PadParams
{
    float tune = 0.0f;         // semitones, -24..24
    float fine = 0.0f;         // cents, -100..100
    float decay = 10.0f;       // seconds until the amp envelope's release has ended, 0.01..10; 10 = whole sample
    float attack = 0.0f;       // ms, 0..100
    float volumeDb = 0.0f;     // -60..+12, -60 = silent
    float pan = 0.0f;          // -1..1
    float cutoff = 20000.0f;   // low-pass Hz, 20..20000; 20000 = filter off
    float resonance = 0.0f;    // 0..1
    float drive = 0.0f;        // soft clip, 0..24 dB
    float startOffset = 0.0f;  // 0..1 of the sample length, skipped from where playback starts
    bool reverse = false;
    float velocitySens = 1.0f; // 0 = every hit at full level, 1 = level follows velocity linearly
    int chokeGroup = 0;        // 0 = none, 1..8
    bool mute = false;
    bool solo = false;
    bool oneShot = true;       // false = gate: a note-off starts the release
    int polyphony = 4;         // voices per pad, 1..8; the oldest is stolen

    bool operator== (const PadParams&) const = default;
};

enum class PadParam
{
    tune,
    fine,
    decay,
    attack,
    volumeDb,
    pan,
    cutoff,
    resonance,
    drive,
    startOffset,
    reverse,
    velocitySens,
    chokeGroup,
    mute,
    solo,
    oneShot,
    polyphony,
    count
};

inline constexpr int kNumPadParams = (int) PadParam::count;

struct PadParamInfo
{
    const char* key; // used in the kit text format, never shown to the user
    float minValue, maxValue, defaultValue;
    bool discrete;   // bools and integers are rounded
};

const PadParamInfo& padParamInfo (PadParam id) noexcept;
float getParam (const PadParams& p, PadParam id) noexcept;
void setParam (PadParams& p, PadParam id, float value) noexcept; // clamps to the range, rounds discrete values

// ---------------------------------------------------------------------------------------------------------------
// Kit state (saved in the project)
// ---------------------------------------------------------------------------------------------------------------

struct PadState
{
    std::string name;
    // Where the sample comes from: "factory:<kit>#<pad>" for a synthesised factory sound (see DrumSynth.h), an
    // absolute or project-relative file path for a user sample, empty for an empty pad. The app resolves it.
    std::string source;
    PadParams params;

    bool operator== (const PadState&) const = default;
};

struct KitState
{
    std::string name;
    std::array<PadState, kNumPads> pads;

    bool operator== (const KitState&) const = default;

    // A line-based "key=value" text: `format=daw.drumkit`, `version=1`, `name=...`, then `pad36.source=...`,
    // `pad36.tune=...` and so on, keyed by note so the format survives a bigger pad grid later. Numbers use the
    // classic C locale with 9 significant digits, so a float survives the round trip bit for bit.
    std::string toText() const;
    // Unknown keys are ignored and missing keys keep their defaults, so older and newer files both load.
    // Returns false (and leaves `out` untouched) when the text is not a drum kit.
    static bool fromText (const std::string& text, KitState& out);
};

// ---------------------------------------------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------------------------------------------

// A note event for process(). velocity in (0, 1]; velocity <= 0 is a note-off.
struct DrumEvent
{
    int offset = 0;
    int note = 0;
    float velocity = 1.0f;
};

// 16-pad sample player. Real-time safe: process(), noteOn(), noteOff() and allNotesOff() never allocate, lock or
// wait.
//
// Threads:
//   - message thread: prepare(), setPadSample(), collectGarbage(), getPadSample();
//   - any thread:     setPadParam(), setPadParams(), getPadParams(), padActivity(), numActiveVoices();
//   - audio thread:   process(), noteOn(), noteOff(), allNotesOff(), reset().
// noteOn()/noteOff() queue the event for the next process() call (at the given offset inside that block), so a
// pad click in the UI goes through the app's own command FIFO and is replayed here on the audio thread.
//
// Sample hand-over: setPadSample() keeps the shared_ptr on the message thread and publishes the raw pointer with
// an atomic store. The audio thread copies the raw pointer into a voice at note-on and never owns it. The replaced
// shared_ptr goes to a retired list together with the audio thread's cycle counter at that moment. At the end of
// every process() the audio thread publishes, per voice slot, the sample that slot is playing, and then bumps the
// cycle counter. collectGarbage() frees a retired sample once the counter has moved past its retirement (so any
// block that could have picked up the old pointer has finished and published) and no slot reports it any more.
// A voice can never pick the old pointer up again after that, so it is safe. A voice playing the old sample
// therefore always finishes, and the memory is released on the message thread, never in the audio callback.
// Call collectGarbage() from a UI timer (it is cheap and does nothing when nothing is retired).
class DrumKitEngine
{
public:
    static constexpr int kDefaultMaxVoices = 32;
    static constexpr int kMaxQueuedEvents = 256;   // noteOn()/noteOff() calls between two process() calls
    static constexpr int kMaxEventsPerBlock = 1024; // further events in one block are dropped
    static constexpr float kChokeFadeMs = 3.0f;
    static constexpr float kStealFadeMs = 2.0f;
    static constexpr float kActivityDecaySeconds = 0.15f;

    DrumKitEngine();
    ~DrumKitEngine();
    DrumKitEngine (const DrumKitEngine&) = delete;
    DrumKitEngine& operator= (const DrumKitEngine&) = delete;

    // Allocates the voices and buffers. Call with the audio stopped. maxVoices = sounding voices; stolen and choked
    // voices fade out in extra slots so they never cut a new hit short.
    void prepare (double sampleRate, int maxBlockSize, int maxVoices = kDefaultMaxVoices);

    void setPadSample (int pad, SamplePtr sample);
    SamplePtr getPadSample (int pad) const;
    void collectGarbage();
    int numRetiredSamples() const noexcept { return (int) retired.size(); }

    void setPadParam (int pad, PadParam id, float value) noexcept;
    void setPadParams (int pad, const PadParams& p) noexcept;
    PadParams getPadParams (int pad) const noexcept;

    // 0..1: velocity of the last hit, decaying to 0 within about kActivityDecaySeconds. For flashing pads.
    float padActivity (int pad) const noexcept;
    int numActiveVoices() const noexcept { return activeVoices.load (std::memory_order_relaxed); }

    void noteOn (int note, float velocity, int sampleOffset = 0) noexcept;
    void noteOff (int note, int sampleOffset = 0) noexcept;
    void allNotesOff() noexcept; // every voice fades out in kStealFadeMs
    void reset() noexcept;       // silences everything at once (transport stop with a hard cut, prepare)

    // Overwrites outL/outR with numSamples of audio. Events must lie in [0, numSamples) and are applied at their
    // sample offset; unsorted input is sorted. MIDI: note-on (velocity 0 = note-off), note-off, CC 120/123.
    void process (float* outL, float* outR, int numSamples, const MidiEvent* events = nullptr, int numEvents = 0) noexcept;
    void process (float* outL, float* outR, int numSamples, const DrumEvent* events, int numEvents) noexcept;

private:
    struct Voice;

    enum class EventKind : uint8_t
    {
        on,
        off,
        allOff
    };

    struct Event
    {
        int offset = 0;
        int note = 0;
        float velocity = 0.0f;
        EventKind kind = EventKind::on;
    };

    struct PadMix
    {
        float gainL = 1.0f, gainR = 1.0f;
        float cutoff = 20000.0f, resonance = 0.0f;
        float driveGain = 1.0f, driveNorm = 1.0f;
    };

    struct PadSlotState
    {
        std::atomic<const Sample*> sample { nullptr };
        std::array<std::atomic<float>, kNumPadParams> params {};
        std::atomic<float> activity { 0.0f };
    };

    struct Retired
    {
        SamplePtr sample;
        uint64_t cycle = 0;
    };

    void queue (const Event& e) noexcept;
    void addEvent (const Event& e) noexcept;
    void renderBlock (float* outL, float* outR, int numSamples) noexcept;
    void handle (const Event& e) noexcept;
    void startNote (int pad, float velocity) noexcept;
    void releaseNote (int pad) noexcept;
    void beginFade (Voice& v, float ms) noexcept;
    Voice* takeSlot() noexcept;
    void computeMix() noexcept;
    void publishVoices() noexcept;

    double sampleRate = 48000.0;
    int maxSounding = kDefaultMaxVoices;
    int numSlots = 0;
    std::unique_ptr<Voice[]> voices;
    std::unique_ptr<std::atomic<const Sample*>[]> slotInUse;
    std::atomic<uint64_t> cycle { 0 };
    std::atomic<int> activeVoices { 0 };
    uint64_t ageCounter = 0;

    std::array<PadSlotState, kNumPads> pads;
    std::array<PadParams, kNumPads> blockParams {};
    std::array<PadMix, kNumPads> mix {};

    std::array<Event, kMaxQueuedEvents> pending {};
    int numPending = 0;
    std::array<Event, kMaxEventsPerBlock> blockEvents {};
    int numBlockEvents = 0;

    // Message thread only.
    std::array<SamplePtr, kNumPads> owned;
    std::vector<Retired> retired;
};

} // namespace daw::drums
