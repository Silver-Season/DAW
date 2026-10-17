#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../model/ModelRefs.h"

#include "daw/engine/AudioClip.h"

namespace ui
{
// Draws the waveform of an audio clip from the overview of its source (AudioSource::peaks), with the same mapping of
// content beats to file frames as the engine (renderAudioClip): `area` shows `spanBeats` beats of playback that entered
// the content at `entry` and repeat the loop brace if `loop`. `bpm` is the project tempo. Only what the loader has
// decoded so far is drawn, so a file that is still loading fills in from the left.
void drawAudioWaveform (juce::Graphics&, juce::Rectangle<float> area, const daw::engine::AudioSource&, const ClipRef& clip, double entry,
                        bool loop, double spanBeats, double bpm, juce::Colour ink);
} // namespace ui
