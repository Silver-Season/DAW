# DAW: architecture and roadmap

Working names: the application is **DAW**, the first built-in synth is **Prisma**. Both can be renamed at any time.

## Technical choices

| Decision | Choice | Why |
|---|---|---|
| Language | C++20 | The industry standard for real-time audio |
| Framework | JUCE 8 | Audio/MIDI devices, AU/VST3 hosting, plugin builds, GUI. Tracktion Waveform, Vital and Surge are built on it |
| Build | CMake (+ Ninja or Xcode) | JUCE supports CMake directly |
| Platform | macOS 11+, universal binary (arm64 + x86_64) | Apple Silicon and Intel from one build |
| Plugin formats | AU and VST3 (hosting and export), Standalone | AU is needed for Logic/GarageBand, VST3 for everything else |

**License: GNU AGPLv3** (decided on 16.10.2026). The project is open to everyone, so JUCE is used under its free AGPLv3 license and no commercial JUCE license is needed.

## Layers

```
┌─────────────────────────────────────────────────────────┐
│ app/      GUI: Session View, Arrangement, Mixer, browser │
├─────────────────────────────────────────────────────────┤
│ app/      AudioEngine: track graph, plugin hosting,      │
│           audio/MIDI devices (JUCE)                      │
├─────────────────────────────────────────────────────────┤
│ core/     daw::engine: transport, clips, quantization    │
│ core/     daw::synth:  Prisma (voices, mod matrix)       │
│ core/     daw::dsp:    wavetable, filters, envelopes, LFO│
│           (plain C++20, no JUCE, covered by tests)       │
├─────────────────────────────────────────────────────────┤
│ plugins/  Prisma as AU/VST3/Standalone (JUCE wrapper)    │
└─────────────────────────────────────────────────────────┘
```

All sound and all timing logic live in `core/` with no dependency on JUCE. That gives fast unit tests, offline rendering, and the option to switch GUI frameworks without touching the audio.

### Audio engine
- The audio thread never allocates memory and never waits on I/O. Changes from the GUI reach it as immutable snapshots swapped in atomically, small values as atomics, and commands through lock-free queues (see [docs/spec/00-rules.md](spec/00-rules.md)).
- Block processing: the transport hands out a `[startBeat, endBeat)` window per block, and clips turn it into MIDI events with exact sample offsets.
- Two timelines (stage 3): monotonic beats (`Block::startBeat/endBeat`, `getPositionBeats()`) start from 0 on every play and never jump; Session clip launches, their phase and session recording live on them. The song position (`Block::windows`, `getSongPositionBeats()`) starts at `arrangementPlayStart`, can be relocated and jumps at the loop; a block with a jump is split into windows with exact sample offsets. The arrangement, the metronome and the playhead live on the song position.
- Arrangement (`daw::engine::ArrangementPlayer`): keeps track of the sounding notes per track; on any position discontinuity (relocation, loop, return from the session, clip edits) it turns them off and "chases" the notes under the new position. A track with a running Session clip, or whose Session stop button was pressed, is "in the session" (`TrackRuntime::inSession`) and does not play the arrangement until `AudioEngine::backToArrangement()`.
- Graph: track → instrument → effect chain → fader/pan → buses/sends → master. Later: plugin delay compensation (PDC), multi-threaded track processing.

### Session and clips
- A grid of tracks × scenes where every slot can hold a clip. Clip launches are quantized (1 bar by default), and clips loop.
- Launching a whole scene, stopping a track.
- Done in the skeleton: MIDI clips, quantized launching, scenes, stop. Audio clips with warp come in phase 2.

### Arrangement
- A linear timeline with clips on tracks, recording a Session performance into the Arrangement, a loop, markers.
- The same `ClipPlayer` renders a clip over a time window, so the arrangement reuses the session logic.

### Mixer
- Volume, pan (equal power, −3 dB at center), mute/solo/arm, level meters: done.
- Sends, return tracks, groups, sidechain: phase 2.

### MIDI
- External MIDI devices and the on-screen keyboard go to the armed track.
- Piano roll, MIDI recording, quantization, MPE: phases 1–3.

### Automation
- Parameter envelopes in the arrangement and in clips, write modes (latch/touch), sample-accurate application by splitting the block.

### Plugin hosting
- Scanning AU and VST3, a list of known plugins, loading an instrument on a track, the plugin editor window: done in the skeleton.
- Later: effects in the chain, scanning in a separate process (so a crashing plugin does not take the DAW down), plugin state saved in the project.

### Built-in instruments and effects
| Instrument | Type | Status |
|---|---|---|
| **Prisma** | Wavetable: 2 oscillators, unison up to 16 voices, sub, noise, 12/24 dB SVF filter, 3 envelopes, 2 LFOs with sync, 8-slot mod matrix, mono/glide, presets | First version done |
| **Alloy** (working name), the flagship synth | 4 sources, each with its own engine: virtual analog, wavetable, additive (sample resynthesis), spectral, granular, sampler. XY morphing between sources, 8 macro knobs, arpeggiator, step sequencer, mod matrix with MSEG envelopes, built-in effects. Reuses Prisma's DSP blocks | Phase 2–3 |
| FM synth | 6 operators, algorithms | Phase 3 |
| Subtractive analog | Ladder filter, drift | Phase 3 |
| Sampler / Drum Rack | Pads, layers, choke groups | Phase 2 |

### Presets
- Now: Prisma has 7 factory presets that can be switched from the DAW and from any host (AU/VST3 programs).
- Next: a preset library on disk (`~/Music/DAW/Presets`), a browser with categories and tags (Bass, Lead, Pad, Keys, FX), favorites, search, preview, saving your own presets. Release goal: hundreds of factory presets for every built-in synth.

### MIDI controllers
- Any class-compliant MIDI controller works out of the box: keys, mod wheel, pitch bend, sustain, knobs via MIDI Learn (phase 1).
- **Novation Launchkey (MK3/MK4)**: a dedicated support script using Novation's open DAW Mode protocol. Pads launch Session clips with clip colors lit, knobs control volume/pan/parameters of the selected instrument, transport from the buttons (phase 2).
- **Native Instruments Maschine MK3**: in MIDI mode (via NI Controller Editor) the pads, knobs and transport work as a regular controller with a mapping (phase 2). Deep integration with the Maschine screens relies on NI's closed protocol, and whether it is possible still has to be checked.
- Architecture: a `ControlSurface` layer with a pluggable script per device.

### Third-party plugins
- Loading AU and VST3 from any vendor (Serum, Vital, Kontakt, FabFilter and so on) is already in the skeleton. Saving plugin state in the project is phase 1, isolated scanning phase 3.

Effects (phase 2): 8-band EQ, compressor, reverb, delay, chorus, distortion, limiter.

## Roadmap

**Where we are (October 2026).** Phase 0 is done. Phase 1 proceeds in the stages of [docs/spec](spec/README.md): stage 1 (project model, engine, window, browser, saving), stage 2 (piano roll, scales, metronome, MIDI recording, capture), stage 3 (Arrangement View, recording to the timeline) and part 1 of stage 5 (audio tracks and playback) are done; next is stage 5 part 2 (audio recording), then stage 4 (export). What already works is described in the [README](../README.md); the current step is in [HANDOFF.md](HANDOFF.md).

**Phase 0: foundation.** Build skeleton, `core` with tests, Prisma as a plugin and a built-in instrument, an app with Session View, mixer, AU/VST3 hosting and audio device selection.

**Phase 1: a playable DAW.** Piano roll, MIDI recording, saving and loading projects, a lock-free command queue, undo/redo, preset browser, clip drag and drop.

**Phase 2: production.** Audio tracks and recording, audio clips with warp (time-stretching), Arrangement View, automation, effects, sends and returns, sampler and Drum Rack, PDC, export to WAV/AIFF/MP3.

**Phase 3: the feature set of a mature DAW.** More synths (FM, analog), MPE, group tracks, macros and racks, sidechain, Link (tempo sync), controller support (Push, Launchpad), a multi-threaded engine, isolated plugin scanning.

**Phase 4: release.** Apple code signing and notarization, installer, auto-update, documentation.

**English UI.** All UI strings are collected in `app/Source/Strings.h` and are Russian for now. An English translation with a language switch (and English screenshots in `media/`) is a separate stage, planned after the core production features.

An honest estimate: a full-featured DAW is the result of many years of work by large teams. This project can grow into a strong and comfortable DAW, but the full feature set arrives stage by stage, not in one leap.
