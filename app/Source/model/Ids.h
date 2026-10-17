#pragma once

#include <juce_data_structures/juce_data_structures.h>

// Every node type and property name of the project model (see docs/spec/01-layout-project.md, task 1.1).
namespace ids
{
#define DAW_ID(name) inline const juce::Identifier name { #name };

// node types
DAW_ID (PROJECT)
DAW_ID (TRACKS)
DAW_ID (TRACK)
DAW_ID (DEVICES)
DAW_ID (DEVICE)
DAW_ID (SLOTS)
DAW_ID (SLOT)
DAW_ID (CLIP)
DAW_ID (NOTE)
DAW_ID (CONTROLLERS)
DAW_ID (CC)
DAW_ID (BEND)
DAW_ID (PRESSURE)
DAW_ID (ARRANGEMENT)
DAW_ID (ACLIP)
DAW_ID (MARKER)
DAW_ID (MASTER)
DAW_ID (SCENES)
DAW_ID (SCENE)
DAW_ID (MARKERS)
DAW_ID (SNAPSHOTS)

// project
DAW_ID (schemaVersion)
DAW_ID (tempo)
DAW_ID (sigNum)
DAW_ID (sigDen)
DAW_ID (launchQuantise)
DAW_ID (nextId)
DAW_ID (selectedTrack)
DAW_ID (scaleRoot)
DAW_ID (scaleMode)
DAW_ID (tuningA4)
DAW_ID (author)
DAW_ID (notes)
DAW_ID (quantGrid)
DAW_ID (quantStrength)
DAW_ID (quantSwing)
DAW_ID (quantEnds)
DAW_ID (metronome)
DAW_ID (metronomeVolumeDb)
DAW_ID (metronomeOnlyRecording)
DAW_ID (metronomeSound)
DAW_ID (countInBars)
DAW_ID (overdub)
DAW_ID (recordQuantise)
DAW_ID (recordFixedBars)
DAW_ID (arrangementLoopStart)
DAW_ID (arrangementLoopEnd)
DAW_ID (arrangementLoopOn)
DAW_ID (arrangementPlayStart)
DAW_ID (followPlayhead)

// shared
DAW_ID (id)
DAW_ID (name)
DAW_ID (colour)

// track
DAW_ID (type)
DAW_ID (volumeDb)
DAW_ID (pan)
DAW_ID (mute)
DAW_ID (solo)
DAW_ID (arm)
DAW_ID (input)
DAW_ID (monitor)
DAW_ID (recordSource)
DAW_ID (width)
DAW_ID (transpose)
DAW_ID (arrHeight)

// device
DAW_ID (kind)
DAW_ID (enabled)
DAW_ID (folded)
DAW_ID (pluginDesc)
DAW_ID (state)
DAW_ID (macroParams)

// clip and note
DAW_ID (lengthBeats)
DAW_ID (loopStart)
DAW_ID (loopEnd)
DAW_ID (startOffset)
DAW_ID (offset)
DAW_ID (beat)
DAW_ID (loop)
DAW_ID (ownScale)
DAW_ID (start)
DAW_ID (length)
DAW_ID (pitch)
DAW_ID (velocity)
DAW_ID (number)
DAW_ID (value)
DAW_ID (time)

#undef DAW_ID
} // namespace ids
