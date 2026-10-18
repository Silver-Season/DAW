// Stage 2B of docs/spec/02-midi-recording.md: metronome, count-in, recording into slots, overdub, controllers and
// capture (spec tests 1 to 5, 7 to 10 and 12), driven through the engine's audio callback by hand.
#include "TestSupport.h"

#include "../app/Source/Strings.h"
#include "../app/Source/engine/RtCheck.h"
#include "../app/Source/engine/SessionRecorder.h"
#include "../app/Source/model/ClipEdit.h"
#include "../app/Source/project/ProjectFile.h"

#include <array>
#include <atomic>
#include <cmath>

using namespace tests;

namespace
{
constexpr int kBlock = 256;
constexpr double kRate = 48000.0;
constexpr double kSpb = 24000.0; // 120 BPM

double gClockMs = 1000.0;
double scriptedClock() { return gClockMs; }

// Instrument that logs what it is sent. Fixed storage: the audio thread must not allocate.
class MidiLog final : public juce::AudioProcessor
{
public:
    struct Entry
    {
        int64_t sample;
        uint8_t status, d1, d2;
    };

    MidiLog() : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)) {}

    using AudioProcessor::processBlock;
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        for (const auto meta : midi)
        {
            const int n = count.load (std::memory_order_relaxed);
            if (n < (int) log.size())
            {
                log[(size_t) n] = { blocks * 256 + meta.samplePosition, meta.data[0], meta.numBytes > 1 ? meta.data[1] : (uint8_t) 0,
                                    meta.numBytes > 2 ? meta.data[2] : (uint8_t) 0 };
                count.store (n + 1, std::memory_order_relaxed);
            }
        }
        ++blocks;
        buffer.clear();
    }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "MidiLog"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    int countNoteOns (int pitch) const
    {
        int n = 0;
        for (int i = 0; i < count.load(); ++i)
            n += (log[(size_t) i].status & 0xF0) == 0x90 && log[(size_t) i].d1 == pitch && log[(size_t) i].d2 > 0 ? 1 : 0;
        return n;
    }
    std::vector<Entry> entries() const { return { log.begin(), log.begin() + count.load() }; }

    std::atomic<int> count { 0 };
    int64_t blocks = 0; // audio thread
    std::array<Entry, 16384> log {};
};

struct Sched
{
    double beat;
    uint8_t b0, b1, b2;
};
Sched noteOn (double beat, int pitch, int vel = 100) { return { beat, 0x90, (uint8_t) pitch, (uint8_t) vel }; }
Sched noteOff (double beat, int pitch) { return { beat, 0x80, (uint8_t) pitch, 0 }; }
Sched cc (double beat, int number, int value) { return { beat, 0xB0, (uint8_t) number, (uint8_t) value }; }

// A project with one armed MIDI track and an engine, with a scripted clock so that live events land at exact samples.
struct Rig
{
    explicit Rig (bool logDevice = true, int scenes = 4)
        : dev (kBlock, kRate), runner (f.engine, kBlock), recorder (f.engine, f.model)
    {
        if (logDevice)
            f.engine.setDeviceFactory ([this] (const DeviceRef&) -> std::unique_ptr<juce::AudioProcessor> {
                auto p = std::make_unique<MidiLog>();
                logs.push_back (p.get());
                return p;
            });
        for (int i = 0; i < scenes; ++i)
            f.model.insertScene (-1);
        track = addTrack();
        f.model.setSelectedTrack (track.id());
        f.model.setCountInBars (1);
        gClockMs = 1000.0;
        f.engine.audioDeviceAboutToStart (&dev);
        f.engine.rebuildGraphIfNeeded();
        f.engine.setClockForTesting (&scriptedClock);
        runner.run (1);
        f.model.undo.clearUndoHistory();
    }
    ~Rig() { f.engine.audioDeviceStopped(); }

    TrackRef addTrack()
    {
        auto t = f.model.addTrack ("midi");
        f.model.addDevice (t, "prisma", "Log");
        t.setArm (true, nullptr);
        f.engine.rebuildGraphIfNeeded();
        return t;
    }

    MidiLog* logOf (const TrackRef& t) const
    {
        for (auto* l : logs)
            if (f.engine.findProcessor (t.device (0).id()) == l)
                return l;
        return nullptr;
    }

    // One block with live events at the given offsets, then a recorder poll about every 6 blocks (30 Hz).
    void block (const std::vector<std::pair<int, Sched>>& events = {})
    {
        for (const auto& [offset, s] : events)
        {
            const uint8_t bytes[3] { s.b0, s.b1, s.b2 };
            f.engine.pushLiveMidi (bytes, 3, true, gClockMs + offset * 1000.0 / kRate + 0.0001);
        }
        gClockMs += kBlock * 1000.0 / kRate;
        runner.run (1);
        if (++blocks % 6 == 0)
            recorder.poll();
    }

    // Runs until the transport reaches `beat`; the events of `schedule` happen at the beats they name, as the player
    // would hear them (so the recorder has to take the latency off again).
    void runTo (double beat, const std::vector<Sched>& schedule)
    {
        const int comp = f.engine.getRecordCompensationSamples();
        while (f.engine.getPositionBeats() < beat)
        {
            const double blockStart = f.engine.getPositionBeats() * kSpb;
            std::vector<std::pair<int, Sched>> now;
            for (const auto& s : schedule)
            {
                const long at = std::lround (s.beat * kSpb + comp - blockStart);
                if (at >= 0 && at < kBlock)
                    now.push_back ({ (int) at, s });
            }
            block (now);
            if (++guard > 2000000)
                break;
        }
    }
    void runTo (double beat) { runTo (beat, {}); }

    Fixture f;
    FakeDevice dev;
    Runner runner;
    SessionRecorder recorder;
    TrackRef track;
    std::vector<MidiLog*> logs;
    int blocks = 0, guard = 0;
};

//==============================================================================
std::vector<float> renderLeft (AudioEngine& engine, int numBlocks, int block, double* peak = nullptr)
{
    Runner runner { engine, block };
    std::vector<float> out;
    for (int b = 0; b < numBlocks; ++b)
    {
        runner.run (1);
        for (int i = 0; i < block; ++i)
            out.push_back (runner.out.getSample (0, i));
    }
    if (peak != nullptr)
        *peak = runner.peak;
    return out;
}

// Positions where a click begins: the first non-zero sample after a long stretch of silence.
std::vector<int> clickStarts (const std::vector<float>& s)
{
    std::vector<int> out;
    int zeros = 100000;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (juce::exactlyEqual (s[i], 0.0f))
            ++zeros;
        else
        {
            if (zeros >= 3000)
                out.push_back ((int) i);
            zeros = 0;
        }
    }
    return out;
}

double rmsDb (const std::vector<float>& s, int from, int count)
{
    double sum = 0.0;
    for (int i = from; i < from + count; ++i)
        sum += (double) s[(size_t) i] * s[(size_t) i];
    return 10.0 * std::log10 (sum / count + 1.0e-30);
}

void testMetronome()
{
    std::printf ("[test 1] metronome: clicks start on the sample of each beat, the downbeat is 6 dB louder, off is silent\n");
    const auto render = [] (const std::function<void (ProjectModel&)>& setup, double* peak = nullptr) {
        Fixture f;
        FakeDevice dev (512, kRate);
        setup (f.model);
        f.engine.audioDeviceAboutToStart (&dev);
        f.engine.rebuildGraphIfNeeded();
        f.engine.play();
        auto out = renderLeft (f.engine, 375, 512, peak); // two bars at 120 BPM
        f.engine.audioDeviceStopped();
        return out;
    };

    const auto classic = render ([] (ProjectModel& m) { m.setMetronome (true); });
    const auto starts = clickStarts (classic);
    bool exact = starts.size() == 8;
    for (size_t k = 0; exact && k < starts.size(); ++k)
        exact = starts[k] == (int) k * 24000;
    check (exact, "eight clicks begin at k * 24000 exactly (found " + juce::String ((int) starts.size()) + ")");
    if (starts.size() == 8)
    {
        const double strong = rmsDb (classic, starts[0], 2400), weak = rmsDb (classic, starts[1], 2400), strong2 = rmsDb (classic, starts[4], 2400);
        std::printf ("    downbeat %.2f dB, beat %.2f dB, second downbeat %.2f dB\n", strong, weak, strong2);
        check (std::abs ((strong - weak) - 6.0) < 0.5 && std::abs ((strong2 - weak) - 6.0) < 0.5, "the first beat of a bar is 6 dB louder");
    }

    const auto wood = render ([] (ProjectModel& m) {
        m.setMetronome (true);
        m.setMetronomeSound ("wood");
    });
    const auto woodStarts = clickStarts (wood);
    check (woodStarts.size() == 8 && woodStarts[0] == 0 && woodStarts[3] == 72000, "the wooden click starts on the same samples");

    double peak = 0.0;
    render ([] (ProjectModel& m) { m.setMetronome (false); }, &peak);
    check (juce::exactlyEqual (peak, 0.0), "metronome off: silence");
    render ([] (ProjectModel& m) {
        m.setMetronome (true);
        m.setMetronomeOnlyRecording (true);
    }, &peak);
    check (juce::exactlyEqual (peak, 0.0), "'only while recording' is silent while nothing records");
    render ([] (ProjectModel& m) {
        m.setMetronome (true);
        m.setMetronomeVolumeDb (-60.0);
    }, &peak);
    check (juce::exactlyEqual (peak, 0.0), "at -60 dB the click is off");
}

//==============================================================================
void testInputLatency()
{
    std::printf ("[test 3a] a live event is placed at block position + offset - compensation\n");
    Fixture f;
    FakeDevice dev (kBlock, kRate);
    f.model.insertScene (-1);
    auto track = f.model.addTrack ("midi");
    f.model.addDevice (track, "prisma", "Prisma");
    track.setArm (true, nullptr);
    gClockMs = 1000.0;
    f.engine.audioDeviceAboutToStart (&dev);
    f.engine.rebuildGraphIfNeeded();
    f.engine.setClockForTesting (&scriptedClock);
    Runner runner { f.engine, kBlock };
    runner.run (1);
    f.engine.play();
    runner.run (10);

    const int comp = f.engine.getRecordCompensationSamples();
    check (comp == kBlock / 2, "automatic compensation = output latency (0 here) + half a block");
    const double blockStart = f.engine.getPositionBeats();
    const int64_t engineStart = f.engine.getEngineSample();
    const uint8_t on[3] { 0x90, 64, 100 };
    f.engine.pushLiveMidi (on, 3, true, gClockMs + 77 * 1000.0 / kRate + 0.0001);
    gClockMs += kBlock * 1000.0 / kRate;
    runner.run (1);

    std::vector<daw::engine::InputEvent> events;
    f.engine.popInputEvents (events);
    check (events.size() == 1, "one event reached the queue");
    if (events.size() == 1)
    {
        check (std::abs (events[0].beat - (blockStart + (77 - comp) / kSpb)) < 1e-9, "its beat is P + o - compensation");
        check (events[0].engineSample == engineStart + 77 && events[0].playing && events[0].trackId == track.id(), "raw engine sample, playing flag and track");
    }

    f.engine.setRecordCorrectionMs (10.0);
    check (f.engine.getRecordCompensationSamples() == comp + 480, "the manual correction (+10 ms) adds 480 samples");
    f.engine.setRecordCorrectionMs (500.0);
    check (std::abs (f.engine.getRecordCorrectionMs() - 50.0) < 1e-9, "and is limited to 50 ms");
    f.engine.setRecordCorrectionMs (0.0);

    // events that reach no track are queued once with track 0, others (clock, aftertouch per key) are not queued
    track.setArm (false, nullptr);
    f.model.setSelectedTrack (-1);
    const uint8_t on2[3] { 0x90, 60, 90 }, clock[1] { 0xF8 }, poly[3] { 0xA0, 60, 10 };
    f.engine.pushLiveMidi (on2, 3, false, gClockMs + 1.0);
    f.engine.pushLiveMidi (clock, 1, false, gClockMs + 1.0);
    f.engine.pushLiveMidi (poly, 3, false, gClockMs + 1.0);
    gClockMs += kBlock * 1000.0 / kRate;
    runner.run (1);
    events.clear();
    f.engine.popInputEvents (events);
    check (events.size() == 1 && events[0].trackId == 0, "an event for no track is queued with track 0; clock and poly pressure are not queued");
    f.engine.audioDeviceStopped();
}

//==============================================================================
void testRecordingIntoSlot()
{
    std::printf ("[test 2, 3, 4] count-in, recording 8 notes into a slot, hanging notes, undo\n");
    Rig r;
    auto other = r.addTrack();
    other.setArm (false, nullptr);
    {
        auto c = r.f.model.createClip (other, 1, 4.0);
        c.addNote ({ 0.0, 1.0, 72, 100 }, nullptr);
    }
    r.f.engine.rebuildGraphIfNeeded();
    r.f.model.undo.clearUndoHistory();
    const auto id = r.track.id();
    auto* rt = r.f.engine.findRuntime (id);
    auto* otherRt = r.f.engine.findRuntime (other.id());

    check (r.recorder.startRecording (id, 0), "recording can start");
    check (! r.recorder.startRecording (id, 1), "but not twice on one track");
    r.block();
    check (r.f.engine.isCountingIn() && r.f.engine.getPositionBeats() < -3.9, "the position shows a count-in of one bar");
    r.f.engine.launchClip (other.id(), 1);
    r.runTo (-0.5);
    check (r.f.engine.isCountingIn() && otherRt->playingSlot.load() == -1, "no clip starts during the count-in");
    check (r.runner.peak > 0.0f, "the count-in clicks even with the metronome off");

    // a note a little before beat 0 belongs to the start of the clip, one a whole beat early is not recorded
    std::vector<Sched> played { noteOn (-1.0, 50), noteOff (-0.9, 50), noteOn (-0.02, 60), noteOff (0.4, 60) };
    for (int i = 1; i < 8; ++i)
    {
        played.push_back (noteOn (i, 60 + i, 70 + i));
        played.push_back (noteOff (i + 0.5, 60 + i));
    }
    r.runTo (0.5, played);
    check (! r.f.engine.isCountingIn() && otherRt->playingSlot.load() == 1, "at beat 0 the count-in ends and the queued clip starts");
    check (rt->recordState.load() == TrackRuntime::recordRunning, "the recording runs");
    r.runTo (7.6, played);
    check (r.recorder.stopRecording (id), "stop is accepted");
    r.runTo (8.5, played);
    r.recorder.poll();

    auto clip = r.track.clipAt (0);
    check (clip.isValid() && std::abs (clip.lengthBeats() - 8.0) < 1e-9, "the clip is two bars long");
    check (clip.isValid() && clip.numNotes() == 8, "with 8 notes (found " + juce::String (clip.isValid() ? clip.numNotes() : -1) + ")");

    bool positions = clip.isValid() && clip.numNotes() == 8;
    for (int i = 0; positions && i < 8; ++i)
    {
        const auto n = clip.note (i);
        positions = std::abs (n.start - i) < 0.002 && n.pitch == 60 + i && std::abs (n.length - (i == 0 ? 0.4 : 0.5)) < 0.003
                    && (i == 0 || n.velocity == 70 + i);
        if (! positions)
            std::printf ("    note %d: start %.4f length %.4f pitch %d velocity %d\n", i, n.start, n.length, n.pitch, n.velocity);
    }
    check (positions, "the notes sit where they were played (latency taken off), lengths and velocities are right");
    check (rt->recordState.load() == TrackRuntime::recordIdle && ! r.recorder.isAnythingRecording(), "the recorder is idle again");
    r.runTo (12.5);
    check (rt->playingSlot.load() == 0 && std::abs (rt->playingStartBeat.load() - 8.0) < 1e-9, "the new clip loops from the end of the recording");

    // undo removes the whole clip, one transaction
    r.f.model.undo.undo();
    check (! r.track.clipAt (0).isValid(), "Cmd+Z removes the recorded clip at once");
    r.f.model.undo.redo();
    check (r.track.clipAt (0).isValid() && r.track.clipAt (0).numNotes() == 8, "and redo brings it back");

    // A second recording while the transport runs starts on the launch quantisation (a bar) and takes a fixed length.
    r.f.model.undo.clearUndoHistory();
    r.f.model.setRecordFixedBars (1);
    check (r.recorder.startRecording (id, 1), "second recording");
    r.block();
    const double start = rt->recordStartBeat.load();
    check (rt->recordState.load() == TrackRuntime::recordWaiting && std::abs(std::fmod (start, 4.0)) < 1e-9 && start >= r.f.engine.getPositionBeats(),
          "it waits for the next bar line");
    std::vector<Sched> second { noteOn (start + 0.0, 55), noteOff (start + 0.25, 55), noteOn (start + 1.0, 67) }; // 67 is never released
    r.runTo (start + 4.5, second);
    r.recorder.poll();
    auto hanging = r.track.clipAt (1);
    check (hanging.isValid() && std::abs (hanging.lengthBeats() - 4.0) < 1e-9, "a fixed length of one bar stops the recording by itself");
    bool closed = false;
    for (int i = 0; hanging.isValid() && i < hanging.numNotes(); ++i)
        if (hanging.note (i).pitch == 67)
            closed = std::abs (hanging.note (i).length - 3.0) < 0.01;
    check (closed, "a note still held at the end is closed at the end of the clip");
    r.runTo (start + 6.0);
    check (rt->playingSlot.load() == 1, "the new clip plays");
    auto* log = r.logOf (r.track);
    check (log != nullptr && log->countNoteOns (55) >= 2, "its downbeat note is played by the loop, not lost to the late launch");
    r.f.model.undo.undo();
    check (! r.track.clipAt (1).isValid() && r.track.clipAt (0).isValid(), "undo removes only the second recording");
}

//==============================================================================
void testHowRecordingsEnd()
{
    std::printf ("[test 3c] a recording ends with the stop button, with another launch, or is cancelled before it began\n");
    {
        Rig r;
        const auto id = r.track.id();
        r.recorder.startRecording (id, 0);
        r.block();
        r.runTo (-1.0);
        check (r.recorder.stopRecording (id), "stop during the count-in");
        r.runTo (1.0);
        r.recorder.poll();
        check (! r.track.clipAt (0).isValid() && ! r.recorder.isAnythingRecording(), "cancels the recording: no clip");
        check (r.f.engine.findRuntime (id)->recordState.load() == TrackRuntime::recordIdle, "and the engine is idle");
        r.f.engine.stop();
        r.runner.run (2);
    }
    {
        Rig r;
        r.f.model.setCountInBars (0);
        const auto id = r.track.id();
        r.recorder.startRecording (id, 0);
        r.block();
        r.runTo (5.2, { noteOn (1.0, 60), noteOff (1.5, 60), noteOn (4.5, 62), noteOff (5.0, 62) });
        r.f.engine.stop();
        r.runner.run (4);
        r.recorder.poll();
        const auto clip = r.track.clipAt (0);
        check (clip.isValid() && std::abs (clip.lengthBeats() - 8.0) < 1e-9 && clip.numNotes() == 2, "the transport stop ends the recording at the next whole bar (two bars)");
        check (! r.f.engine.isPlaying() && r.f.engine.findRuntime (id)->playingSlot.load() == -1, "and the transport stays stopped");
    }
    {
        Rig r;
        r.f.model.setCountInBars (0);
        const auto id = r.track.id();
        {
            auto older = r.f.model.createClip (r.track, 2, 4.0);
            older.addNote ({ 0.0, 1.0, 72, 100 }, nullptr);
        }
        r.f.engine.rebuildGraphIfNeeded();
        r.recorder.startRecording (id, 0);
        r.block();
        r.runTo (5.1, { noteOn (1.0, 60), noteOff (1.5, 60) });
        r.f.engine.launchClip (id, 2);
        r.runTo (10.0);
        r.recorder.poll();
        const auto clip = r.track.clipAt (0);
        check (clip.isValid() && std::abs (clip.lengthBeats() - 8.0) < 1e-9, "launching another clip ends the recording at the launch bar");
        check (r.f.engine.findRuntime (id)->playingSlot.load() == 2, "and the launched clip plays, not the recording");
    }
}

//==============================================================================
void testRecordQuantise()
{
    std::printf ("[test 3b] record quantise\n");
    Rig r;
    r.f.model.setCountInBars (0);
    r.f.model.setRecordQuantise (0.25);
    r.f.model.setRecordFixedBars (1);
    r.recorder.startRecording (r.track.id(), 0);
    r.block();
    const std::vector<Sched> played { noteOn (0.1, 60), noteOff (0.4, 60), noteOn (1.2, 62), noteOff (1.4, 62), noteOn (3.95, 64), noteOff (3.99, 64) };
    r.runTo (4.5, played);
    r.recorder.poll();
    auto clip = r.track.clipAt (0);
    check (clip.isValid() && clip.numNotes() == 3, "three notes");

    if (clip.isValid() && clip.numNotes() == 3)
    {
        check (std::abs (clip.note (0).start - 0.0) < 0.003 && std::abs (clip.note (2).start - 1.25) < 0.003, "starts move to the 1/16 grid");
        check (std::abs (clip.note (0).length - 0.3) < 0.003, "lengths are kept");
        check (clip.note (1).pitch == 64 && std::abs (clip.note (1).start) < 0.003, "a note pulled onto the loop point goes to the start of the loop");
    }
}

//==============================================================================
void testOverdub()
{
    std::printf ("[test 5] overdub: every pass is one layer, undo removes the last layer only\n");
    Rig r;
    r.f.model.setOverdub (true);
    auto clip = r.f.model.createClip (r.track, 0, 4.0);
    clip.addNote ({ 0.0, 0.5, 60, 100 }, nullptr);
    r.f.engine.rebuildGraphIfNeeded();
    r.f.model.undo.clearUndoHistory();
    const auto id = r.track.id();
    auto* rt = r.f.engine.findRuntime (id);

    r.f.engine.launchClip (id, 0);
    r.runTo (4.2);
    check (rt->playingSlot.load() == 0, "the clip plays");
    check (r.recorder.startRecording (id, 0), "record on the playing clip");
    r.block();
    check (rt->recordOverdub.load() && rt->recordState.load() == TrackRuntime::recordRunning, "it is an overdub: recording runs at once");
    r.recorder.poll();

    const std::vector<Sched> played { noteOn (5.0, 64), noteOff (5.5, 64), noteOn (10.0, 67), noteOff (10.5, 67) };
    r.runTo (8.6, played); // the loop has started its third pass: the first layer is written
    r.recorder.poll();
    check (clip.numNotes() == 2, "after the first pass of playing, its layer is in the clip (" + juce::String (clip.numNotes()) + " notes)");
    check (r.recorder.isRecording (id, 0), "and the recording goes on");
    r.runTo (10.8, played);
    r.recorder.stopRecording (id);
    r.runTo (11.5, played);
    r.recorder.poll();

    check (clip.numNotes() == 3 && ! r.recorder.isAnythingRecording(), "stopping adds the last layer");
    if (clip.numNotes() == 3)
    {
        check (clip.note (0).pitch == 60 && clip.note (1).pitch == 64 && clip.note (2).pitch == 67, "the old note stays, the new ones are added");
        check (std::abs (clip.note (1).start - 1.0) < 0.003 && std::abs (clip.note (2).start - 2.0) < 0.003, "at the position inside the loop where they were played");
        check (std::abs (clip.note (1).length - 0.5) < 0.003, "with their lengths");
    }
    check (rt->playingSlot.load() == 0 && std::abs (clip.lengthBeats() - 4.0) < 1e-9, "the clip kept playing and kept its length");
    r.f.model.undo.undo();
    check (clip.numNotes() == 2 && clip.note (1).pitch == 64, "undo removes only the second layer");
    r.f.model.undo.undo();
    check (clip.numNotes() == 1 && clip.note (0).pitch == 60, "the next undo removes the first layer");
    check (! r.f.model.undo.canUndo(), "and nothing else was recorded in the history");
}

//==============================================================================
void testControllers()
{
    std::printf ("[test 10] recorded controllers play back at the same values and positions\n");
    Rig r;
    r.f.model.setCountInBars (0);
    r.f.model.setRecordFixedBars (1);
    const auto id = r.track.id();
    r.recorder.startRecording (id, 0);
    r.block();
    const uint8_t bendLsb = 0x10, bendMsb = 100;
    const std::vector<Sched> played { noteOn (0.5, 60), cc (1.0, 1, 10), cc (1.4, 1, 10), cc (2.5, 1, 100), cc (3.0, 64, 127),
                                      { 3.25, 0xE0, bendLsb, bendMsb }, noteOff (3.5, 60), cc (3.6, 7, 80) };
    r.runTo (4.5, played);
    r.recorder.poll();
    auto clip = r.track.clipAt (0);
    check (clip.isValid(), "a clip was recorded");
    const auto ctl = clip.isValid() ? clip.controllers() : std::vector<daw::engine::ControllerEvent>();
    check (ctl.size() == 4, "modwheel (the repeated value once), sustain and bend are stored; volume is not (" + juce::String ((int) ctl.size()) + ")");
    if (ctl.size() == 4)
    {
        check (ctl[0].status == 0xB0 && ctl[0].data1 == 1 && ctl[0].data2 == 10 && std::abs (ctl[0].time - 1.0) < 0.002, "modwheel 10 at beat 1");
        check (ctl[1].data2 == 100 && std::abs (ctl[1].time - 2.5) < 0.002, "modwheel 100 at beat 2.5");
        check (ctl[2].data1 == 64 && ctl[2].data2 == 127, "sustain on");
        check (ctl[3].status == 0xE0 && ctl[3].data1 == bendLsb && ctl[3].data2 == bendMsb && std::abs (ctl[3].time - 3.25) < 0.002, "pitch bend with both bytes");
    }
    check (clip.isValid() && clip.state.getChild (clip.state.getNumChildren() - 1).hasType (ids::CONTROLLERS), "the CONTROLLERS node is the last child of the clip");

    // Playback through the engine: the log of the instrument shows the same events at the same samples.
    r.f.model.setRecordFixedBars (0);
    r.f.engine.stopAllClips();
    r.f.engine.stop();
    r.runner.run (20);
    r.f.engine.rebuildGraph();
    auto* log = r.logOf (r.track);
    check (log != nullptr, "the instrument is logging");
    if (log == nullptr || ctl.size() != 4)
        return;
    const auto before = log->entries().size();
    log->blocks = 0;
    const int64_t baseline = (int64_t) before;
    r.f.engine.launchClip (id, 0); // transport stopped: starts at beat 0 in the very next block
    for (int b = 0; b < 4 * 24000 / kBlock + 4; ++b)
        r.runner.run (1);
    bool exact = true;
    int found = 0;
    const auto all = log->entries();
    for (size_t i = (size_t) baseline; i < all.size(); ++i)
    {
        const auto& e = all[i];
        for (const auto& c : ctl)
            if (e.status == c.status && e.d1 == c.data1 && e.d2 == c.data2)
            {
                ++found;
                exact = exact && std::llabs (e.sample - std::llround (c.time * kSpb)) <= 1;
            }
    }
    check (found >= 4 && exact, "controller events come out with the recorded values within one sample of the recorded positions");
}

//==============================================================================
void testCapture()
{
    std::printf ("[test 7, 8, 9] capture: playing transport, stopped transport, repeated phrase\n");
    {
        // 6 bars played while the transport runs, starting in the middle of bar 2
        Rig r (false, 4);
        const auto id = r.track.id();
        r.f.model.setSelectedTrack (id);
        r.f.engine.play();
        std::vector<Sched> played;
        const int count = 23;
        for (int i = 0; i < count; ++i)
        {
            const double beat = 5.5 + i; // 5.5 .. 27.5
            played.push_back (noteOn (beat, 40 + (i * 7) % 29));
            played.push_back (noteOff (beat + 0.5, 40 + (i * 7) % 29));
        }
        r.runTo (29.0, played);
        const auto result = r.recorder.capture (id, 1);
        std::printf ("    %s\n", result.info.toRawUTF8());
        check (result.ok && result.bars == 8 && result.numNotes == count, "6 bars of playing give an 8-bar clip with all notes");
        auto clip = r.track.clipAt (1);
        check (clip.isValid() && std::abs (clip.lengthBeats() - 32.0) < 1e-9 && clip.numNotes() == count, "it is in the chosen scene");
        check (clip.isValid() && std::abs (clip.note (0).start - 1.5) < 0.003, "and starts on the bar line of the first note (first note at 1.5)");
        check (result.info.contains (Strings::tr ("8 bars", "8 тактов")) && result.info.contains (Strings::tr ("23 notes", "23 ноты")),
               "the hint text names bars and notes in Russian plurals");
        r.runTo (36.5);
        check (r.f.engine.findRuntime (id)->playingSlot.load() == 1, "the captured clip is launched while the transport runs");
        r.f.model.undo.undo();
        check (! r.track.clipAt (1).isValid(), "capture is one undo step");
    }
    {
        // 16th notes at 128 BPM with jitter, transport stopped, empty project
        Rig r (false, 4);
        const auto id = r.track.id();
        uint32_t lcg = 777;
        const double step = 60.0 / 128.0 / 4.0; // seconds
        const int total = 64;
        std::vector<double> times;
        for (int i = 0; i < total; ++i)
        {
            lcg = lcg * 1664525u + 1013904223u;
            times.push_back (i * step + ((double) (lcg >> 8) / (double) (1u << 24) * 2.0 - 1.0) * 0.010 + 0.05);
        }
        // play them by wall time: block b covers [b * 256 / 48000, ...)
        int next = 0;
        for (int b = 0; next < total || b < 40; ++b)
        {
            std::vector<std::pair<int, Sched>> events;
            const double t0 = b * kBlock / kRate, t1 = (b + 1) * kBlock / kRate;
            while (next < total && times[(size_t) next] < t1)
            {
                const int offset = std::max (0, (int) std::lround ((times[(size_t) next] - t0) * kRate));
                const int pitch = 48 + (next * 5) % 24;
                events.push_back ({ std::min (offset, kBlock - 1), noteOn (0, pitch) });
                if (offset + 1000 < kBlock)
                    events.push_back ({ offset + 1000 - 1000 + 10 > kBlock - 1 ? kBlock - 1 : offset + 10, noteOff (0, pitch) });
                ++next;
            }
            r.block (events);
            if (b > 2000)
                break;
        }
        r.runner.run (1);
        const auto result = r.recorder.capture (id, 0);
        std::printf ("    %s, tempo now %.2f\n", result.info.toRawUTF8(), r.f.model.tempo());
        check (result.ok && result.bars == 4 && result.numNotes == total, "64 sixteenths give a 4-bar clip");
        check (std::abs (r.f.model.tempo() - 128.0) < 0.5, "an empty project takes the tempo of the phrase (128 +- 0.5)");
        r.f.model.undo.undo();
        check (std::abs (r.f.model.tempo() - 120.0) < 1e-9 && ! r.track.clipAt (0).isValid(), "and undo gives back both the clip and the tempo");
    }
    {
        // the same 4 bars twice: one round is captured
        Rig r (false, 4);
        const auto id = r.track.id();
        r.f.engine.play();
        std::vector<Sched> played;
        for (int round = 0; round < 2; ++round)
            for (int i = 0; i < 16; ++i)
            {
                const double beat = 4.0 + round * 16.0 + i;
                const int pitch = 50 + (i * 3) % 17;
                played.push_back (noteOn (beat, pitch));
                played.push_back (noteOff (beat + 0.5, pitch));
            }
        r.runTo (37.0, played);
        const auto result = r.recorder.capture (id, 0);
        check (result.ok && result.bars == 4 && result.numNotes == 16, "a repeated phrase gives one 4-bar round (" + result.info + ")");
    }
    {
        Rig r (false, 2);
        const auto result = r.recorder.capture (r.track.id(), 0);
        check (! result.ok && result.info.isNotEmpty(), "nothing played: nothing captured, and the hint says so");
    }
}

//==============================================================================
void testModelWithControllers()
{
    std::printf ("[test 10b] a CONTROLLERS node does not disturb note editing, copying and saving\n");
    auto model = ProjectModel::createEmpty();
    model.insertScene (-1);
    const auto track = model.addTrack ("midi");
    auto clip = model.createClip (track, 0, 4.0);
    clip.addNote ({ 1.0, 0.5, 62, 100 }, &model.undo);
    clip.addNote ({ 0.0, 0.5, 60, 100 }, &model.undo);
    clip.setControllers ({ { 0.5, 0xB0, 1, 20 }, { 1.5, 0xE0, 0, 0x50 }, { 2.0, 0xD0, 33, 0 } }, &model.undo);
    check (clip.numNotes() == 2 && clip.controllers().size() == 3, "notes and controllers are counted apart");
    auto node = clip.addNote ({ 0.5, 0.5, 61, 90 }, &model.undo);
    check (clip.numNotes() == 3 && clip.note (1).pitch == 61 && clip.state.getChild (3).hasType (ids::CONTROLLERS), "new notes go in before the controllers");
    clipedit::shiftNotes (clip, clipedit::allNodes (clip), 0.25, 2, &model.undo);
    clipedit::quantizeNotes (clip, clipedit::allNodes (clip), 1.0, 1.0, 0.0, false, &model.undo);
    clip.removeNote (node, &model.undo);
    check (clip.numNotes() == 2 && clip.controllers().size() == 3, "shift, quantise and delete leave the controllers alone");
    clip.setNotes ({ { 0.0, 1.0, 70, 100 } }, &model.undo);
    check (clip.numNotes() == 1 && clip.controllers().size() == 3, "setNotes keeps the controllers");

    const auto copy = model.setClip (track, 0, clip);
    check (copy.controllers().size() == 3, "copying a clip copies its controllers");
    const auto text = project::serialise (model.root);
    const auto loaded = project::parseProject (text);
    check (loaded.ok(), "a project with controllers is saved and loaded");
    if (loaded.ok())
    {
        ProjectModel again (loaded.tree);
        const auto c = again.trackAt (0).clipAt (0);
        const auto ctl = c.controllers();
        check (c.numNotes() == 1 && ctl.size() == 3 && ctl[1].status == 0xE0 && ctl[1].data2 == 0x50 && ctl[2].data1 == 33, "and comes back as it was");
    }
    // a project saved before stage 2B (no controllers, no recording settings) opens with the defaults
    auto old = ProjectModel::createDefault();
    check (! old.metronome() && old.countInBars() == 1 && std::abs (old.metronomeVolumeDb() + 6.0) < 1e-9 && ! old.overdub() && std::abs (old.recordQuantise()) < 1e-9
               && old.recordFixedBars() == 0 && old.metronomeSound() == "classic",
           "recording preferences default as the spec says");
    old.setCountInBars (3);
    check (old.countInBars() == 2, "count-in is 0, 1, 2 or 4 bars");
}

//==============================================================================
void testRealtime()
{
    std::printf ("[test 12] four minutes of events while recording: nothing allocates on the audio thread\n");
    Rig r (false, 2);
    const auto id = r.track.id();
    r.f.model.setCountInBars (0);
    r.f.model.setMetronome (true);
    r.recorder.startRecording (id, 0);
    r.block();

    std::vector<Sched> played;
    for (int i = 0; i < 2400; ++i) // 240 s at 120 BPM = 480 beats
    {
        const double beat = 1.0 + i * 0.2;
        const int pitch = 36 + (i * 7) % 48;
        played.push_back (noteOn (beat, pitch));
        played.push_back (noteOff (beat + 0.1, pitch));
        played.push_back (cc (beat + 0.05, 1, (i * 5) % 128));
        played.push_back ({ beat + 0.07, 0xE0, (uint8_t) (i % 128), 64 });
    }
    std::sort (played.begin(), played.end(), [] (const Sched& a, const Sched& b) { return a.beat < b.beat; });

    daw::rt::resetViolations();
    const int comp = r.f.engine.getRecordCompensationSamples();
    size_t first = 0;
    int guard = 0, sentOns = 0;
    while (r.f.engine.getPositionBeats() < 480.0 && ++guard < 400000)
    {
        const double blockStart = r.f.engine.getPositionBeats() * kSpb;
        std::vector<std::pair<int, Sched>> now;
        while (first < played.size() && played[first].beat * kSpb + comp < blockStart)
            ++first;
        for (size_t k = first; k < played.size() && played[k].beat * kSpb + comp < blockStart + kBlock; ++k)
            now.push_back ({ (int) std::lround (played[k].beat * kSpb + comp - blockStart), played[k] });
        for (const auto& e : now)
            sentOns += e.second.b0 == 0x90 ? 1 : 0;
        r.block (now);
    }
    const auto violations = daw::rt::violationCount();
    check (r.f.engine.getPositionBeats() >= 480.0, "four minutes ran");
    std::printf ("    %lld heap operations on the audio thread, %u queue drops\n", (long long) violations, (unsigned) r.f.engine.getInputEventDrops());
    check (violations == 0, "no allocation or deallocation on the audio thread while recording");
    check (r.f.engine.getInputEventDrops() == 0, "no event was dropped by the queue");

    r.recorder.stopRecording (id);
    r.runTo (490.0);
    r.recorder.poll();
    auto clip = r.track.clipAt (0);
    check (clip.isValid() && clip.lengthBeats() >= 480.0 && clip.numNotes() > 2300, "the whole performance is in the clip (" + juce::String (clip.isValid() ? clip.numNotes() : 0) + " notes)");
    check (r.recorder.getCaptureBuffer().size() > 9000 && sentOns > 2300, "and in the capture buffer");
    std::vector<SessionRecorder::NoteMark> marks;
    r.recorder.getRecentNoteOns (marks);
    check ((int) marks.size() == sentOns && marks.back().velocity == 100, "the ribbon can list the note-ons of the buffer");
}
} // namespace

void tests::runRecordingTests()
{
    testMetronome();
    testInputLatency();
    testRecordingIntoSlot();
    testHowRecordingsEnd();
    testRecordQuantise();
    testOverdub();
    testControllers();
    testCapture();
    testModelWithControllers();
    testRealtime();
}
