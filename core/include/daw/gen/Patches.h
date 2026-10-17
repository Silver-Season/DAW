#pragma once

#include "daw/synth/PrismaParams.h"

#include <cstdint>
#include <string>
#include <vector>

// Patch generator for "Inspiration": designs a Prisma sound of a requested kind from a seed.
//
// A patch is a vector of plain parameter values in the order of synth::parameterDescriptors(), one value per
// descriptor, in the descriptor's own units (seconds, Hz, dB, semitones; choices, ints and toggles as whole numbers).
// That is the same order the Prisma plugin keeps its parameters in, and the same values its
// AudioProcessorValueTreeState holds. The plugin state itself is an XML ValueTree, so the app applies a patch
// parameter by parameter (see paramsFromValues for the core side):
//
//     const auto values = daw::gen::generatePatch (kind, seed, wildness);
//     const auto& descriptors = daw::synth::parameterDescriptors();
//     for (size_t i = 0; i < descriptors.size(); ++i)
//         if (auto* param = prisma.apvts.getParameter (descriptors[i].id))
//             param->setValueNotifyingHost (param->convertTo0to1 (values[i]));
//
// and then reads prisma.getStateInformation() as with a factory preset (PrismaProcessor::setCurrentProgram does
// the same loop for factory presets).
namespace daw::gen
{

enum class SoundKind
{
    bass,
    sub,
    lead,
    pad,
    pluck,
    keys,
    bell,
    fx,
    arp
};

// All kinds in menu order.
const std::vector<SoundKind>& allSoundKinds();

// A stable English key for the kind ("bass", "sub", ...), for files and settings, not for the interface.
const char* soundKindKey (SoundKind kind);

// A full Prisma parameter vector (see above). wildness 0..1: 0 stays within the archetype of the kind, 1 widens
// every range and adds random tables and modulation routings. Every value is inside its descriptor's range, and
// whole-number parameters hold whole numbers. The same arguments always give the same patch.
std::vector<float> generatePatch (SoundKind kind, uint32_t seed, float wildness);

// An evocative two-word name for the patch with this kind and seed, such as "Velvet Drift" or "Neon Bite".
std::string patchName (SoundKind kind, uint32_t seed);

// Conversions between a value vector in descriptor order and the parameter struct the core synth plays. A vector
// that is too short leaves the remaining parameters at their defaults.
synth::PrismaParams paramsFromValues (const std::vector<float>& values);
std::vector<float> valuesFromParams (const synth::PrismaParams& params);

} // namespace daw::gen
