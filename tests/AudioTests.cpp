// Stage 5 of docs/spec/05-audio-recording.md, tasks 5.1, 5.2 and 5.6: the pool of sound files, audio clips playing in
// Session View and in the Arrangement, a file that is still loading, schema 3. Tests 1, 2 and 3 of the spec; the core
// renderer has its own tests in core/tests/audio_tests.cpp.
#include "TestSupport.h"

#include "../app/Source/engine/RtCheck.h"
#include "../app/Source/model/ArrangementEdit.h"
#include "../app/Source/project/AudioImport.h"
#include "../app/Source/project/ProjectFile.h"

#include <cmath>

using namespace tests;

namespace
{
constexpr double kRate = 48000.0;
constexpr double kSpb = 24000.0; // 120 BPM

// Writes a 32-bit float WAV, so the samples come back exactly.
bool writeWav (const juce::File& file, const std::vector<float>& samples, double rate = kRate, int channels = 1)
{
    file.deleteFile();
    file.getParentDirectory().createDirectory();
    std::unique_ptr<juce::OutputStream> out (file.createOutputStream().release());
    if (out == nullptr)
        return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (out.get(), rate, (unsigned int) channels, 32, {}, 0));
    if (writer == nullptr)
        return false;
    out.release(); // the writer owns the stream now
    const int frames = (int) samples.size() / channels;
    juce::AudioBuffer<float> buffer (channels, frames);
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c)
            buffer.setSample (c, i, samples[(size_t) (i * channels + c)]);
    return writer->writeFromAudioSampleBuffer (buffer, 0, frames);
}

std::vector<float> sine (int frames, double hz, float amp = 0.5f, double rate = kRate)
{
    std::vector<float> s ((size_t) frames);
    for (int i = 0; i < frames; ++i)
        s[(size_t) i] = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / rate);
    return s;
}

struct AudioRig
{
    explicit AudioRig (int block = 256) : dev (block, kRate), blockSize (block), out (2, block)
    {
        folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("daw-audio-test-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        folder.createDirectory();
        f.engine.setProjectFolder (folder.getChildFile ("Song Project"));
        f.engine.getProjectFolder().createDirectory();
        for (int i = 0; i < 4; ++i)
            f.model.insertScene (-1);
        track = f.model.addTrack ("audio");
        f.engine.audioDeviceAboutToStart (&dev);
        f.engine.rebuildGraphIfNeeded();
        f.model.undo.clearUndoHistory();
    }
    ~AudioRig()
    {
        f.engine.audioDeviceStopped();
        folder.deleteRecursively();
    }

    // Imports the file and puts a clip of it into the slot. Returns the clip.
    ClipRef sessionClip (const juce::File& file, int scene)
    {
        f.model.undo.beginNewTransaction ("import");
        const auto imported = audioimport::importFile (f.model, f.engine, file, true);
        check (imported.ok, "the file is imported");
        auto clip = f.model.createAudioClip (track, scene, imported.fileId, imported.lengthBeats, imported.name);
        f.engine.rebuildGraphIfNeeded();
        f.engine.getAudioPool().waitForLoads();
        return clip;
    }

    // Runs `samples` samples and appends the left channel to `left`.
    void run (int64_t samples)
    {
        for (int64_t done = 0; done < samples; done += blockSize)
        {
            f.engine.audioDeviceIOCallbackWithContext (nullptr, 0, out.getArrayOfWritePointers(), 2, blockSize, {});
            left.insert (left.end(), out.getReadPointer (0), out.getReadPointer (0) + blockSize);
        }
    }

    Fixture f;
    FakeDevice dev;
    const int blockSize;
    juce::AudioBuffer<float> out;
    juce::File folder;
    TrackRef track;
    std::vector<float> left;
};

float maxStep (const std::vector<float>& x, size_t from, size_t to)
{
    float m = 0.0f;
    for (size_t i = std::max<size_t> (from, 1); i < std::min (to, x.size()); ++i)
        m = std::max (m, std::abs (x[i] - x[i - 1]));
    return m;
}

void testPool()
{
    std::printf ("[audio test 1] pool: one FILE for two copies of a sound, a lost file is found by its hash\n");
    AudioRig r;
    const auto a = r.folder.getChildFile ("Downloads").getChildFile ("loop_124bpm.wav");
    const auto b = r.folder.getChildFile ("Elsewhere").getChildFile ("same loop.wav");
    const auto content = sine (48000, 440.0);
    check (writeWav (a, content) && writeWav (b, content), "test files written");

    r.f.model.undo.beginNewTransaction ("import");
    const auto first = audioimport::importFile (r.f.model, r.f.engine, a, true);
    const auto second = audioimport::importFile (r.f.model, r.f.engine, b, true);
    check (first.ok && second.ok && first.fileId == second.fileId, "the same content is one FILE");
    check (r.f.model.audioFilesNode().getNumChildren() == 1, "exactly one FILE in the pool");
    check (std::abs (first.fileTempo - 124.0) < 1.0e-9, "the tempo comes from the file name");
    check (std::abs (first.lengthBeats - 124.0 / 60.0) < 1.0e-6, "a clip of the loop is as long as the file at its own tempo");

    const auto ref = r.f.model.findAudioFile (first.fileId);
    const auto copy = r.f.engine.resolveAudioFile (ref);
    check (copy.isAChildOf (r.f.engine.getProjectFolder().getChildFile ("Samples").getChildFile ("Imported")) && copy.existsAsFile(),
           "a file from outside is copied into Samples/Imported");
    check (ref.path() == "Samples/Imported/loop_124bpm.wav", "and stored with a path relative to the project");

    // the copy goes away; the original is found again in another folder by its hash
    copy.deleteFile();
    check (! r.f.engine.resolveAudioFile (ref).existsAsFile() && r.f.engine.audioSourceFor (first.fileId) == nullptr, "the file is lost");
    const auto other = r.folder.getChildFile ("Backup Drive").getChildFile ("renamed.wav");
    writeWav (other, content);
    r.f.model.undo.beginNewTransaction ("relink");
    check (audioimport::relinkMissing (r.f.model, r.f.engine, r.folder.getChildFile ("Backup Drive")) == 1, "one file is found");
    check (r.f.engine.resolveAudioFile (ref) == other && r.f.engine.audioSourceFor (first.fileId) != nullptr, "and plays from its new place");

    check (audioimport::unusedFiles (r.f.model).size() == 1, "a FILE without clips is unused");
    r.f.model.createAudioClip (r.track, 0, first.fileId, 4.0, "loop");
    check (audioimport::unusedFiles (r.f.model).isEmpty(), "with a clip it is used");
}

void testSessionPlayback()
{
    std::printf ("[audio test 2] a one-bar 1 kHz loop plays in Session View for eight passes without a click\n");
    AudioRig r (331); // a block size that does not divide the bar
    const auto file = r.folder.getChildFile ("sine.wav");
    const auto content = sine (96000, 1000.0); // one bar at 120 BPM: 2000 whole periods
    writeWav (file, content);
    auto clip = r.sessionClip (file, 0);
    check (std::abs (clip.lengthBeats() - 4.0) < 1.0e-9, "the clip is one bar long");

    r.run (331 * 2);
    const size_t origin = r.left.size();
    r.f.engine.launchClip (r.track.id(), 0); // stopped transport: starts now
    r.run (8 * 96000 + 2000);
    // the launch lands on the first sample of the next block
    size_t start = origin;
    while (start < r.left.size() && juce::exactlyEqual (r.left[start], 0.0f) && juce::exactlyEqual (r.left[start + 1], 0.0f))
        ++start;
    check (start - origin < 331 * 2, "the clip starts right away");
    bool exact = true;
    for (size_t i = 400; i < 90000; i += 997)
        exact = exact && std::abs (r.left[start + i] - content[i]) < 1.0e-6f;
    check (exact, "the output is the file, sample for sample (unity gain, centre pan)");
    const float inside = maxStep (r.left, start + 1000, start + 90000);
    float seams = 0.0f;
    for (int k = 1; k < 8; ++k)
        seams = std::max (seams, maxStep (r.left, start + (size_t) k * 96000 - 200, start + (size_t) k * 96000 + 200));
    check (seams <= inside * 1.5f, "the seams step no more than 1.5x the sine itself: " + juce::String (seams) + " / " + juce::String (inside));
    check (r.f.engine.getDiskMisses() == 0, "no disk misses");

    // gain and transpose reach the engine
    r.f.model.undo.beginNewTransaction ("gain");
    clip.setGainDb (-6.0206, &r.f.model.undo);
    r.f.engine.rebuildGraphIfNeeded();
    r.left.clear();
    r.run (96000);
    float peak = 0.0f;
    for (auto s : r.left)
        peak = std::max (peak, std::abs (s));
    check (std::abs (peak - 0.25f) < 0.002f, "-6 dB halves the level: " + juce::String (peak));
    r.f.engine.stop();
    r.run (331 * 2);
}

void testArrangementPlayback()
{
    std::printf ("[audio test 2b] an arrangement clip plays from its offset, with fades at the cuts\n");
    AudioRig r (256);
    const auto file = r.folder.getChildFile ("ramp.wav");
    std::vector<float> content (96000 * 2);
    for (size_t i = 0; i < content.size(); ++i)
        content[i] = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 100.0 * (double) i / kRate);
    writeWav (file, content);
    r.f.model.undo.beginNewTransaction ("place");
    const auto imported = audioimport::importFile (r.f.model, r.f.engine, file, true);
    auto node = r.f.model.makeAudioClipNode (r.track, imported.fileId, imported.lengthBeats, "ramp");
    auto a = arrangement::addSlotClip (r.f.model, r.track, ClipRef (node), 4.0);
    a.setLength (4.0, &r.f.model.undo);
    check (a.isValid() && a.clip().isAudio(), "an arrangement clip holds the audio clip");
    a.setOffset (2.0, &r.f.model.undo); // enters the file one second in
    r.f.engine.rebuildGraphIfNeeded();
    r.f.engine.getAudioPool().waitForLoads();

    r.run (512);
    const size_t origin = r.left.size();
    r.f.engine.play();
    r.run ((int64_t) (10 * kSpb));
    const size_t clipStart = origin + (size_t) (4 * kSpb);
    check (juce::exactlyEqual (r.left[clipStart - 10], 0.0f), "silent before the clip");
    bool exact = true;
    for (size_t i = 1000; i < (size_t) (3.9 * kSpb); i += 1009)
        exact = exact && std::abs (r.left[clipStart + i] - content[48000 + i]) < 1.0e-6f;
    check (exact, "the clip plays the file from its offset (one second in)");
    check (std::abs (r.left[clipStart]) < 0.01f && maxStep (r.left, clipStart - 5, clipStart + 5) < 0.01f, "a 4 ms fade in at the cut");
    const size_t clipEnd = origin + (size_t) (8 * kSpb);
    check (juce::exactlyEqual (r.left[clipEnd + 10], 0.0f) && std::abs (r.left[clipEnd - 2]) < 0.01f, "a fade out at the end, silence after it");
    r.f.engine.stop();
    r.run (512);
}

void testLoadingFile()
{
    std::printf ("[audio test 3] a long file that loads slowly: the audio thread never waits, misses are counted\n");
    AudioRig r (512);
    const auto file = r.folder.getChildFile ("long.wav");
    writeWav (file, sine (48000 * 120, 220.0, 0.3f)); // two minutes
    r.f.engine.getAudioPool().setReadDelayForTesting (30);
    r.f.model.undo.beginNewTransaction ("import");
    const auto imported = audioimport::importFile (r.f.model, r.f.engine, file, true);
    r.f.model.createAudioClip (r.track, 0, imported.fileId, imported.lengthBeats, imported.name);
    r.f.engine.rebuildGraphIfNeeded();
    r.f.engine.launchClip (r.track.id(), 0);

    double worstMs = 0.0;
    daw::rt::resetViolations();
    for (int b = 0; b < 400; ++b)
    {
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        r.f.engine.audioDeviceIOCallbackWithContext (nullptr, 0, r.out.getArrayOfWritePointers(), 2, 512, {});
        worstMs = std::max (worstMs, juce::Time::getMillisecondCounterHiRes() - t0);
    }
    check (worstMs < 1.0, "no block took 1 ms or more: " + juce::String (worstMs, 3) + " ms");
    check (daw::rt::violationCount() == 0, "no heap use on the audio thread");
    check (r.f.engine.getDiskMisses() > 0, "the blocks ahead of the loader were counted as misses");
    r.f.engine.getAudioPool().setReadDelayForTesting (0);
    r.f.engine.getAudioPool().waitForLoads();
    const auto misses = r.f.engine.getDiskMisses();
    r.left.clear();
    r.run (48000);
    check (r.f.engine.getDiskMisses() == misses, "once loaded, nothing is missed");
    float peak = 0.0f;
    for (auto s : r.left)
        peak = std::max (peak, std::abs (s));
    check (std::abs (peak - 0.3f) < 0.01f, "and it plays");
    r.f.engine.stop();
    r.run (1024);
}

void testPreview()
{
    std::printf ("[audio test] pre-listening of a sound file in the browser\n");
    AudioRig r (256);
    const auto a = r.folder.getChildFile ("a.wav"), b = r.folder.getChildFile ("b.wav");
    writeWav (a, sine (24000, 500.0, 0.5f, 44100.0), 44100.0);
    writeWav (b, sine (48000, 300.0, 0.5f));
    r.f.engine.setPreviewVolumeDb (0.0f);
    check (r.f.engine.previewAudioFile (a), "a readable file previews");
    r.f.engine.getAudioPool().waitForLoads();
    daw::rt::resetViolations();
    r.run (4096);
    float peak = 0.0f;
    for (auto s : r.left)
        peak = std::max (peak, std::abs (s));
    check (std::abs (peak - 0.5f) < 0.02f, "it plays at its level: " + juce::String (peak));
    check (r.f.engine.previewAudioFile (b) && ! r.f.engine.previewAudioFile (r.folder.getChildFile ("none.wav")), "switching, a missing file");
    r.f.engine.getAudioPool().waitForLoads();
    r.left.clear();
    r.run (48000);
    check (daw::rt::violationCount() == 0, "no heap use on the audio thread");
    check (! r.f.engine.isPreviewSounding(), "a missing file stops the preview");
}

void testSchemaAndTracks()
{
    std::printf ("[audio test] schema 3, audio tracks, a saved audio clip loads again\n");
    {
        auto model = ProjectModel::createDefault();
        auto root = model.root.createCopy();
        root.removeChild (root.getChildWithName (ids::AUDIOFILES), nullptr);
        root.setProperty (ids::schemaVersion, 2, nullptr);
        const auto result = project::parseProject (project::serialise (root));
        check (result.ok() && result.tree.getChildWithName (ids::AUDIOFILES).isValid(), "a version 2 file gets an AUDIOFILES pool");
        check ((int) result.tree.getProperty (ids::schemaVersion) == 3, "and version 3");
    }
    auto model = ProjectModel::createEmpty();
    model.insertScene (-1);
    auto t = model.addTrack ("audio");
    check (t.isAudio() && t.input() == "audio:0-1" && t.recordSource() == "audio", "an audio track listens to inputs 1/2 and records sound");
    ProjectModel::AudioFileInfo info;
    info.path = "Samples/Recorded/x.wav";
    info.lengthSamples = 1000;
    const auto f = model.addAudioFile (info);
    auto clip = model.createAudioClip (t, 0, f.id(), 2.0, "x");
    clip.setTranspose (7, &model.undo);
    clip.setReverse (true, &model.undo);
    const auto loaded = project::parseProject (project::serialise (model.root));
    ProjectModel back (loaded.tree);
    const auto c = back.trackAt (0).clipAt (0);
    check (c.isAudio() && c.fileId() == f.id() && c.transpose() == 7 && c.reverse() && back.findAudioFile (f.id()).path() == info.path,
           "the audio clip and its file survive save and load");
    check (std::abs (c.pitchRatio() - std::pow (2.0, 7.0 / 12.0)) < 1.0e-12, "transpose is a resampling ratio");

    // a copy of the clip gets a new id but still plays the same file
    back.undo.beginNewTransaction ("copy");
    back.moveClip (back.trackAt (0).id(), 0, back.trackAt (0).id(), 0, true);
    auto copy = back.setClip (back.trackAt (0), 0, c);
    check (copy.isAudio() && copy.fileId() == f.id() && copy.id() != c.id(), "copies keep the file");
}
} // namespace

void tests::runAudioTests()
{
    testPool();
    testSessionPlayback();
    testArrangementPlayback();
    testLoadingFile();
    testPreview();
    testSchemaAndTracks();
}
