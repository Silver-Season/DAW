#pragma once

#include "daw/engine/Clip.h"

namespace daw::engine::demo
{

// A short song in A minor (Am F C G, 4 bars per clip) used to fill a new session and by the offline renderer.
MidiClip padClip();
MidiClip arpClip();
MidiClip bassClip (bool sustained);
MidiClip leadClip();

} // namespace daw::engine::demo
