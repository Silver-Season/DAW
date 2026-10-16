// Shared helpers of the JUCE-layer tests: check(), a fake audio device, and a project + engine fixture.
#pragma once

#include "../app/Source/Settings.h"
#include "../app/Source/engine/AudioEngine.h"
#include "../app/Source/model/ProjectModel.h"
#include "../plugins/prisma/Source/PrismaProcessor.h"

#include "daw/engine/DemoContent.h"
#include "daw/synth/PrismaParams.h"

#include <cstdio>

namespace tests
{
inline int failures = 0, checks = 0;

inline void check (bool ok, const juce::String& what)
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
    explicit FakeDevice (int block = 512, double rate = 48000.0) : AudioIODevice ("Fake", "Fake"), blockSize (block), sampleRate (rate) {}
    juce::StringArray getOutputChannelNames() override { return { "L", "R" }; }
    juce::StringArray getInputChannelNames() override { return {}; }
    juce::Array<double> getAvailableSampleRates() override { return { sampleRate }; }
    juce::Array<int> getAvailableBufferSizes() override { return { blockSize }; }
    int getDefaultBufferSize() override { return blockSize; }
    juce::String open (const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
    void close() override {}
    bool isOpen() override { return true; }
    void start (juce::AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override { return true; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return blockSize; }
    double getCurrentSampleRate() override { return sampleRate; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return 3; }
    juce::BigInteger getActiveInputChannels() const override { return 0; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }

    const int blockSize;
    const double sampleRate;
};

// Calls the engine's audio callback by hand, the way a device would.
struct Runner
{
    Runner (AudioEngine& e, int block = 512) : engine (e), blockSize (block), out (2, block) {}

    // Runs n blocks and returns the RMS of the left output channel. Non-finite samples are counted.
    double run (int blocks)
    {
        double sum = 0.0;
        for (int b = 0; b < blocks; ++b)
        {
            engine.audioDeviceIOCallbackWithContext (nullptr, 0, out.getArrayOfWritePointers(), 2, blockSize, {});
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                {
                    const float s = out.getSample (ch, i);
                    if (! std::isfinite (s))
                        ++nonFinite;
                    peak = juce::jmax (peak, std::abs (s));
                }
            for (int i = 0; i < blockSize; ++i)
                sum += (double) out.getSample (0, i) * out.getSample (0, i);
        }
        return std::sqrt (sum / (blocks * (double) blockSize));
    }

    AudioEngine& engine;
    const int blockSize;
    juce::AudioBuffer<float> out;
    int64_t nonFinite = 0;
    float peak = 0.0f;
};

// Base64 state of Prisma with a factory preset loaded, as stored in a DEVICE node.
inline juce::String prismaState (int program)
{
    PrismaProcessor p;
    p.setCurrentProgram (program);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    return state.toBase64Encoding();
}

// Adds a MIDI track with a Prisma preset to the model (inside the caller's transaction).
inline TrackRef addPrismaTrack (ProjectModel& model, int program)
{
    auto track = model.addTrack ("midi");
    auto device = model.addDevice (track, "prisma", "Prisma");
    device.setStateBase64 (prismaState (program), &model.undo);
    return track;
}

inline void putClip (ProjectModel& model, const TrackRef& track, int scene, const daw::engine::MidiClip& source)
{
    auto clip = model.createClip (track, scene, source.lengthBeats, source.name);
    clip.setNotes (source.notes, &model.undo);
}

// A model with an engine attached, settings in a temporary file.
struct Fixture
{
    Fixture()
        : settingsFile (".settings"), settings (settingsFile.getFile()), model (ProjectModel::createEmpty()), engine (settings, false)
    {
        engine.setModel (&model);
    }

    juce::TemporaryFile settingsFile;
    Settings settings;
    ProjectModel model;
    AudioEngine engine;
};

void runModelTests();
void runGraphTests();
void runHostingTests();
void runUiTests();
} // namespace tests
