#pragma once

#include "daw/dsp/Envelope.h"
#include "daw/dsp/Filter.h"
#include "daw/dsp/LFO.h"
#include "daw/dsp/Wavetable.h"
#include "daw/dsp/WavetableOscillator.h"
#include "daw/synth/PrismaParams.h"

#include <array>
#include <cstdint>
#include <vector>

namespace daw
{

// Framework-independent MIDI event with a sample offset inside the current block.
struct MidiEvent
{
    int sampleOffset = 0;
    uint8_t status = 0, data1 = 0, data2 = 0;

    static MidiEvent noteOn (int offset, int note, int velocity, int channel = 0)
    {
        return { offset, (uint8_t) (0x90 | channel), (uint8_t) note, (uint8_t) velocity };
    }
    static MidiEvent noteOff (int offset, int note, int channel = 0)
    {
        return { offset, (uint8_t) (0x80 | channel), (uint8_t) note, 0 };
    }
    static MidiEvent controller (int offset, int cc, int value, int channel = 0)
    {
        return { offset, (uint8_t) (0xB0 | channel), (uint8_t) cc, (uint8_t) value };
    }
};

} // namespace daw

namespace daw::synth
{

struct VoiceContext
{
    const PrismaParams* params = nullptr;
    const std::vector<const dsp::Wavetable*>* tables = nullptr;
    double sampleRate = 48000.0;
    double bpm = 120.0;
    float modWheel = 0.0f, aftertouch = 0.0f, pitchBendSemis = 0.0f;
};

class PrismaVoice
{
public:
    void prepare (double sampleRate);
    void start (int note, float velocity, float glideFromNote, bool legato, uint64_t age, uint32_t seed, const VoiceContext& ctx);
    void release();
    void kill();

    void render (float* outL, float* outR, int numSamples, const VoiceContext& ctx);

    bool isActive() const noexcept { return amp.isActive(); }
    bool isReleased() const noexcept { return released; }
    int getNote() const noexcept { return note; }
    uint64_t getAge() const noexcept { return age; }
    float getCurrentPitch() const noexcept { return currentPitch; }

private:
    static constexpr int kControlBlock = 16;

    dsp::WavetableOscillator osc[2];
    dsp::SynthFilter filterL, filterR;
    dsp::Envelope amp, env2, env3;
    dsp::LFO lfo[2];
    float lfoValue[2] {};

    int note = -1;
    float velocity = 0.0f, randomValue = 0.0f;
    float currentPitch = 60.0f, targetPitch = 60.0f;
    float smoothedCutoff = -1.0f, smoothedPos[2] { -1.0f, -1.0f };
    double subPhase = 0.0;
    uint32_t noiseSeed = 1;
    bool released = false;
    uint64_t age = 0;
    double sampleRate = 48000.0;
};

// Polyphonic wavetable synthesizer in the spirit of Serum. Real-time safe: no allocation in process().
class PrismaSynth
{
public:
    static constexpr int kMaxVoices = 32;

    PrismaSynth();

    void prepare (double sampleRate, int maxBlockSize);
    void setParams (const PrismaParams& p) { params = p; }
    const PrismaParams& getParams() const noexcept { return params; }
    void setTempo (double bpm) noexcept { ctx.bpm = bpm; }

    // Overwrites outL/outR with numSamples of audio. Events must be sorted by sampleOffset.
    void process (float* outL, float* outR, int numSamples, const MidiEvent* events, int numEvents);

    void allNotesOff();
    void reset();
    int getNumActiveVoices() const;

    const dsp::Wavetable* getTable (int index) const;

private:
    void handleEvent (const MidiEvent& e);
    void noteOn (int note, float velocity);
    void noteOff (int note);
    void renderVoices (float* outL, float* outR, int numSamples);
    PrismaVoice* findVoiceToUse();

    PrismaParams params;
    VoiceContext ctx;
    std::vector<dsp::WavetablePtr> tableRefs;
    std::vector<const dsp::Wavetable*> tables;
    std::array<PrismaVoice, kMaxVoices> voices;

    std::array<bool, 128> sustainedNotes {};
    std::array<int, 16> monoStack {};
    int monoStackSize = 0;
    bool sustainPedal = false;
    float lastPitch = -1.0f;
    uint64_t ageCounter = 0;
    uint32_t seedCounter = 12345;
    float currentGain = 0.0f;
};

} // namespace daw::synth
