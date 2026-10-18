// Stage 7 of docs/spec/07-warp.md, tasks 7.1 and 7.2 in the app: the tempo of an imported file is found in the background
// and its clips start following the project tempo; a warped clip plays a stretched copy (same pitch, project tempo),
// the original resampled until the copy is ready; export waits for the copies; older projects keep their loops in time.
// The analysis and the stretcher have their own tests in core/tests (daw_analysis_tests, daw_stretch_tests).
#include "TestSupport.h"

#include "../app/Source/engine/OfflineRenderer.h"
#include "../app/Source/engine/RtCheck.h"
#include "../app/Source/model/ArrangementEdit.h"
#include "../app/Source/project/AudioImport.h"
#include "../app/Source/project/ProjectFile.h"

#include <cmath>

using namespace tests;

namespace
{
constexpr double kRate = 48000.0;
constexpr double kPi = juce::MathConstants<double>::pi;

std::vector<float> sine (int frames, double hz, float amp = 0.5f)
{
    std::vector<float> s ((size_t) frames);
    for (int i = 0; i < frames; ++i)
        s[(size_t) i] = amp * (float) std::sin (2.0 * kPi * hz * i / kRate);
    return s;
}

// A drum loop with no tempo in its name: a kick on every beat, a hat on every off-beat, `bars` bars after `leadIn` s.
std::vector<float> drumLoop (double bpm, int bars, double leadIn)
{
    const double beat = 60.0 / bpm;
    const auto lead = (size_t) (leadIn * kRate);
    std::vector<float> s (lead + (size_t) (bars * 4 * beat * kRate), 0.0f);
    juce::Random rnd (7);
    for (int b = 0; b < bars * 4; ++b)
    {
        const auto at = lead + (size_t) (b * beat * kRate);
        for (size_t i = 0; i < (size_t) (0.15 * kRate) && at + i < s.size(); ++i)
        {
            const double t = (double) i / kRate;
            s[at + i] += 0.8f * (float) (std::exp (-t * 30.0) * std::sin (2.0 * kPi * (50.0 + 120.0 * std::exp (-t * 40.0)) * t));
        }
        const auto hat = at + (size_t) (beat * 0.5 * kRate);
        for (size_t i = 0; i < (size_t) (0.03 * kRate) && hat + i < s.size(); ++i)
            s[hat + i] += 0.25f * (float) std::exp (-(double) i / kRate * 150.0) * (rnd.nextFloat() * 2.0f - 1.0f);
    }
    return s;
}

// The frequency of a steady tone, from its rising zero crossings.
double frequency (const std::vector<float>& x, size_t from, size_t to)
{
    int crossings = 0;
    size_t first = 0, last = 0;
    for (size_t i = std::max<size_t> (from, 1); i < std::min (to, x.size()); ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            if (crossings == 0)
                first = i;
            last = i;
            ++crossings;
        }
    return crossings > 1 ? (crossings - 1) * kRate / (double) (last - first) : 0.0;
}

// Lets the background jobs finish and their answers arrive on the message thread.
void settle (AudioEngine& engine)
{
    engine.getWarp().waitForJobs();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
}

struct WarpRig
{
    WarpRig() : dev (512, kRate), out (2, 512)
    {
        folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("daw-warp-test-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        folder.createDirectory();
        f.engine.setProjectFolder (folder.getChildFile ("Song Project"));
        f.engine.getProjectFolder().createDirectory();
        for (int i = 0; i < 2; ++i)
            f.model.insertScene (-1);
        track = f.model.addTrack ("audio");
        f.engine.audioDeviceAboutToStart (&dev);
        f.engine.rebuildGraphIfNeeded();
        f.model.undo.clearUndoHistory();
    }
    ~WarpRig()
    {
        f.engine.audioDeviceStopped();
        folder.deleteRecursively();
    }
    void run (int64_t samples)
    {
        for (int64_t done = 0; done < samples; done += 512)
        {
            f.engine.audioDeviceIOCallbackWithContext (nullptr, 0, out.getArrayOfWritePointers(), 2, 512, {});
            left.insert (left.end(), out.getReadPointer (0), out.getReadPointer (0) + 512);
        }
    }

    Fixture f;
    FakeDevice dev;
    juce::AudioBuffer<float> out;
    juce::File folder;
    TrackRef track;
    std::vector<float> left;
};

void testImportAnalysis()
{
    std::printf ("[7.2] an imported loop without a tempo in its name: the tempo is found in the background, its clip follows\n");
    WarpRig r;
    const auto file = r.folder.getChildFile ("groove.wav");
    check (writeWav (file, drumLoop (100.0, 4, 0.25)), "test file written");
    r.f.model.undo.beginNewTransaction ("import");
    const auto imported = audioimport::importFile (r.f.model, r.f.engine, file, true);
    check (imported.ok && imported.fileTempo <= 0.0, "the name gives no tempo");
    auto clip = r.f.model.createAudioClip (r.track, 0, imported.fileId, imported.lengthBeats, imported.name);
    check (! clip.followTempo(), "until the analysis answers, the clip plays as it is");
    const int undoSteps = r.f.model.undo.getNumActionsInCurrentTransaction();

    settle (r.f.engine);
    const auto node = r.f.model.findAudioFile (imported.fileId);
    check (node.analysed() && std::abs (node.tempo() - 100.0) < 0.5, "the file tempo is found: " + juce::String (node.tempo(), 3));
    check (std::abs (node.firstBeatSeconds() - 0.25) < 0.015, "and its first beat after the lead-in: " + juce::String (node.firstBeatSeconds(), 4));
    check (clip.followTempo() && std::abs (clip.originalTempo() - node.tempo()) < 1.0e-9, "the clip now follows the project tempo");
    check (clip.warpMode() == "beats", "a four-bar loop is stretched as drums");
    check (std::abs (clip.lengthBeats() - 16.0) < 0.1, "and is as long as its four bars: " + juce::String (clip.lengthBeats(), 3));
    check (std::abs (clip.sampleStart() - 0.25 * kRate) < 0.015 * kRate, "and starts on the first beat");
    check (r.f.model.undo.getNumActionsInCurrentTransaction() == undoSteps, "the analysis adds nothing to undo");

    // A second clip of the same file, made after the answer, gets the tempo right away.
    auto second = r.f.model.createAudioClip (r.track, 1, imported.fileId, 16.0, imported.name);
    check (second.followTempo() && second.warpMode() == "beats", "a later clip of the file follows from the start");
}

void testStretchedPlayback()
{
    std::printf ("[7.1] a warped clip keeps its pitch at another tempo: resampled at first, stretched once the copy is ready\n");
    WarpRig r;
    r.f.model.setTempo (100.0);
    const auto file = r.folder.getChildFile ("tone_120bpm.wav");
    check (writeWav (file, sine ((int) (16 * kRate), 440.0)), "test file written"); // 32 beats at 120 BPM
    r.f.model.undo.beginNewTransaction ("import");
    const auto imported = audioimport::importFile (r.f.model, r.f.engine, file, true);
    auto clip = r.f.model.createAudioClip (r.track, 0, imported.fileId, imported.lengthBeats, imported.name);
    check (clip.followTempo() && std::abs (clip.originalTempo() - 120.0) < 1.0e-9, "the tempo comes from the name");
    clip.setWarpMode ("complex", nullptr);
    r.f.engine.rebuildGraphIfNeeded();
    r.f.engine.getAudioPool().waitForLoads();
    r.f.engine.rebuildGraph(); // the file is loaded: the stretch starts now

    r.f.engine.launchClip (r.track.id(), 0);
    daw::rt::resetViolations();
    r.run (2 * 48000);
    const double before = frequency (r.left, 24000, 96000);
    check (std::abs (before - 440.0 * 100.0 / 120.0) < 2.0, "meanwhile it is resampled to the project tempo: " + juce::String (before, 1) + " Hz");

    settle (r.f.engine);
    check (r.f.engine.isGraphDirty(), "a ready copy asks for a new graph");
    r.f.engine.rebuildGraphIfNeeded();
    r.left.clear();
    r.run (2 * 48000);
    const double after = frequency (r.left, 24000, 96000);
    check (std::abs (after - 440.0) < 2.0, "the stretched copy keeps the pitch: " + juce::String (after, 1) + " Hz");
    check (daw::rt::violationCount() == 0, "no heap use on the audio thread");

    // Re-Pitch: no copy, the pitch follows the speed.
    clip.setWarpMode ("repitch", nullptr);
    r.f.engine.rebuildGraphIfNeeded();
    r.left.clear();
    r.run (2 * 48000);
    const double repitch = frequency (r.left, 24000, 96000);
    check (std::abs (repitch - 440.0 * 100.0 / 120.0) < 2.0, "Re-Pitch changes the pitch with the speed: " + juce::String (repitch, 1) + " Hz");
    r.f.engine.stop();
    r.run (2048);
}

void testWarpedExport()
{
    std::printf ("[7.1] export renders the stretched copies itself: a warped clip has its pitch from the first sample\n");
    WarpRig r;
    r.f.model.setTempo (90.0);
    const auto file = r.folder.getChildFile ("tone_120bpm.wav");
    writeWav (file, sine ((int) (8 * kRate), 440.0));
    r.f.model.undo.beginNewTransaction ("import");
    const auto imported = audioimport::importFile (r.f.model, r.f.engine, file, true);
    auto node = r.f.model.makeAudioClipNode (r.track, imported.fileId, imported.lengthBeats, imported.name);
    auto a = arrangement::addSlotClip (r.f.model, r.track, ClipRef (node), 0.0);
    a.clip().setWarpMode ("tones", nullptr);
    a.setLength (8.0, nullptr);

    OfflineRenderer::Options o;
    o.startBeat = 0.0;
    o.endBeat = 8.0;
    o.file = r.folder.getChildFile ("warped.wav");
    o.tail = false;
    OfflineRenderer renderer (r.f.settings, r.f.model, r.f.engine, o);
    const auto result = renderer.renderNow();
    check (result.ok, "the file was written: " + result.error);
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (o.file.createInputStream().release(), true));
    check (reader != nullptr && reader->lengthInSamples == (juce::int64) (8 * 60.0 / 90.0 * kRate + 0.5), "8 beats at 90 BPM");
    if (reader == nullptr)
        return;
    juce::AudioBuffer<float> buffer (1, (int) reader->lengthInSamples);
    reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, false);
    std::vector<float> x (buffer.getReadPointer (0), buffer.getReadPointer (0) + buffer.getNumSamples());
    const double hz = frequency (x, 4800, x.size() - 4800);
    check (std::abs (hz - 440.0) < 2.0, "the export keeps the pitch: " + juce::String (hz, 1) + " Hz");
}

void testOldProjectMigration()
{
    std::printf ("[7] schema 4: a clip saved before warp that followed the tempo of its file keeps following it\n");
    auto model = ProjectModel::createEmpty();
    auto track = model.addTrack ("audio");
    model.insertScene (-1);
    ProjectModel::AudioFileInfo info;
    info.name = "loop";
    info.path = "Samples/loop.wav";
    info.sampleRate = kRate;
    info.channels = 1;
    info.lengthSamples = (juce::int64) (8 * kRate);
    info.tempo = 120.0;
    const auto file = model.addAudioFile (info);
    auto clip = model.createAudioClip (track, 0, file.id(), 16.0, "loop");
    clip.state.removeProperty (ids::originalTempo, nullptr); // as schema 3 wrote it: just warp="1"
    clip.state.removeProperty (ids::warpMode, nullptr);
    model.root.setProperty (ids::schemaVersion, 3, nullptr);
    const auto result = project::parseProject (model.root.toXmlString());
    check (result.ok(), "a schema 3 project opens");
    const auto loaded = TrackRef (result.tree.getChildWithName (ids::TRACKS).getChild (0)).clipAt (0);
    check (loaded.followTempo() && std::abs (loaded.originalTempo() - 120.0) < 1.0e-9, "its clip follows the project tempo at the tempo of its file");
    check (loaded.warpMode() == "beats", "and is stretched, a short loop as drums");
}
} // namespace

void runWarpTests()
{
    testImportAnalysis();
    testStretchedPlayback();
    testWarpedExport();
    testOldProjectMigration();
}
