// Stage 5, part 2 of docs/spec/05-audio-recording.md: inputs and monitoring (5.3), recording sound to disk with latency
// compensation, gaps and takes (5.4; spec tests 4, 5 and 9) and the loopback test, driven through the engine's audio
// callback by hand with a fake device that has inputs.
#include "TestSupport.h"

#include "../app/Source/engine/RtCheck.h"
#include "../app/Source/engine/SessionRecorder.h"
#include "../app/Source/model/ArrangementEdit.h"

#include <functional>

using namespace tests;

namespace
{
constexpr int kBlock = 256;
constexpr double kRate = 48000.0;
constexpr int kBeat = 24000; // samples per beat at 120 BPM

// A project with one armed audio track listening to inputs 1/2, an engine with a fake device that has inputs, and the
// recorders. block() feeds the inputs from `signal` (by engine sample) and polls the recorder like the 30 Hz timer.
struct SoundRig
{
    explicit SoundRig (int inputLatency = 0, int outputLatency = 0, int numInputs = 2) : dev (kBlock, kRate), recorder (f.engine, f.model)
    {
        folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("daw-rec-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        folder.createDirectory();
        f.engine.setProjectFolder (folder);
        dev.numInputs = numInputs;
        dev.inputLatency = inputLatency;
        dev.outputLatency = outputLatency;
        f.model.insertScene (-1);
        f.model.insertScene (-1);
        f.model.setCountInBars (0);
        track = f.model.addTrack ("audio");
        track.setInput ("audio:0-1", nullptr);
        track.setArm (true, nullptr);
        f.engine.audioDeviceAboutToStart (&dev);
        f.engine.rebuildGraphIfNeeded();
        in.setSize (juce::jmax (1, numInputs), kBlock);
        out.setSize (2, kBlock);
        block();
        f.model.undo.clearUndoHistory();
    }

    ~SoundRig()
    {
        f.engine.audioDeviceStopped();
        folder.deleteRecursively();
    }

    void block()
    {
        const int64_t s0 = f.engine.getEngineSample();
        for (int c = 0; c < in.getNumChannels(); ++c)
            for (int i = 0; i < kBlock; ++i)
                in.setSample (c, i, signal != nullptr ? signal (c, s0 + i) : 0.0f);
        f.engine.audioDeviceIOCallbackWithContext (in.getArrayOfReadPointers(), dev.numInputs, out.getArrayOfWritePointers(), 2, kBlock, {});
        if (afterBlock != nullptr)
            afterBlock();
        if (++blocks % 6 == 0)
            recorder.poll();
    }

    void blocks_ (int n)
    {
        for (int i = 0; i < n; ++i)
            block();
    }

    // Runs until `done` or the guard runs out; the writer is helped along so the test does not depend on its timing.
    bool runUntil (const std::function<bool()>& done, int maxBlocks = 200000)
    {
        for (int i = 0; i < maxBlocks; ++i)
        {
            if (done())
                return true;
            block();
            if (i % 64 == 0)
                recorder.sound().processNowForTesting();
        }
        return done();
    }

    // The samples of a recorded file (channel 0 and 1), read back from disk.
    struct FileData
    {
        std::vector<float> left, right;
        double rate = 0.0;
        int bits = 0;
    };
    FileData readFile (int64_t fileId)
    {
        FileData d;
        const auto file = f.engine.resolveAudioFile (f.model.findAudioFile (fileId));
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true));
        if (reader == nullptr)
            return d;
        juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&buffer, 0, (int) reader->lengthInSamples, 0, true, true);
        d.left.assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + buffer.getNumSamples());
        const int r = buffer.getNumChannels() > 1 ? 1 : 0;
        d.right.assign (buffer.getReadPointer (r), buffer.getReadPointer (r) + buffer.getNumSamples());
        d.rate = reader->sampleRate;
        d.bits = (int) reader->bitsPerSample;
        return d;
    }

    Fixture f;
    FakeDevice dev;
    SessionRecorder recorder;
    TrackRef track;
    juce::File folder;
    juce::AudioBuffer<float> in, out;
    std::function<float (int, int64_t)> signal;
    std::function<void()> afterBlock;
    int blocks = 0;
};

std::vector<int> pulses (const std::vector<float>& s, float threshold)
{
    std::vector<int> at;
    for (int i = 0; i < (int) s.size(); ++i)
        if (std::abs (s[(size_t) i]) > threshold)
            at.push_back (i);
    return at;
}

//==============================================================================
void testStreamGaps()
{
    std::printf ("[5.4] record stream: a full ring becomes a gap of silence, the length stays right\n");
    RecordStream stream (1, 1024);
    check (stream.request (RecordStream::Mode::slot, 1), "request");
    std::vector<float> ones (600, 1.0f), twos (600, 2.0f);
    const float* p1[1] { ones.data() };
    const float* p2[1] { twos.data() };
    stream.write (p1, 600);
    stream.write (p2, 600); // 424 fit, 176 are lost
    check (stream.droppedFrames.load() == 176, "176 frames lost, got " + juce::String (stream.droppedFrames.load()));
    check (! stream.request (RecordStream::Mode::slot, 1), "a busy stream cannot be armed again");
    std::vector<float> all, chunk (4096);
    float* c[1] { chunk.data() };
    int got = stream.read (c, 4096);
    all.insert (all.end(), chunk.begin(), chunk.begin() + got);
    stream.write (p1, 100); // after the gap
    got = stream.read (c, 4096);
    all.insert (all.end(), chunk.begin(), chunk.begin() + got);
    check (all.size() == 1300, "600 + 600 + 100 frames come out, got " + juce::String ((int) all.size()));
    if (all.size() == 1300)
    {
        check (juce::exactlyEqual (all[599], 1.0f) && juce::exactlyEqual (all[600], 2.0f) && juce::exactlyEqual (all[1023], 2.0f),
               "the data before the gap");
        check (juce::exactlyEqual (all[1024], 0.0f) && juce::exactlyEqual (all[1199], 0.0f), "the gap is silent");
        check (juce::exactlyEqual (all[1200], 1.0f) && juce::exactlyEqual (all[1299], 1.0f), "what came after the gap is in place");
    }
}

// Spec test 4: impulses on the beats, recorded into a slot through a device with latency, land on the beats of the clip.
void testSlotRecordingOnBeats()
{
    std::printf ("[5.4] spec test 4: impulses on the beats land on the beats of the clip (+-1 sample after compensation)\n");
    SoundRig rig (100, 71);
    const int latency = rig.f.engine.getAudioRecordLatency();
    check (latency == 100 + 71 + 2 * kBlock, "latency = driver in + out + a buffer each way, got " + juce::String (latency));
    rig.f.model.setRecordFixedBars (2);

    check (rig.recorder.startRecording (rig.track.id(), 0), "recording starts on the audio track");
    const int64_t beat0 = rig.f.engine.getEngineSample(); // the next block starts the transport at beat 0
    // What the player hears at beat k comes back on the input `latency` samples later.
    rig.signal = [beat0, latency] (int ch, int64_t s) {
        const int64_t t = s - beat0 - latency;
        return t >= 0 && t % kBeat == 0 ? (ch == 0 ? 0.9f : -0.6f) : 0.0f;
    };
    const bool done = rig.runUntil ([&] { return rig.blocks > 10 && ! rig.recorder.isTrackRecording (rig.track.id()); });
    check (done, "the recording ended and was committed");

    const auto clip = rig.track.clipAt (0);
    check (clip.isValid() && clip.isAudio(), "an audio clip in the slot");
    if (! clip.isAudio())
        return;
    check (std::abs (clip.lengthBeats() - 8.0) < 1.0e-9, "two bars long: " + juce::String (clip.lengthBeats()));
    check (std::abs (clip.sampleStart() - latency) < 0.5, "sampleStart = latency: " + juce::String (clip.sampleStart()));
    const auto file = rig.f.model.findAudioFile (clip.fileId());
    check (file.isValid() && file.path().startsWith ("Samples/Recorded/"), "the file is in Samples/Recorded: " + file.path());
    check (file.lengthSamples() == 8 * kBeat + latency, "the file runs to the end + the latency: " + juce::String (file.lengthSamples()));

    const auto data = rig.readFile (clip.fileId());
    check (data.bits == 24 && juce::exactlyEqual (data.rate, kRate), "24-bit WAV at the project rate");
    const auto left = pulses (data.left, 0.5f), right = pulses (data.right, 0.3f);
    check (left.size() == 8 && right.size() == 8, "eight impulses on each channel, got " + juce::String ((int) left.size()));
    bool onBeats = left.size() == 8;
    for (size_t k = 0; k < left.size() && k < 8; ++k)
    {
        const double contentBeat = (left[k] - clip.sampleStart()) / (double) kBeat;
        onBeats = onBeats && std::abs (contentBeat - (double) k) * kBeat <= 1.0;
    }
    check (onBeats, "every impulse is on its beat of the clip");
    check (right.size() == 8 && data.right[(size_t) right[0]] < 0.0f, "the second channel is the second input");
    check (rig.f.model.undo.getNumActionsInCurrentTransaction() > 0 || rig.f.model.undo.canUndo(), "one undoable step");
    rig.f.model.undo.undo();
    check (! rig.track.clipAt (0).isValid(), "undo removes the clip");
    check (rig.f.engine.resolveAudioFile (file).existsAsFile(), "the file stays on disk after undo");
}

// Spec test 5: a writer that stands still. The recording goes on, the lost stretch is silence, the length is right.
void testOverflow()
{
    std::printf ("[5.4] spec test 5: a stalled writer leaves a silent gap, the file keeps its length, a warning is shown\n");
    SoundRig rig;
    const int latency = rig.f.engine.getAudioRecordLatency();
    rig.f.model.setRecordFixedBars (2); // 4 seconds; the ring holds 2
    juce::String warning;
    rig.recorder.onWarning = [&warning] (const juce::String& w) { warning = w; };
    rig.signal = [] (int, int64_t) { return 0.25f; };
    rig.recorder.sound().setWriterPausedForTesting (true);
    check (rig.recorder.startRecording (rig.track.id(), 0), "recording starts");
    for (int i = 0; i < 8 * kBeat / kBlock + 40; ++i)
        rig.block();
    rig.recorder.sound().setWriterPausedForTesting (false);
    check (rig.runUntil ([&] { return ! rig.recorder.isTrackRecording (rig.track.id()); }), "the recording was committed");
    const auto clip = rig.track.clipAt (0);
    check (clip.isAudio(), "the clip exists");
    if (! clip.isAudio())
        return;
    const auto data = rig.readFile (clip.fileId());
    check ((int) data.left.size() == 8 * kBeat + latency, "the file has its full length: " + juce::String ((int) data.left.size()));
    check (data.left.size() > 150000 && std::abs (data.left[1000] - 0.25f) < 1.0e-3 && std::abs (data.left[90000] - 0.25f) < 1.0e-3,
           "what fit into the ring is there");
    check (data.left.size() > 150000 && juce::exactlyEqual (data.left[150000], 0.0f), "the lost stretch is silent");
    check (warning.isNotEmpty(), "the user is told: " + warning);
}

// Recording onto the timeline in a loop: one clip, a take per pass, each reading the file where its pass was.
void testArrangementTakes()
{
    std::printf ("[5.4] arrangement: three passes of the loop become three takes of one file\n");
    SoundRig rig (0, 0);
    const int latency = rig.f.engine.getAudioRecordLatency();
    rig.f.model.setArrangementLoop (0.0, 4.0, true);
    check (rig.recorder.arrangement().start(), "arrangement recording starts with only an audio track armed");
    rig.signal = [] (int, int64_t s) { return (float) std::sin ((double) s * 0.01) * 0.3f; };
    rig.runUntil ([&] { return rig.f.engine.getPositionBeats() >= 12.0; });
    rig.recorder.arrangement().stop();
    check (rig.runUntil ([&] { return ! rig.recorder.arrangement().isActive(); }), "the recording was written");

    const auto clips = rig.track.arrangementClips();
    check (clips.size() == 1, "one clip on the track, got " + juce::String ((int) clips.size()));
    if (clips.size() != 1)
        return;
    const auto& a = clips[0];
    check (std::abs (a.start()) < 1.0e-9 && std::abs (a.length() - 4.0) < 1.0e-6, "it covers the loop");
    const auto takes = arrangement::takesOf (a);
    check (takes.size() == 3, "three takes, got " + juce::String ((int) takes.size()));
    check (a.activeTake() == 3, "the last pass is on top");
    check (a.clip().isAudio() && std::abs (a.clip().sampleStart() - (latency + 8.0 * kBeat)) < 1.0, "the top take reads the third pass");
    bool passes = true;
    for (const auto& t : a.takesNode())
    {
        const int number = (int) t.getProperty (ids::number);
        const ClipRef c (t.getChildWithName (ids::CLIP));
        passes = passes && c.isAudio() && c.fileId() == a.clip().fileId()
                 && std::abs (c.sampleStart() - (latency + (number - 1) * 4.0 * kBeat)) < 1.0;
    }
    check (passes, "the other takes read the earlier passes of the same file");
    check (arrangement::switchTake (rig.f.model, a, 1) && std::abs (a.clip().sampleStart() - latency) < 1.0, "switching to take 1");
}

void testMonitoring()
{
    std::printf ("[5.3] monitoring Auto / In / Off, input meter and its red mark\n");
    SoundRig rig;
    rig.signal = [] (int, int64_t) { return 0.1f; };
    const auto level = [&rig] {
        rig.blocks_ (4);
        return rig.out.getMagnitude (0, 0, kBlock);
    };
    rig.track.setMonitor ("auto", nullptr);
    check (std::abs (level() - 0.1f) < 1.0e-3, "auto, armed: the input is heard");
    rig.track.setArm (false, nullptr);
    check (level() < 1.0e-6, "auto, not armed: silent");
    rig.track.setMonitor ("in", nullptr);
    check (std::abs (level() - 0.1f) < 1.0e-3, "in: always heard");
    rig.track.setMonitor ("off", nullptr);
    rig.track.setArm (true, nullptr);
    check (level() < 1.0e-6, "off: never heard");
    auto* rt = rig.f.engine.findRuntime (rig.track.id());
    check (rt != nullptr && rt->inputMeter.load() > 0.09f, "the input meter moves without monitoring or recording");
    check (rt != nullptr && ! rt->inputClipped.load(), "no red mark at -20 dBFS");
    rig.signal = [] (int, int64_t) { return 0.97f; };
    rig.blocks_ (1);
    check (rt != nullptr && rt->inputClipped.load(), "above -0.5 dBFS the meter turns red");

    rig.track.setInput ("audio:1", nullptr); // mono, second input
    rig.track.setMonitor ("in", nullptr);
    rig.signal = [] (int ch, int64_t) { return ch == 1 ? 0.2f : 0.0f; };
    check (std::abs (level() - 0.2f) < 1.0e-3 && std::abs (rig.out.getMagnitude (1, 0, kBlock) - 0.2f) < 1.0e-3,
           "a mono input sounds on both sides");
    rig.track.setInput ("resample", nullptr);
    check (level() < 1.0e-6, "resampling is never monitored (it would feed back)");
}

void testRecordSourceAudio()
{
    std::printf ("[5.3] a MIDI track whose record source is \"audio\" records its instrument into an audio clip\n");
    SoundRig rig;
    auto midi = rig.f.model.addTrack ("midi");
    rig.f.model.addDevice (midi, "prisma", "Prisma").setStateBase64 (prismaState (0), nullptr);
    midi.setRecordSource ("audio", nullptr);
    rig.f.engine.rebuildGraphIfNeeded();
    rig.f.model.setRecordFixedBars (1);
    check (rig.recorder.sound().recordsSound (midi), "the track records sound");
    check (rig.recorder.startRecording (midi.id(), 0), "recording starts");
    check (rig.runUntil ([&] { return rig.blocks > 10 && ! rig.recorder.isTrackRecording (midi.id()); }), "committed");
    const auto clip = midi.clipAt (0);
    check (clip.isAudio() && std::abs (clip.sampleStart()) < 0.5, "an audio clip with no latency to take off");
}

void testLatencyTest()
{
    std::printf ("[5.4] loopback test: a cable from output to input measures the round trip and sets the correction\n");
    SoundRig rig (100, 71);
    constexpr int kTrip = 1371;
    std::vector<float> history;
    rig.signal = [&history] (int, int64_t s) { return s - kTrip >= 0 && s - kTrip < (int64_t) history.size() ? history[(size_t) (s - kTrip)] : 0.0f; };
    rig.afterBlock = [&] {
        const int64_t end = rig.f.engine.getEngineSample();
        history.resize ((size_t) end, 0.0f);
        for (int i = 0; i < kBlock; ++i)
            history[(size_t) (end - kBlock + i)] = rig.out.getSample (0, i);
    };
    rig.block();
    check (rig.f.engine.startLatencyTest (0), "the test starts");
    check (rig.runUntil ([&] { return rig.f.engine.isLatencyTestDone(); }, 20000), "it finishes");
    const auto r = rig.f.engine.takeLatencyTestResult();
    check (r.ok && std::abs (r.roundTripSamples - kTrip) <= 1, "round trip " + juce::String (r.roundTripSamples) + " samples");
    check (std::abs (r.correctionMs - (kTrip - (100 + 71 + 2 * kBlock)) * 1000.0 / kRate) < 0.05, "correction = measured - driver");
    rig.f.engine.setAudioLatencyCorrectionMs (r.correctionMs);
    check (std::abs (rig.f.engine.getAudioRecordLatency() - kTrip) <= 3, "recordings now move by the measured round trip");
}

// Spec test 9: four stereo tracks record onto the timeline for a minute without a single allocation on the audio thread.
void testRealTime()
{
    std::printf ("[5.4] spec test 9: four stereo tracks record for a minute, no heap use on the audio thread\n");
    SoundRig rig (0, 0, 8);
    for (int i = 1; i < 4; ++i)
    {
        auto t = rig.f.model.addTrack ("audio");
        t.setInput ("audio:" + juce::String (2 * i) + "-" + juce::String (2 * i + 1), nullptr);
        t.setArm (true, nullptr);
    }
    rig.f.engine.rebuildGraphIfNeeded();
    rig.signal = [] (int ch, int64_t s) { return (float) std::sin ((double) s * 0.003 * (ch + 1)) * 0.2f; };
    check (rig.recorder.arrangement().start(), "recording starts");
    rig.block();
    daw::rt::resetViolations();
    rig.runUntil ([&] { return rig.f.engine.getPositionBeats() >= 120.0; }); // a minute at 120 BPM
    const auto violations = daw::rt::violationCount();
    rig.recorder.arrangement().stop();
    check (rig.runUntil ([&] { return ! rig.recorder.arrangement().isActive(); }), "the recording was written");
    check (! daw::rt::enabled || violations == 0, "allocations on the audio thread: " + juce::String (violations));
    int clips = 0;
    for (const auto& t : rig.f.model.tracks())
        clips += (int) t.arrangementClips().size();
    check (clips == 4, "a clip on each of the four tracks, got " + juce::String (clips));
}
} // namespace

namespace tests
{
void runAudioRecordingTests()
{
    testStreamGaps();
    testSlotRecordingOnBeats();
    testOverflow();
    testArrangementTakes();
    testMonitoring();
    testRecordSourceAudio();
    testLatencyTest();
    testRealTime();
}
} // namespace tests

namespace
{
// Stage 6 in the app: a built-in effect on a track is a DEVICE kind="fx" that the engine plays and saves.
void testBuiltInEffectDevice()
{
    std::printf ("[6.1] a built-in effect on a track: made by the engine, changes the sound, keeps its parameters\n");
    SoundRig rig;
    rig.track.setMonitor ("in", nullptr);
    auto device = rig.f.model.addDevice (rig.track, "fx", "Utility");
    device.setPluginDesc ("limiter", nullptr);
    rig.f.engine.rebuildGraphIfNeeded();
    auto* p = rig.f.engine.findProcessor (device.id());
    check (p != nullptr && p->getName().isNotEmpty(), "the engine made the limiter");
    if (p == nullptr)
        return;
    for (auto* param : p->getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param); ranged != nullptr && ranged->paramID == "ceiling")
            ranged->setValueNotifyingHost (ranged->convertTo0to1 (-6.0f));
    rig.signal = [] (int, int64_t s) { return 0.9f * (float) std::sin ((double) s * 0.05); };
    rig.blocks_ (200);
    check (rig.out.getMagnitude (0, 0, kBlock) < 0.53f, "the limiter holds the input at -6 dB: " + juce::String (rig.out.getMagnitude (0, 0, kBlock)));
    juce::MemoryBlock state;
    p->getStateInformation (state);
    rig.f.engine.captureDeviceStates (device.state);
    check (device.stateBase64().isNotEmpty(), "the state goes into the model");
    rig.f.model.removeDevice (device);
    rig.f.engine.rebuildGraphIfNeeded();
    rig.f.model.undo.clearUndoHistory();
}
} // namespace

namespace tests
{
void runBuiltInEffectTests() { testBuiltInEffectDevice(); }
} // namespace tests
