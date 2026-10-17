#pragma once

#include <cstdint>

// The click of the metronome and of the count-in (docs/spec/02-midi-recording.md, task 2.1). Audio thread only:
// no allocation, no locks. The engine mixes it into the master after the master chain and the master gain, so a
// limiter on the master never squeezes it. The offline export (stage 4) must not call it: the click is a live aid
// and never part of a bounce.
class Metronome
{
public:
    enum class Sound
    {
        classic, // a sine with an exponential decay of 30 ms
        wood     // a short burst of noise through a band-pass at 2 kHz
    };

    void prepare (double sampleRate) noexcept;
    void reset() noexcept { active = false; }

    // Adds the clicks of one block to L and R. A click begins exactly at the sample of each whole beat inside the
    // block [blockStartBeat, blockStartBeat + numSamples / samplesPerBeat) and may run on into the next block.
    // The first beat of a bar (a beat number that is a multiple of beatsPerBar; count-in bars are whole bars, so
    // negative beats line up too) is 1500 Hz and 6 dB louder; the others are 1000 Hz. `gain` is the linear volume.
    // Beats may be negative (count-in). `emit` false only lets a running click ring out.
    void render (float* left, float* right, int numSamples, double blockStartBeat, double samplesPerBeat, double beatsPerBar, float gain,
                 Sound sound, bool emit) noexcept;

private:
    void startClick (bool strong, Sound sound, float gain) noexcept;
    float nextSample() noexcept;

    static constexpr double kDecaySeconds = 0.030;
    static constexpr double kBandpassHz = 2000.0;

    double sampleRate = 48000.0;
    bool active = false;
    Sound currentSound = Sound::classic;
    float amplitude = 0.0f;
    double phaseInc = 0.0;  // radians per sample
    int64_t age = 0;        // samples since the click began
    int64_t length = 0;     // samples until the click is inaudible
    double decay = 0.0;     // per-sample multiplier
    double envelope = 1.0;
    // wood: noise generator and band-pass (RBJ biquad, constant 0 dB peak gain)
    uint32_t noiseState = 12345u;
    double b0 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;
};
