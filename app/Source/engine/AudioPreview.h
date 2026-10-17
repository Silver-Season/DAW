#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "daw/engine/AudioClip.h"

#include <atomic>
#include <memory>
#include <vector>

// Pre-listening of sound files in the browser (docs/spec/05-audio-recording.md, task 5.6): the selected file plays once
// from its start into the master, resampled to the device rate. Switching files fades the old one out in 10 ms.
//
// The message thread hands a decoded source over through an atomic pointer; it keeps the source alive until the audio
// thread has confirmed that it moved on (the serial it saw), so nothing is ever freed on the audio thread.
class AudioPreview
{
public:
    // Message thread
    void play (std::shared_ptr<const daw::engine::AudioSource>);
    void stop();
    void setGain (float g) { gain.store (g); }
    bool isSounding() const { return sounding.load(); }

    // Audio thread: adds to out (2 channels).
    void process (juce::AudioBuffer<float>& out, int numSamples, double engineRate) noexcept;

private:
    struct Voice
    {
        const daw::engine::AudioSource* source = nullptr;
        double position = 0.0; // file frames
        float level = 0.0f;
        bool fading = false;
    };
    void renderVoice (Voice&, juce::AudioBuffer<float>& out, int numSamples, double engineRate, float target) noexcept;

    struct Held
    {
        std::shared_ptr<const daw::engine::AudioSource> source;
        uint32_t serial = 0;
    };
    std::vector<Held> held; // message thread: the current source and those the audio thread may still read
    std::atomic<const daw::engine::AudioSource*> requested { nullptr };
    std::atomic<uint32_t> requestSerial { 0 }, seenSerial { 0 };
    std::atomic<float> gain { 0.25f };
    std::atomic<bool> sounding { false };

    // audio thread
    Voice current, previous;
    uint32_t handledSerial = 0;
};
