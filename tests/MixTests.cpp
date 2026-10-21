// The mix round after the demo song (docs/demo/README.md): the master seen as a track with its chain and automation,
// built-in effects dropped on the master, and the defaults an envelope opens with.
#include "TestSupport.h"
#include "../app/Source/Strings.h"
#include "../app/Source/engine/RtCheck.h"

#include "../app/Source/browser/BrowserActions.h"
#include "../app/Source/library/UserLibrary.h"
#include "../app/Source/model/Automation.h"
#include "../app/Source/engine/DrumKitProcessor.h"
#include "../app/Source/engine/BuiltInEffectProcessor.h"
#include "../app/Source/engine/OfflineRenderer.h"
#include "../app/Source/model/ArrangementEdit.h"
#include "daw/dsp/Loudness.h"
#include "daw/drums/DrumSynth.h"
#include "daw/synth/PrismaParams.h"
#include <cmath>

using namespace tests;
namespace
{
struct Lvl { double peak = 0, rms = 0; };
Lvl renderProc (juce::AudioProcessor& p, const std::vector<std::pair<int,int>>& notes, double seconds, int noteLenSamples)
{
    const double rate = 48000.0; const int block = 512;
    p.setPlayConfigDetails (0, 2, rate, block);
    p.prepareToPlay (rate, block);
    juce::AudioBuffer<float> buf (2, block);
    const int total = (int) (seconds * rate);
    double sum = 0; double peak = 0; long n = 0;
    for (int s = 0; s < total; s += block)
    {
        juce::MidiBuffer midi;
        for (auto [at, note] : notes)
        {
            if (at >= s && at < s + block) midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), at - s);
            if (at + noteLenSamples >= s && at + noteLenSamples < s + block) midi.addEvent (juce::MidiMessage::noteOff (1, note), at + noteLenSamples - s);
        }
        buf.clear();
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < block; ++i) { const double v = buf.getSample (c, i); sum += v * v; peak = std::max (peak, std::abs (v)); ++n; }
    }
    return { 20 * std::log10 (peak + 1e-12), 10 * std::log10 (sum / n + 1e-20) };
}
}
void testDrumLevels()
{
    std::printf ("[mix] a factory drum groove peaks near -6 dBFS with the faders at 0 dB, in the range of the synth presets\n");
    for (const auto& kit : daw::drums::factoryKits())
    {
        DrumKitProcessor d;
        juce::MemoryBlock mb;
        mb.fromBase64Encoding (DrumKitProcessor::factoryStateBase64 (kit.name));
        d.setStateInformation (mb.getData(), (int) mb.getSize());
        std::vector<std::pair<int, int>> groove; // kick on every beat, snare on 2 and 4, eighth hats, at 120 BPM
        for (int b = 0; b < 8; ++b)
        {
            groove.push_back ({ b * 24000, 36 });
            if (b % 2 == 1)
                groove.push_back ({ b * 24000, 38 });
            groove.push_back ({ b * 24000, 42 });
            groove.push_back ({ b * 24000 + 12000, 42 });
        }
        const auto l = renderProc (d, groove, 4.0, 2000);
        check (l.peak < -3.0 && l.peak > -10.0, juce::String (kit.name) + ": groove peak " + juce::String (l.peak, 1) + " dBFS");
        check (l.rms > -26.0 && l.rms < -16.0, juce::String (kit.name) + ": groove RMS " + juce::String (l.rms, 1) + " dB");
    }
    PrismaProcessor p;
    p.setCurrentProgram (0);
    const auto chord = renderProc (p, { { 0, 60 }, { 0, 64 }, { 0, 67 } }, 3.0, 96000);
    check (chord.rms > -30.0 && chord.rms < -14.0, "the first synth preset's chord sits in the same range: " + juce::String (chord.rms, 1) + " dB RMS");
}

namespace
{
constexpr int kBlock = 256;
constexpr double kRate = 48000.0;

// One audio track that monitors input 1 (a constant 0.5 on the left), through the master to the output.
struct MixRig
{
    MixRig() : dev (kBlock, kRate)
    {
        dev.numInputs = 2;
        f.model.insertScene (-1);
        track = f.model.addTrack ("audio");
        track.setInput ("audio:0", nullptr);
        track.setMonitor ("in", nullptr);
        f.engine.audioDeviceAboutToStart (&dev);
        f.engine.rebuildGraphIfNeeded();
        in.setSize (2, kBlock);
        out.setSize (2, kBlock);
        for (int i = 0; i < kBlock; ++i)
            in.setSample (0, i, 0.5f);
        f.model.undo.clearUndoHistory();
    }
    ~MixRig() { f.engine.audioDeviceStopped(); }

    void blocks (int n)
    {
        for (int b = 0; b < n; ++b)
        {
            in.clear (1, 0, kBlock);
            f.engine.audioDeviceIOCallbackWithContext (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, kBlock, {});
        }
    }
    float left() const { return out.getSample (0, kBlock - 1); }

    Fixture f;
    FakeDevice dev;
    TrackRef track;
    juce::AudioBuffer<float> in, out;
};

void testMasterAsTrack()
{
    std::printf ("[mix] the master is a track for the chain and the automation, never for clips\n");
    auto model = ProjectModel::createDefault();
    const auto master = model.findTrackOrMaster (ProjectModel::kMasterTrackId);
    check (master.isValid() && master.isMaster() && master.type() == "master" && ! master.hasInstrument(), "findTrackOrMaster (0) is the master");
    check (! model.findTrack (ProjectModel::kMasterTrackId).isValid(), "findTrack does not find it: clip code never meets the master");
    check (model.findTrackOrMaster (model.trackAt (0).id()) == model.trackAt (0), "other ids are tracks as before");
    check (model.trackAt (0).hasInstrument() && ! model.trackAt (0).isMaster(), "a MIDI track has an instrument");

    // A file saved before the master had an id gets it when loaded.
    auto tree = ProjectModel::createDefault().root.createCopy();
    tree.getChildWithName (ids::MASTER).removeProperty (ids::id, nullptr);
    ProjectModel loaded (tree);
    check (loaded.masterTrack().id() == ProjectModel::kMasterTrackId, "an older file's master gets id 0");
}

void testMasterEffectDrop()
{
    std::printf ("[mix] built-in effects dropped on the master go into its chain; instruments and files do not\n");
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("daw-mix-lib");
    root.deleteRecursively();
    Fixture f;
    library::UserLibrary lib (root);
    browser::BrowserActions actions (f.model, f.engine, lib);
    f.model.insertScene (-1);
    addPrismaTrack (f.model, 0);
    f.model.undo.clearUndoHistory();

    browser::DragItem comp;
    comp.kind = browser::kinds::builtinFx;
    comp.id = "comp";
    comp.name = "Comp";
    comp.instrument = false;
    browser::DragItem prisma;
    prisma.kind = browser::kinds::prisma;
    prisma.name = "Prisma";
    prisma.instrument = true;
    browser::DragItem wav;
    wav.kind = browser::kinds::audioFile;
    wav.path = "/x.wav";

    const auto m = ProjectModel::kMasterTrackId;
    check (actions.accepts (comp, browser::DropTarget::chainGap, m) && actions.accepts (comp, browser::DropTarget::track, m), "an effect is accepted by the master");
    check (! actions.accepts (prisma, browser::DropTarget::track, m) && ! actions.accepts (wav, browser::DropTarget::track, m), "an instrument and a sound file are not");
    const auto r = actions.drop (comp, browser::DropTarget::chainGap, m, -1, 0);
    check (r.ok && f.model.masterTrack().numDevices() == 1 && f.model.masterTrack().device (0).kind() == "fx", "the compressor is in the master chain");
    check (f.model.numTracks() == 1, "no track was made for it");
    const auto r2 = actions.useOnTrack (comp, m);
    check (r2.ok && f.model.masterTrack().numDevices() == 2, "double-click in the browser with the master selected adds to the master");
    f.model.undo.undo();
    check (f.model.masterTrack().numDevices() == 1, "each drop is one undo step");
    root.deleteRecursively();
}

void testMasterAutomation()
{
    std::printf ("[mix] master automation: a volume lane sets the level of the whole mix, a master effect follows its lane\n");
    MixRig rig;
    rig.blocks (4);
    check (std::abs (rig.left() - 0.5f) < 2.0e-3f, "the track reaches the output at its level: " + juce::String (rig.left()));

    auto master = rig.f.model.masterTrack();
    auto lane = automation::getOrAddLane (rig.f.model, master, automation::volumeTarget);
    automation::addPoint (rig.f.model, lane, { 0.0, automation::normalisedVolume (-6.0206f), 0.0f });
    auto fx = rig.f.model.addDevice (rig.f.model.masterDevicesNode(), "fx", "Comp");
    fx.setPluginDesc ("comp", nullptr);
    rig.f.engine.rebuildGraphIfNeeded();
    auto* p = rig.f.engine.findProcessor (fx.id());
    check (p != nullptr, "the master effect has a processor");
    const int outputIndex = p != nullptr ? p->getParameters().size() - 1 : 0;
    auto plane = automation::getOrAddLane (rig.f.model, master, automation::deviceTarget (fx.id(), outputIndex));
    automation::addPoint (rig.f.model, plane, { 0.0, 0.5f, 0.0f });
    automation::addPoint (rig.f.model, plane, { 4.0, 0.5f, 0.0f });
    rig.f.engine.rebuildGraphIfNeeded();

    if (p != nullptr)
        p->getParameters()[outputIndex]->setValue (0.5f);
    rig.blocks (40);
    const float reference = rig.left(); // through the compressor, the lanes not applied while stopped
    check (reference > 0.05f, "stopped, the mix still sounds at the fader level: " + juce::String (reference));
    rig.f.engine.play();
    rig.blocks (8);
    check (std::abs (rig.left() - 0.5f * reference) < 3.0e-3f, "playing, the lane at -6 dB halves the mix: " + juce::String (rig.left()));
    check (p != nullptr && std::abs (p->getParameters()[outputIndex]->getValue() - 0.5f) < 0.01f, "the master compressor's output knob follows its lane");

    // Master pan is a balance: hard right silences the left and leaves the right as it was.
    auto pan = automation::getOrAddLane (rig.f.model, master, automation::panTarget);
    automation::addPoint (rig.f.model, pan, { 0.0, 1.0f, 0.0f });
    rig.f.engine.rebuildGraphIfNeeded();
    rig.blocks (4);
    check (std::abs (rig.left()) < 1.0e-4f, "panned hard right, the left is silent");
    rig.f.engine.stop();
    rig.blocks (2);
}

void testTempoEnvelope()
{
    std::printf ("[mix] the tempo envelope of the master drives the song clock: steps and ramps land where the integral says\n");
    MixRig rig;
    auto& model = rig.f.model;
    model.setTempo (120.0);
    auto lane = automation::getOrAddLane (model, model.masterTrack(), automation::tempoTarget);
    check (std::abs (automation::tempoFromNormalised (automation::normalisedTempo (97.5)) - 97.5) < 1.0e-3, "BPM survives the 0..1 mapping");
    // 120 BPM up to beat 8, then a step to 60.
    automation::addPoint (model, lane, { 0.0, automation::normalisedTempo (120.0), 0.0f });
    automation::addPoint (model, lane, { 8.0, automation::normalisedTempo (120.0), 0.0f });
    automation::addPoint (model, lane, { 8.0, automation::normalisedTempo (60.0), 0.0f });
    rig.f.engine.rebuildGraphIfNeeded();
    rig.f.engine.play();
    const auto runSeconds = [&] (double seconds) { rig.blocks ((int) std::lround (seconds * kRate / kBlock)); };
    runSeconds (2.0);
    check (std::abs (rig.f.engine.getSongPositionBeats() - 4.0) < 0.02, "2 s at 120 BPM: beat 4 (" + juce::String (rig.f.engine.getSongPositionBeats(), 3) + ")");
    check (std::abs (rig.f.engine.getCurrentTempo() - 120.0) < 0.01, "the current tempo is 120");
    runSeconds (4.0);
    check (std::abs (rig.f.engine.getSongPositionBeats() - 10.0) < 0.05, "then 2 s more to beat 8 and 2 s at 60 BPM: beat 10 (" + juce::String (rig.f.engine.getSongPositionBeats(), 3) + ")");
    check (std::abs (rig.f.engine.getCurrentTempo() - 60.0) < 0.01, "the current tempo is 60 after the step");
    rig.f.engine.stop();
    rig.blocks (2);

    // A ramp 120 -> 180 over 16 beats takes 60 * ln (180 / 120) / k seconds, k = 60 / 16 BPM per beat.
    automation::removePointsIn (model, lane, 0.0, 100.0);
    automation::addPoint (model, lane, { 0.0, automation::normalisedTempo (120.0), 0.0f });
    automation::addPoint (model, lane, { 16.0, automation::normalisedTempo (180.0), 0.0f });
    rig.f.engine.rebuildGraphIfNeeded();
    rig.f.engine.play();
    const double rampSeconds = 60.0 * std::log (1.5) / (60.0 / 16.0);
    runSeconds (rampSeconds);
    check (std::abs (rig.f.engine.getSongPositionBeats() - 16.0) < 0.03, "the ramp reaches beat 16 after " + juce::String (rampSeconds, 3) + " s (" + juce::String (rig.f.engine.getSongPositionBeats(), 3) + ")");
    check (std::abs (rig.f.engine.getCurrentTempo() - 180.0) < 0.5, "at 180 BPM (" + juce::String (rig.f.engine.getCurrentTempo(), 2) + ")");
    rig.f.engine.stop();
    rig.blocks (2);

    // Without the envelope the project tempo is back.
    automation::removePointsIn (model, lane, 0.0, 100.0);
    rig.f.engine.rebuildGraphIfNeeded();
    rig.f.engine.play();
    rig.blocks (2);
    check (std::abs (rig.f.engine.getCurrentTempo() - 120.0) < 0.01, "an empty envelope leaves the project tempo");
    rig.f.engine.stop();
    rig.blocks (2);
}
} // namespace

namespace
{
juce::AudioBuffer<float> readFile (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    juce::AudioBuffer<float> b;
    if (reader == nullptr)
        return b;
    b.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&b, 0, (int) reader->lengthInSamples, 0, true, true);
    return b;
}

void testExportStemsMp3Lufs()
{
    std::printf ("[4.3-4.5] export: stems add up to the master, LUFS normalisation, MP3 decodes back, 16-bit dither\n");
    Fixture f;
    FakeDevice dev (512, 48000.0);
    f.engine.audioDeviceAboutToStart (&dev);
    f.model.setTempo (120.0);
    f.model.setName ("Mix Test");
    auto a = addPrismaTrack (f.model, 0);
    auto b = addPrismaTrack (f.model, 1);
    auto muted = addPrismaTrack (f.model, 2);
    muted.setMute (true, nullptr);
    b.setPan (-0.5f, nullptr);
    for (auto* t : { &a, &b, &muted })
    {
        auto clip = arrangement::addEmptyClip (f.model, *t, 0.0, 8.0);
        for (int i = 0; i < 8; ++i)
            clip.clip().addNote ({ (double) i, 0.9, 48 + 7 * (int) (t == &b) + (i % 4) * 2, 100 }, nullptr);
    }
    // a master effect that must not reach the stems
    auto comp = f.model.addDevice (f.model.masterDevicesNode(), "fx", "Comp");
    comp.setPluginDesc ("comp", nullptr);
    comp.setEnabled (false, nullptr); // test 7 compares with the master chain off
    f.engine.rebuildGraphIfNeeded();

    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("daw-mix-export");
    folder.deleteRecursively();
    OfflineRenderer::Options o;
    o.startBeat = 0.0;
    o.endBeat = 8.0;
    o.tail = false;
    o.format = "wav";
    o.bitDepth = 32;
    o.file = folder.getChildFile ("song.wav");
    o.stems = true;
    o.stemsFolder = folder.getChildFile ("stems");
    {
        OfflineRenderer renderer (f.settings, f.model, f.engine, o);
        const auto r = renderer.renderNow();
        check (r.ok && r.stemFiles.size() == 2, "two stems: the muted track is skipped by default (" + juce::String (r.stemFiles.size()) + ") " + r.error);
        check (o.stemsFolder.getChildFile ("01 " + a.name() + ".wav").existsAsFile() && o.stemsFolder.getChildFile ("02 " + b.name() + ".wav").existsAsFile(),
               "named by their place and track name");
        check (o.stemsFolder.findChildFiles (juce::File::findFiles, false, "*.part").isEmpty(), "no unfinished files left");
        const auto master = readFile (o.file);
        const auto s1 = readFile (o.stemsFolder.getChildFile ("01 " + a.name() + ".wav"));
        const auto s2 = readFile (o.stemsFolder.getChildFile ("02 " + b.name() + ".wav"));
        double worst = 0.0;
        bool sameLength = master.getNumSamples() == s1.getNumSamples() && master.getNumSamples() == s2.getNumSamples() && master.getNumSamples() > 0;
        for (int c = 0; sameLength && c < 2; ++c)
            for (int i = 0; i < master.getNumSamples(); ++i)
                worst = std::max (worst, (double) std::abs (master.getSample (c, i) - s1.getSample (c, i) - s2.getSample (c, i)));
        check (sameLength, "every stem is as long as the master");
        check (worst < 1.0e-6, "test 7: the stems add up to the master (master chain off): " + juce::String (worst));
        check (s2.getMagnitude (0, 0, s2.getNumSamples()) > s2.getMagnitude (1, 0, s2.getNumSamples()) * 1.2f, "a stem is after its pan");
    }

    // LUFS: -14 integrated, true peak under -1 dBTP
    o.stems = false;
    o.normaliseLufs = true;
    o.lufsTarget = -14.0;
    o.file = folder.getChildFile ("loud.wav");
    {
        OfflineRenderer renderer (f.settings, f.model, f.engine, o);
        const auto r = renderer.renderNow();
        const auto audio = readFile (o.file);
        const auto m = daw::dsp::measureLoudness (audio.getReadPointer (0), audio.getReadPointer (1), (size_t) audio.getNumSamples(), 48000.0);
        check (r.ok && (std::abs (m.integratedLufs + 14.0) < 0.2 || (r.limitedByPeak && m.integratedLufs < -14.0)),
               "test 6: the file measures -14 LUFS (" + juce::String (m.integratedLufs, 2) + ", limited " + juce::String ((int) r.limitedByPeak) + ")");
        check (m.truePeakDb <= -0.95 && std::abs (r.lufs - m.integratedLufs) < 0.05, "true peak at or under -1 dBTP, and the report says what the file has");
    }

    // MP3 320: decodes back with the length of the song (the LAME tag drops the encoder delay)
    o.normaliseLufs = false;
    o.format = "mp3";
    o.file = folder.getChildFile ("song.mp3");
    {
        OfflineRenderer renderer (f.settings, f.model, f.engine, o);
        const auto r = renderer.renderNow();
        check (r.ok && o.file.getSize() > 100000, "an MP3 of 4 s at 320 kbps: " + juce::String (o.file.getSize()) + " bytes " + r.error);
        juce::MP3AudioFormat mp3;
        std::unique_ptr<juce::AudioFormatReader> reader (mp3.createReaderFor (o.file.createInputStream().release(), true));
        check (reader != nullptr && juce::exactlyEqual (reader->sampleRate, 48000.0) && reader->numChannels == 2, "it decodes as 48 kHz stereo");
        if (reader != nullptr)
            check (std::abs ((double) reader->lengthInSamples - 4.0 * 48000.0) <= 2.0 * 1152.0 + 576.0,
                   "test 3: about the length of the song: " + juce::String (reader->lengthInSamples));
    }

    // 16 bits: silence comes out as dither noise near -96 dBFS (spec test 8)
    for (auto t : f.model.tracks())
        t.setMute (true, nullptr);
    f.engine.rebuildGraphIfNeeded();
    o.format = "wav";
    o.bitDepth = 16;
    o.file = folder.getChildFile ("quiet16.wav");
    {
        OfflineRenderer renderer (f.settings, f.model, f.engine, o);
        renderer.renderNow();
        const auto audio = readFile (o.file);
        const float rms = audio.getRMSLevel (0, 0, audio.getNumSamples());
        const double db = juce::Decibels::gainToDecibels (rms, -200.0f);
        check (db > -103.0 && db < -92.0, "TPDF dither of silence: " + juce::String (db, 1) + " dBFS RMS");
    }
    f.engine.audioDeviceStopped();
    folder.deleteRecursively();
}
void testDistortionState()
{
    std::printf ("[mix] Distortion parameters survive a saved state and expose useful macros\n");
    BuiltInEffectProcessor fx ("distortion");
    auto* dsp = fx.getEffect();
    check (dsp != nullptr && fx.getName() == Strings::fx::effectName ("distortion"), "Distortion processor and translated name");
    if (dsp == nullptr)
        return;
    auto& parameters = fx.getParameters();
    auto* drive = dynamic_cast<juce::RangedAudioParameter*> (parameters[dsp->paramIndex ("drive")]);
    drive->setValueNotifyingHost (drive->convertTo0to1 (27.0f));
    juce::MemoryBlock state;
    fx.getStateInformation (state);
    BuiltInEffectProcessor restored ("distortion");
    restored.setStateInformation (state.getData(), (int) state.getSize());
    const auto& loaded = restored.getParameters();
    check (loaded.size() == parameters.size(), "all effect parameters are restored");
    for (int i = 0; i < loaded.size(); ++i)
        check (std::abs (loaded[i]->getValue() - parameters[i]->getValue()) < 1.0e-6f, "parameter round-trip " + juce::String (i));
    check (restored.defaultMacros().front() == dsp->paramIndex ("mode") && restored.defaultMacros().size() == 7,
           "compact controls expose mode, drive, tone, bias, input, mix and output");
    restored.setPlayConfigDetails (2, 2, 48000, 256);
    restored.prepareToPlay (48000, 256);
    juce::AudioBuffer<float> block (2, 256);
    juce::MidiBuffer midi;
    daw::rt::resetViolations();
    {
        const daw::rt::ScopedAudioThread guard;
        for (int b = 0; b < 32; ++b)
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 256; ++i)
                    block.setSample (c, i, 0.25f * std::sin ((float) (b * 256 + i) * 0.1f));
            restored.processBlock (block, midi);
        }
    }
    check (daw::rt::violationCount() == 0, "Distortion processing uses no heap");
    check (block.getMagnitude (0, 0, 256) > 0.01f && block.getMagnitude (0, 0, 256) < 1.0f, "restored distortion renders bounded audio");
}

void testFactoryPresetLevels()
{
    std::printf ("[mix] every factory preset sounds, does not clip and sits near the others\n");
    const auto& presets = daw::synth::factoryPresets();
    check (presets.size() >= 16, "keys, bells, strings, brass and effects joined the first seven presets");
    for (int i = 0; i < (int) presets.size(); ++i)
    {
        PrismaProcessor p;
        p.setCurrentProgram (i);
        const juce::String name (presets[(size_t) i].name);
        const bool bass = name.containsIgnoreCase ("bass");
        const auto l = bass ? renderProc (p, { { 0, 36 }, { 48000, 43 } }, 3.0, 40000) : renderProc (p, { { 0, 60 }, { 0, 64 }, { 0, 67 } }, 6.0, 240000);
        std::printf ("    %-16s peak %6.1f dBFS, RMS %6.1f dB\n", name.toRawUTF8(), l.peak, l.rms);
        check (l.peak < -0.5, name + " does not clip (" + juce::String (l.peak, 1) + " dBFS)");
        check (l.peak > -24.0 && l.rms > -50.0, name + " is not too quiet");
    }
}
} // namespace

namespace tests
{
void runMixTests()
{
    testDrumLevels();
    testDistortionState();
    testFactoryPresetLevels();
    testMasterAsTrack();
    testMasterEffectDrop();
    testMasterAutomation();
    testTempoEnvelope();
    testExportStemsMp3Lufs();
}
} // namespace tests
