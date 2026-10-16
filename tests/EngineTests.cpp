// Headless end-to-end tests of the JUCE layer: Prisma as a juce::AudioProcessor and the DAW AudioEngine
// (clip launch -> instrument -> mixer -> output), driven by a fake audio device.
#include "../app/Source/AudioEngine.h"
#include "../plugins/prisma/Source/PrismaProcessor.h"

#include "daw/engine/DemoContent.h"

#include <cstdio>

namespace
{
int failures = 0, checks = 0;

void check (bool ok, const juce::String& what)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::printf ("  FAIL: %s\n", what.toRawUTF8());
    }
}

class FakeDevice final : public juce::AudioIODevice
{
public:
    FakeDevice() : AudioIODevice ("Fake", "Fake") {}
    juce::StringArray getOutputChannelNames() override { return { "L", "R" }; }
    juce::StringArray getInputChannelNames() override { return {}; }
    juce::Array<double> getAvailableSampleRates() override { return { 48000.0 }; }
    juce::Array<int> getAvailableBufferSizes() override { return { 512 }; }
    int getDefaultBufferSize() override { return 512; }
    juce::String open (const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
    void close() override {}
    bool isOpen() override { return true; }
    void start (juce::AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override { return true; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return 512; }
    double getCurrentSampleRate() override { return 48000.0; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return 3; }
    juce::BigInteger getActiveInputChannels() const override { return 0; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }
};

struct Runner
{
    AudioEngine& engine;
    juce::AudioBuffer<float> out { 2, 512 };

    // Runs n blocks and returns the RMS of the output.
    double run (int blocks)
    {
        double sum = 0.0;
        for (int b = 0; b < blocks; ++b)
        {
            engine.audioDeviceIOCallbackWithContext (nullptr, 0, out.getArrayOfWritePointers(), 2, 512, {});
            for (int i = 0; i < 512; ++i)
                sum += (double) out.getSample (0, i) * out.getSample (0, i);
        }
        return std::sqrt (sum / (blocks * 512.0));
    }
};

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
    std::printf ("[test] AudioEngine: clips -> instruments -> mixer\n");
    AudioEngine engine;
    FakeDevice device;

    for (int t = 0; t < 2; ++t)
    {
        auto prisma = std::make_unique<PrismaProcessor>();
        prisma->setCurrentProgram (t == 0 ? 5 : 2); // Pluck, Reese Bass
        engine.setInstrument (t, std::move (prisma), "Prisma");
    }
    engine.audioDeviceAboutToStart (&device);
    engine.setClip (0, 0, std::make_unique<daw::engine::MidiClip> (daw::engine::demo::arpClip()));
    engine.setClip (1, 0, std::make_unique<daw::engine::MidiClip> (daw::engine::demo::bassClip (false)));

    Runner runner { engine };
    check (runner.run (20) < 1e-6, "silent before anything is launched");

    engine.launchScene (0);
    const double both = runner.run (200);
    std::printf ("    scene 1 RMS %.4f\n", both);
    check (both > 0.01, "scene launch plays both tracks");
    check (engine.isPlaying() && engine.getPositionBeats() > 3.0, "transport started by the launch");
    check (engine.getPlayingScene (0) == 0 && engine.getPlayingScene (1) == 0, "both tracks report the playing scene");

    engine.getTrack (1).solo = true;
    runner.run (5);
    const double solo = runner.run (100);
    engine.getTrack (1).mute = true;
    runner.run (5);
    const double soloMuted = runner.run (100);
    std::printf ("    solo bass RMS %.4f, solo+mute %.6f\n", solo, soloMuted);
    check (solo > 0.005 && soloMuted < 1e-5, "solo and mute");
    engine.getTrack (1).solo = false;
    engine.getTrack (1).mute = false;

    engine.stopAllClips();
    runner.run (200); // quantised stop on the next bar, then release tails
    check (engine.getPlayingScene (0) == -1 && engine.getPlayingScene (1) == -1, "stop all stops at the bar");
    check (runner.run (50) < 1e-4, "silence after stop");

    // Live MIDI from the on-screen keyboard reaches only the armed track.
    engine.armExclusive (0);
    engine.getKeyboardState().noteOn (1, 64, 1.0f);
    const double live = runner.run (20);
    engine.getKeyboardState().noteOff (1, 64, 0.0f);
    std::printf ("    live note RMS %.4f\n", live);
    check (live > 0.005, "on-screen keyboard plays the armed track");

    engine.audioDeviceStopped();
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    testPrismaProcessor();
    testEngine();
    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
