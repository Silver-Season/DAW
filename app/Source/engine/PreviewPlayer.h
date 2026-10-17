#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <memory>

class PrismaProcessor;

// Pre-listening of Prisma presets in the browser (docs/spec/01-layout-project.md, task 1.5): a Prisma that is
// not on any track plays a two-bar C minor arpeggio at the project tempo into the master.
//
// Two instances take turns. A new phrase starts on the idle one while the previous one fades out in about
// 10 ms, so that switching presets quickly never clicks and the old preset's release tail is not played with
// the new preset's parameters.
class PreviewPlayer
{
public:
    PreviewPlayer();
    ~PreviewPlayer();

    // Message thread. Allocates the instances; call when the audio device starts.
    void prepare (double sampleRate, int maxBlockSize);
    void release();

    // Message thread. Loads the preset into the idle instance and asks the audio thread to start the phrase.
    void playFactoryProgram (int program);
    void playState (const void* data, int size);
    void stop(); // fades whatever plays out
    void setGainDb (float db) { gain.store (db <= -60.0f ? 0.0f : juce::Decibels::decibelsToGain (db)); }

    // Audio thread: adds the preview to `out` (2 channels).
    void process (juce::AudioBuffer<float>& out, int numSamples, double bpm);

    bool isSounding() const { return sounding.load(); }

    static constexpr double kPhraseBeats = 8.0; // two bars of 4/4

private:
    struct Note
    {
        double start, length;
        int pitch;
    };
    struct Voice
    {
        std::unique_ptr<PrismaProcessor> synth;
        juce::AudioBuffer<float> buffer;
        juce::MidiBuffer midi;
        double beat = 0.0;      // position inside the phrase
        double tailLeft = 0.0;  // seconds of release still to render after the phrase
        float level = 0.0f;     // current fade gain
        bool running = false;
        bool fadingOut = false;
    };

    void prepareVoice (Voice&);
    void request (int voice);
    void startPhrase (Voice&);
    void renderVoice (Voice&, juce::AudioBuffer<float>& out, int numSamples, double bpm, float userGain, float userGainStart);

    std::array<Voice, 2> voices;
    std::array<Note, 16> phrase;
    double sampleRate = 44100.0;
    int maxBlock = 512;
    float lastUserGain = 0.0f;

    int idleVoice = 0;                  // message thread: the voice the next phrase goes to
    std::atomic<int> requestedVoice { -1 }; // -1: nothing / stop
    std::atomic<uint32_t> requestSerial { 0 };
    uint32_t seenSerial = 0;            // audio thread
    std::atomic<float> gain { 0.25f };  // -12 dB
    std::atomic<bool> sounding { false };
};
