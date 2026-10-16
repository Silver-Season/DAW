#pragma once

#include "AudioEngine.h"

#include <chrono>

// Drives the engine's audio callback from a timer thread at real-time pace, without any sound card.
// A development aid for machines with no audio hardware (containers, CI): start the app with the
// environment variable DAW_NULL_AUDIO=1 to get a running engine, moving meters and a transport.
// The output is discarded.
class NullAudioDriver final : private juce::HighResolutionTimer
{
public:
    explicit NullAudioDriver (AudioEngine& e, double rate = 48000.0, int block = 256)
        : engine (e), device (rate, block), blockSize (block), left ((size_t) block), right ((size_t) block)
    {
        engine.audioDeviceAboutToStart (&device);
        lastTick = std::chrono::steady_clock::now();
        startTimer (4);
    }

    ~NullAudioDriver() override
    {
        stopTimer();
        engine.audioDeviceStopped();
    }

private:
    class Device final : public juce::AudioIODevice
    {
    public:
        Device (double rate, int block) : AudioIODevice ("Null", "Null"), sampleRate (rate), blockSize (block) {}
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

    private:
        double sampleRate;
        int blockSize;
    };

    void hiResTimerCallback() override
    {
        // Run as many blocks as real time has passed since the last tick.
        const auto now = std::chrono::steady_clock::now();
        owed += std::chrono::duration<double> (now - lastTick).count() * device.getCurrentSampleRate();
        lastTick = now;
        float* outs[2] { left.data(), right.data() };
        while (owed >= blockSize)
        {
            engine.audioDeviceIOCallbackWithContext (nullptr, 0, outs, 2, blockSize, {});
            owed -= blockSize;
        }
    }

    AudioEngine& engine;
    Device device;
    const int blockSize;
    std::vector<float> left, right;
    std::chrono::steady_clock::time_point lastTick;
    double owed = 0.0;
};
