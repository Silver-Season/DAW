#pragma once

#include "daw/drums/DrumKit.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Synthesised one-shots for the factory kits. Everything here is offline (message thread or a loader thread):
// the functions allocate. The output is deterministic: the same parameters and sample rate give the same samples,
// noise comes from a seeded xoshiro128** generator.
//
// Every sound comes out finished: DC removed, a 0.25 ms fade-in and a short fade-out, peak normalised (kicks,
// snares, claps, toms and the 808 sub to -1 dBFS, hats and cymbals quieter). Kicks, snares, toms and percussion
// are mono; hats, cymbals and the shaker are stereo with decorrelated noise for a little width.
namespace daw::drums
{

// Shared shaping controls. Each recipe maps them onto its own base values, so 1 / 0.5 / 0 is the stock sound.
struct SoundParams
{
    float pitch = 1.0f;  // frequency multiplier on the recipe's base pitch (2 = an octave up)
    float decay = 1.0f;  // multiplier on the recipe's decay and length
    float tone = 0.5f;   // 0..1: darker .. brighter (click, noise amount, filter positions)
    float drive = 0.0f;  // 0..1 extra saturation on top of the recipe's own
    float lofi = 0.0f;   // 0..1 bit depth and sample rate reduction, then a low-pass
    float room = 0.0f;   // 0..1 small room (adds a tail up to 0.5 s)
    float gainDb = 0.0f; // <= 0, relative to the sound's normalisation target
    uint32_t seed = 1;
};

enum class SoundType
{
    kick808,
    kick909,
    kickPunchy,
    snare808,
    snare909,
    clap,
    rim,
    closedHat,
    openHat,
    crash,
    ride,
    tomLow,
    tomMid,
    tomHigh,
    cowbell,
    shaker,
    perc,
    sub808,
    snap,
    count
};

// The 808 sub is tuned to C1 (MIDI note 24, 32.703 Hz) at pitch 1. Pitch 2 gives C2 and so on.
inline constexpr int kSub808RootNote = 24;
inline constexpr double kSub808RootHz = 32.703195662574829;

SamplePtr renderSound (SoundType type, double sampleRate, const SoundParams& params = {});

SamplePtr kick808 (double sampleRate, const SoundParams& params = {});    // sine 150 -> 49 Hz, long boom, click, drive
SamplePtr kick909 (double sampleRate, const SoundParams& params = {});    // sine 230 -> 55 Hz, noise click, tighter
SamplePtr kickPunchy (double sampleRate, const SoundParams& params = {}); // fast sweep, beater click, saturated
SamplePtr snare808 (double sampleRate, const SoundParams& params = {});   // two tuned bodies + high-passed noise
SamplePtr snare909 (double sampleRate, const SoundParams& params = {});   // swept body + band-passed noise, driven
SamplePtr clap (double sampleRate, const SoundParams& params = {});       // four noise bursts, tail, small room
SamplePtr rim (double sampleRate, const SoundParams& params = {});        // two short resonances and a tick
SamplePtr closedHat (double sampleRate, const SoundParams& params = {});  // six 808-ratio squares + noise, BP/HP
SamplePtr openHat (double sampleRate, const SoundParams& params = {});    // the same metal, long decay
SamplePtr crash (double sampleRate, const SoundParams& params = {});      // two detuned metal banks + noise, 2 s
SamplePtr ride (double sampleRate, const SoundParams& params = {});       // metal + bell partials, 2.5 s
SamplePtr tomLow (double sampleRate, const SoundParams& params = {});
SamplePtr tomMid (double sampleRate, const SoundParams& params = {});
SamplePtr tomHigh (double sampleRate, const SoundParams& params = {});
SamplePtr cowbell (double sampleRate, const SoundParams& params = {});    // squares 540 + 800 Hz through BP 2.6 kHz
SamplePtr shaker (double sampleRate, const SoundParams& params = {});     // high-passed noise grains
SamplePtr perc (double sampleRate, const SoundParams& params = {});       // conga-like modal hit
SamplePtr sub808 (double sampleRate, const SoundParams& params = {});     // long tuned 808 bass on C1, 1.5 s
SamplePtr snap (double sampleRate, const SoundParams& params = {});       // finger snap

// ---------------------------------------------------------------------------------------------------------------
// Factory kits
// ---------------------------------------------------------------------------------------------------------------

struct FactoryKit
{
    std::string name;
    std::array<std::string, kNumPads> padNames;
};

// "808 Classic", "909 Club", "Trap", "Boom Bap", "Techno", "Lo-Fi". Pads follow padMap().
std::vector<FactoryKit> factoryKits();

// Synthesises pad `pad` of the named kit at the given rate. nullptr for an unknown kit or pad.
SamplePtr renderFactorySound (const std::string& kitName, int pad, double sampleRate);

// Sample source strings for KitState: "factory:<kit>#<pad>".
std::string factorySource (const std::string& kitName, int pad);
bool parseFactorySource (const std::string& source, std::string& kitName, int& pad);

// A ready kit: names, factory sources and default parameters (the closed and the open hat share choke group 1).
// Returns a kit with empty pads for an unknown name.
KitState factoryKitState (const std::string& kitName);

} // namespace daw::drums
