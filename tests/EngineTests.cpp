// Headless end-to-end tests of the JUCE layer: Prisma as a juce::AudioProcessor, the project model, and the
// DAW AudioEngine (model -> playback graph -> clip launch -> instrument -> mixer -> output), driven by a
// fake audio device.
#include "TestSupport.h"

using namespace tests;

namespace
{
void testPrismaProcessor()
{
    std::printf ("[test] Prisma processor: render, programs, state\n");
    PrismaProcessor p;
    p.setRateAndBufferSizeDetails (48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    p.setCurrentProgram (1);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    double energy = 0.0;
    for (int b = 0; b < 50; ++b)
    {
        buffer.clear();
        p.processBlock (buffer, midi);
        midi.clear();
        energy += buffer.getRMSLevel (0, 0, 512);
    }
    check (energy > 0.5, "processBlock produces sound");
    check (p.getProgramName (1) == "Supersaw Lead", "program names come from the factory presets");
    check (std::abs (p.apvts.getRawParameterValue ("oscA_unison")->load() - 7.0f) < 0.01f, "program change sets parameters");

    juce::MemoryBlock state;
    p.getStateInformation (state);
    PrismaProcessor q;
    q.setStateInformation (state.getData(), (int) state.getSize());
    bool same = true;
    for (const auto& d : daw::synth::parameterDescriptors())
        same = same && std::abs (p.apvts.getRawParameterValue (d.id)->load() - q.apvts.getRawParameterValue (d.id)->load()) < 1e-5f;
    check (same, "state round-trips through get/setStateInformation");
    check (p.getParameters().size() == (int) daw::synth::parameterDescriptors().size(), "all parameters exposed to the host");
}

void testEngine()
{
    std::printf ("[test] AudioEngine: model -> clips -> instruments -> mixer\n");
    Fixture f;
    auto& model = f.model;
    auto& engine = f.engine;
    FakeDevice device;

    for (int i = 0; i < 8; ++i)
        model.insertScene (-1);
    const auto track0 = addPrismaTrack (model, 5); // Pluck
    const auto track1 = addPrismaTrack (model, 2); // Reese Bass
    putClip (model, track0, 0, daw::engine::demo::arpClip());
    putClip (model, track1, 0, daw::engine::demo::bassClip (false));
    const auto id0 = track0.id(), id1 = track1.id();

    engine.audioDeviceAboutToStart (&device);
    engine.rebuildGraphIfNeeded();
    const auto* rt0 = engine.findRuntime (id0);
    const auto* rt1 = engine.findRuntime (id1);
    check (rt0 != nullptr && rt1 != nullptr, "the graph has a runtime per model track");

    Runner runner { engine };
    check (runner.run (20) < 1e-6, "silent before anything is launched");

    engine.launchScene (0);
    const double both = runner.run (200);
    std::printf ("    scene 1 RMS %.4f\n", both);
    check (both > 0.01, "scene launch plays both tracks");
    check (engine.isPlaying() && engine.getPositionBeats() > 3.0, "transport started by the launch");
    check (rt0->playingSlot == 0 && rt1->playingSlot == 0, "both tracks report the playing scene");

    track1.setSolo (true, &model.undo);
    runner.run (5);
    const double solo = runner.run (100);
    track1.setMute (true, &model.undo);
    runner.run (5);
    const double soloMuted = runner.run (100);
    std::printf ("    solo bass RMS %.4f, solo+mute %.6f\n", solo, soloMuted);
    check (solo > 0.005 && soloMuted < 1e-5, "solo and mute (live parameters, no graph rebuild)");
    check (! engine.isGraphDirty(), "mixer parameters do not mark the graph as outdated");
    track1.setSolo (false, &model.undo);
    track1.setMute (false, &model.undo);

    engine.stopAllClips();
    runner.run (200); // quantised stop on the next bar, then release tails
    check (rt0->playingSlot == -1 && rt1->playingSlot == -1, "stop all stops at the bar");
    check (runner.run (50) < 1e-4, "silence after stop");

    // Live MIDI from the on-screen keyboard reaches only the armed track.
    track0.setArm (true, nullptr);
    engine.getKeyboardState().noteOn (1, 64, 1.0f);
    const double live = runner.run (20);
    engine.getKeyboardState().noteOff (1, 64, 0.0f);
    std::printf ("    live note RMS %.4f\n", live);
    check (live > 0.005, "on-screen keyboard plays the armed track");
    runner.run (200);

    // Without a manually armed track the selected one is armed automatically.
    track0.setArm (false, nullptr);
    model.setSelectedTrack (id1);
    engine.getKeyboardState().noteOn (1, 40, 1.0f);
    const double autoArm = runner.run (20);
    engine.getKeyboardState().noteOff (1, 40, 0.0f);
    check (autoArm > 0.005, "the selected track is armed when nothing is armed by hand");

    // Stop, then Play: the Session clips that were playing start again (only those).
    runner.run (200);
    engine.launchClip (id0, 0);
    runner.run (250); // the launch waits for the next bar while the transport runs
    check (rt0->playingSlot == 0 && rt1->playingSlot == -1, "only track 1 plays before stop");
    engine.stop();
    runner.run (5);
    check (rt0->playingSlot == -1 && ! engine.isPlaying(), "stop stops the clip and the transport");
    engine.play();
    const double resumed = runner.run (100);
    check (engine.isPlaying() && rt0->playingSlot == 0 && rt1->playingSlot == -1, "play brings back the clip that played at stop");
    check (resumed > 0.005, "the resumed clip sounds");
    engine.stop();
    runner.run (5);
    engine.stopAllClips();
    engine.play();
    runner.run (20);
    check (rt0->playingSlot == 0, "play after stop resumes again");
    engine.stopTrack (id0);
    runner.run (200);
    engine.stop();
    runner.run (5);
    engine.play();
    runner.run (20);
    check (rt0->playingSlot == -1 && engine.isPlaying(), "a clip stopped by hand before stop is not resumed; play still runs the transport");
    engine.stop();
    runner.run (5);

    check (runner.nonFinite == 0, "no NaN or Inf in the output");
    engine.audioDeviceStopped();
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    testPrismaProcessor();
    testEngine();
    runModelTests();
    runGraphTests();
    runHostingTests();
    runUiTests();
    runProjectTests();
    runBrowserTests();
    runEditorTests();
    runRecordingTests();
    runArrangementTests();
    runArrangementUiTests();
    runArrangementRecordingTests();
    runAudioTests();
    runAudioUiTests();
    runSessionAudioUiTests();
    runAudioRecordingTests();
    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
