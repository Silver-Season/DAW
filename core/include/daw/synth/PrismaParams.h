#pragma once

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace daw::synth
{

// Every Prisma parameter is a float in this struct (choices and toggles are stored as whole numbers).
// The plugin, the DAW and the offline renderer all use the same descriptor table below, so there is
// a single source of truth for ids, ranges and defaults.

struct OscParams
{
    float on, table, wtPos, octave, semi, fine, level, pan, unison, detune, spread, blend, phase, randPhase;
};

struct EnvParams
{
    float attack, decay, sustain, release;
};

struct LfoParams
{
    float shape, rate, sync, syncDiv, retrig;
};

struct ModSlot
{
    float source, dest, amount;
};

enum class ModSource
{
    None,
    Env2,
    Env3,
    Lfo1,
    Lfo2,
    Velocity,
    ModWheel,
    Aftertouch,
    KeyTrack,
    Random,
    count
};

enum class ModDest
{
    None,
    OscAPitch,
    OscAPos,
    OscALevel,
    OscBPitch,
    OscBPos,
    OscBLevel,
    FilterCutoff,
    FilterResonance,
    FilterDrive,
    AmpLevel,
    Pan,
    Lfo1Rate,
    Lfo2Rate,
    SubLevel,
    NoiseLevel,
    count
};

struct PrismaParams
{
    static constexpr int kNumOsc = 2;
    static constexpr int kNumEnv = 3;
    static constexpr int kNumLfo = 2;
    static constexpr int kNumModSlots = 8;

    std::array<OscParams, kNumOsc> osc {};
    float subOn {}, subLevel {}, subOctave {}, noiseLevel {};
    float filterOn {}, filterType {}, cutoff {}, resonance {}, drive {}, keyTrack {}, filterEnvAmount {};
    std::array<EnvParams, kNumEnv> env {}; // env[0] is the amp envelope, env[1] is wired to the filter
    std::array<LfoParams, kNumLfo> lfo {};
    std::array<ModSlot, kNumModSlots> mod {};
    float masterGain {}, polyphony {}, mono {}, legato {}, glide {}, bendRange {}, velocitySens {};

    PrismaParams(); // initialised from the descriptor defaults
};

struct ParamDescriptor
{
    enum class Kind
    {
        Float,
        Int,
        Bool,
        Choice
    };

    std::string id, name, group;
    Kind kind = Kind::Float;
    float min = 0, max = 1, def = 0;
    float skewCentre = 0;              // 0 = linear, otherwise value that sits in the middle of a knob
    std::string unit;
    std::vector<std::string> choices;  // for Kind::Choice
    std::function<float& (PrismaParams&)> access;

    float& ref (PrismaParams& p) const { return access (p); }
    float get (const PrismaParams& p) const { return access (const_cast<PrismaParams&> (p)); }
};

const std::vector<ParamDescriptor>& parameterDescriptors();

const std::vector<std::string>& modSourceNames();
const std::vector<std::string>& modDestNames();
const std::vector<std::string>& syncDivisionNames();
double syncDivisionBeats (int index); // length of one LFO cycle in beats

struct Preset
{
    std::string name;
    PrismaParams params;
};

const std::vector<Preset>& factoryPresets();

} // namespace daw::synth
