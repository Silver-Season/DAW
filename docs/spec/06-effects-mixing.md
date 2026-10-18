# 06. Effects, sends, groups, PDC, automation, snapshots and morphing

## Goal

Provide a full set of production-grade built-in effects, return tracks and groups, plugin delay compensation, automation of any parameter, and the third signature feature: snapshots of every knob with smooth morphing between them.

At the end the user can: put an EQ, a compressor and a distortion on tracks from the browser, send several tracks to a shared reverb, gather the drums into a group, draw or record a filter cutoff movement, and in a live performance move the whole track from "verse" to "drop" with a single knob.

## Scope

In: 13 built-in effects (DSP in `core/` as `daw::fx`, shared with Prisma from stage 09), compact device UIs, return tracks and sends, groups, sidechain for the compressor and gate, PDC, automation in Arrangement and envelopes in clips, automation recording, undo of parameter changes, MIDI Learn, snapshots and morphing.

Out: racks and macros (phase 3), multithreaded track processing (phase 3), factory effect presets (08), the drum machine (08).

---

## Task 6.1. Effects framework

- `core/include/daw/fx/`: a base interface without JUCE:
  ```cpp
  namespace daw::fx
  {
  struct ParamInfo { std::string id, name, unit; float min, max, def; bool logarithmic; std::vector<std::string> choices; };
  class Effect
  {
  public:
      virtual ~Effect() = default;
      virtual const std::vector<ParamInfo>& params() const = 0;
      virtual void prepare (double sampleRate, int maxBlock) = 0;   // the only place where memory may be allocated
      virtual void reset() = 0;
      virtual void setParam (int index, float value) = 0;           // value in parameter units, smoothing is internal
      virtual void process (float* const* io, int numChannels, int numSamples, const float* const* sidechain = nullptr) = 0;
      virtual int latencySamples() const { return 0; }
      virtual double tailSeconds() const { return 0.0; }
  };
  }
  ```
- Every parameter is smoothed (a one-pole 20 ms curve for levels and frequencies, no smoothing for switches) so that automation does not crackle.
- `app/Source/engine/BuiltInEffectProcessor`: a `juce::AudioProcessor` wrapper around `daw::fx::Effect` (parameters as `AudioProcessorParameter`, state as XML with values by `id`). In the model: `DEVICE kind="fx" fxType="eq8"`.
- A compact UI for each effect in the detail panel (width 200–360 px), using the shared control set from 01 (knobs, switches) plus its own display (EQ curve, compressor curve, GR meter, etc., as in the concept).
- The "Built-in" folder in the browser's "Effects" category (01) gets populated.
- The effects can also be built as standalone AU/VST3 (`plugins/fx/`, one plugin per effect). Optional, only if it does not delay the stage.

## Task 6.2. Effects

Every effect has a **Mix** knob (dry/wet with latency compensation of the dry signal) and an **Output** knob (dB), unless stated otherwise.

| Effect | Parameters | DSP |
|---|---|---|
| **EQ** (`eq8`) | 8 bands: type (low cut 6/12/24/48 dB, low shelf, bell, notch, high shelf, high cut), frequency, gain, Q, on; mode Stereo / L/R / M/S | Biquads using the RBJ formulas, cascaded for 48 dB cuts. The UI curve follows the real frequency response, plus a spectrum analyzer (FFT 4096, 1/6-octave smoothing) |
| **Compressor** (`comp`) | threshold, ratio (1:1–∞), attack, release, auto release, knee, makeup (auto), lookahead 0–10 ms, Peak / RMS detector, external sidechain + sidechain filter (HPF) | Detector in the log domain, soft knee, attack/release with separate coefficients. GR meter |
| **Reverb** (`reverb`) | size, decay time, pre-delay (with sync), width, high damping, low cut, diffusion, freeze, modulation | 8×8 FDN (Hadamard matrix), input diffusion with 4 all-pass filters, modulated delay lines, frequency-dependent decay via shelving in the feedback path |
| **Delay** (`delay`) | L/R time (ms or synced: 1/32…1, dotted and triplet), feedback, ping-pong, filter in the loop (HP/LP), modulation, freeze, ducking (echo level dips while the input is playing) | Fractional delay with interpolation (3rd-order Lagrange), time changes without clicks (crossfade between two read heads, 50 ms) |
| **Distortion and saturator** (`drive`) | mode: soft saturation (tanh), tape, tube (asymmetric), hard clip, fold, bitcrush-lite; drive, tone, bias, DC filter, input/output | 4× oversampling (half-band IIR or FIR from the resampler in 04) around the nonlinearity |
| **Chorus** (`chorus`) | voices 1–4, rate, depth, delay, width, feedback | Modulated delay lines, LFO with per-voice phase offset |
| **Flanger** (`flanger`) | rate (with sync), depth, delay, feedback (±), stereo phase | Short modulated delay, 0.1–10 ms |
| **Phaser** (`phaser`) | stages 2–12, rate, depth, center, feedback, stereo phase | Cascade of first-order all-pass filters with modulated frequency |
| **Filter** (`filter`) | type (LP/HP/BP/Notch 12/24, ladder), cutoff, resonance, drive, LFO (shape, rate with sync, depth), envelope follower on the input | Reuse the SVF and ladder from Prisma (09) |
| **Gate** (`gate`) | threshold, hysteresis, attack, hold, release, depth (floor), sidechain, lookahead | Peak detector, open/hold/closed state machine |
| **Limiter** (`limiter`) | ceiling (−0.1…−12 dBTP), gain, release (auto), lookahead 1–5 ms, true peak on | Lookahead limiter with the envelope from a sliding minimum, true peak via 4× oversampling of the detector. Latency = lookahead (reported to PDC) |
| **Bitcrusher** (`crush`) | bit depth 1–24, sample rate (down to 100 Hz), dither, mix | Sample and hold + quantization |
| **Autopan and tremolo** (`autopan`) | rate (with sync), depth, shape (sine, triangle, saw, square, S&H), phase (0° is tremolo, 180° is panning), offset | LFO locked to the transport when synced, so the phase matches on every start |

- Tempo sync in all effects takes tempo and position from `AudioPlayHead`, like Prisma.
- CPU budget: each effect on stereo at 48 kHz costs no more than 1% of one M1 core (reverb and limiter with true peak: 3%). The numbers go into the report via a performance test in `core/tests`.

## Task 6.3. Return tracks and sends

- Track type `return` (already provided for in the model). A new project has two by default: "A Reverb" (with a reverb, mix 100%) and "B Delay" (with a delay, mix 100%). `Cmd+Alt+T` creates a new return track.
- Return tracks sit to the right of regular tracks in Session and at the bottom in Arrangement. They have no clips, only a chain and the mixer.
- Every regular track has a send knob for each return track (`TRACK/SENDS/SEND returnId levelDb=-inf preFader=0`). Post-fader by default; a Pre/Post switch in the return track's header. A return track can send to other return tracks, except itself and those that send to it (acyclic graph, checked in the model).
- Engine: processing order is regular tracks, then return tracks in topological order, then the master.

## Task 6.4. Groups

- `Cmd+G` groups the selected tracks: a track with `type="group"` is created and the selected tracks become its children (`parentGroup=<id>`). Nesting up to 4 levels. `Cmd+Shift+G` ungroups.
- The children's audio goes into the group (the group's chain, its fader), and the group goes to the master. The children's sends go directly to the return tracks.
- UI: a group collapses (triangle in the header). In Arrangement a collapsed group shows a summary of its children's clips; in Session it shows the children's clip launch buttons in a single row. Soloing a group solos its children.

## Task 6.5. Sidechain

- The compressor and gate have a sidechain input: a menu "Sidechain: none / <track> (pre-effects / post-effects / post-fader)".
- The engine processes the source before the consumer; if that would create a cycle, the menu does not let the user pick that source.
- For third-party plugins with a sidechain bus (VST3/AU with an extra input bus), the same menu is in the device header.

## Task 6.6. Plugin delay compensation (PDC)

- Track latency = the sum of `getLatencySamples()` of the devices in its chain. The engine aligns all paths to the master: every track, return and send gets a compensating delay (a ring buffer allocated on the message thread when the graph is rebuilt), so all signals reach the master at the same time.
- A plugin can change its latency on the fly (`AudioProcessorListener::audioProcessorChanged` with the latency flag): the graph is rebuilt. A short gap in the audio is acceptable (as in all DAWs), but no click: a 10 ms crossfade.
- Clip playback accounts for the total latency: MIDI and audio are fed earlier by the graph's maximum latency, so the sound lines up with the playhead. Input monitoring and live playing are not delayed.
- The transport shows the total latency when it is above 0 ("PDC 12 ms"). A "Disable PDC" button in settings for low-latency live playing.

## Task 6.7. Automation

### Model

```
TRACK/AUTOMATION/LANE  target="device:<deviceId>:<paramIndex>" | "mixer:volume" | "mixer:pan" | "send:<returnId>"
                       visible=1  height=44
     └── POINT  beat  value (normalized 0..1)  curve=0 (−1..1, bend of the segment to the next point)
CLIP/ENVELOPE  target=…  └── POINT …          (envelope inside a clip: in clip beats, repeats with the loop)
```

### Playback

- On the audio thread, automation is applied in sub-blocks of 32 samples: the value at the start of the sub-block is set on the parameter (via `AudioProcessorParameter::setValue` for plugins, which JUCE allows from the audio thread; directly for built-in effects and Prisma), and the smoothing inside the effects makes the transition smooth.
- A clip envelope and arrangement automation on the same parameter: the clip multiplies (for volume) or offsets (for everything else) relative to the arrangement value (a modulation envelope).
- When the user moves an automated knob by hand during playback, that track's automation is "released" (the "Re-enable Automation" button lights up in the transport) until the button is pressed.

### UI

- `A` toggles automation mode in Arrangement: under each track are the lines of its visible lanes, and the lane header has a device and parameter selector. Right-click on any knob → "Show Automation".
- Editing: click on the line to add a point, drag a point, Alt+drag a segment to bend it, marquee-select points to move or delete them, `Delete`. The pencil (`B`) draws steps on the grid. A time selection plus `Delete` removes the points inside it.
- Clip envelopes: in the clip editor (02), an "Envelopes" tab with the same editing.

### Recording

- The automation button in the transport (dotted icon, `Alt+A`): when recording in Arrangement, knob movements are written into lanes. **Touch** mode: writes while the knob is held, then returns to the previous line. **Latch** mode (a setting): writes until stop. The recording is thinned out (Ramer–Douglas–Peucker algorithm with a 0.5% tolerance) to avoid thousands of points.
- Recording in Session View goes into the envelope of the playing clip if the OVR button is on.

### Undo of parameter changes

- Every movement of a built-in device or mixer knob (from press to release) is one undo transaction, "Change Filter Cutoff". For third-party plugins, via `beginChangeGesture`/`endChangeGesture` of the parameter listeners.

## Task 6.8. MIDI Learn

- `Cmd+K` enters mapping mode: all knobs, faders and buttons are highlighted. Click an element, then move a controller knob to assign a CC (or a note for a button). Pressing `Cmd+K` again exits.
- Mappings are stored in the project (`PROJECT/MIDIMAP/MAP channel cc target min max`) and shown as a list in the MIDI settings (inputs with "Remote" enabled from 01 now have a purpose). Takeover mode: "Jump" / "Pickup" (the value changes once the controller knob passes through the current value).

## Task 6.9. Snapshots and morphing

### Model

```
PROJECT/SNAPSHOTS  morphA=<id> morphB=<id> morph=0.0 (0..1)
└── SNAPSHOT  id  name  colour  time
    └── VALUE  target="device:<id>:<param>" | "mixer:<trackId>:volume" | …  value (normalized 0..1)
```

### Behavior

- The "Snapshot" button in the transport (or `Cmd+Alt+S`) stores the current values of **all** parameters of all devices and the mixer (except the master volume). The snapshot is named "Snapshot 3" and can be renamed by double-clicking.
- Snapshot panel (drops down from the A↔B slider): a list of snapshots as colored cards, A and B are assigned by dragging onto the letters, and a "Recall" button applies a snapshot instantly (one undo transaction).
- **Morph**: an A↔B slider in the transport and a separate assignable knob (MIDI Learn suggests the modulation wheel or the first Launchkey knob by default). The value `m` gives each continuous parameter `value = A + (B − A) × m`; switches and lists change at `m ≥ 0.5`. Parameters whose values are the same in A and B are left alone, so they can be moved by hand during a morph.
- An "Exclude from Morph" checkbox on the right-click menu of any knob (for example, the volume of the vocal track). Stored as `SNAPSHOTS/EXCLUDE target`.
- The morph is recorded as automation: a lane with `target="morph"` on the master track, like any parameter. When such a lane plays, the morph applies values to the parameters; a parameter's own automation lanes take priority over the morph.
- The morph is applied on the message thread at 60 Hz during manual movement (these are parameter changes, and the engine smooths them), and during playback of morph automation it is applied on the audio thread in sub-blocks, like regular automation. For this, when A and B are assigned, a flat array `MorphTarget { paramPtr, a, b, isStepped }` is built and published into the graph.
- Link with Alloy (stage 10): if a track has Alloy, the morph can drive its Transform Pad instead of individual parameters (a checkbox in the snapshot panel, "Morph Alloy via Transform Pad").

## Tests

1. **Effects, general** (core, for each of the 13): with Mix = 0 the output equals the input, accounting for latency (null test < −120 dB); silence in gives silence out after the tail; white noise at −6 dBFS gives no NaN, Inf or output beyond ±4.0; `reset` removes the tail; `latencySamples` matches the measured impulse latency.
2. **EQ:** the frequency response of a +6 dB bell at 1 kHz, Q 1 matches the analytic one ± 0.1 dB; a 48 dB cut at 100 Hz gives −48 ± 1 dB at 50 Hz.
3. **Compressor:** a −10 dBFS sine, threshold −20, ratio 4:1, knee 0 → steady-state level −17.5 ± 0.2 dBFS; a 10 ms attack reaches 63% ± 5% in 10 ms.
4. **Limiter:** on a +6 dBFS sine the output true peak is ≤ ceiling + 0.1 dB.
5. **Distortion:** a 15 kHz sine at 48 kHz with 24 dB drive: aliasing products below −80 dB relative to the fundamental (thanks to 4×).
6. **Delay:** an impulse with time 1/4 at 120 BPM gives an echo after 24,000 ± 1 samples at 48 kHz; changing the time while sounding gives no jumps larger than 0.1.
7. **Sends and returns:** a −6 dB send gives a −6 dB signal in the return; pre-fader does not depend on the fader.
8. **Cycles:** the model does not allow creating a send or sidechain cycle.
9. **PDC:** a track with a limiter (5 ms latency) and a track without effects, with the same impulse at the same position, produce impulses in the master at the same sample.
10. **Automation:** a volume line from −∞ to 0 dB over 1 bar gives a monotonic envelope with no steps larger than 0.5 dB; segment bend changes the shape as expected.
11. **Release:** moving a knob during playback disables the track's automation until "Re-enable Automation".
12. **Morph:** at `m = 0` the values of A, at `m = 1` the values of B, at `m = 0.5` the midpoint for continuous parameters and B for switches; excluded parameters do not change.
13. **Real time:** a project with 8 tracks, 3 effects and automation on each, with the morph written on the audio thread: zero memory allocations.

## Done when

- [ ] All 13 effects are in the browser, each with a compact UI (screenshots in 4 themes), tests 1–6 green.
- [ ] Two return tracks by default, pre/post sends, groups with nesting and collapsing.
- [ ] Compressor sidechain from the kick works (the bass pumping is audible and visible on the GR meter).
- [ ] PDC aligns tracks (test 9), total latency is visible in the transport.
- [ ] Automation can be drawn, recorded in Touch and Latch modes, clip envelopes work.
- [ ] Every knob movement is undone with `Cmd+Z`.
- [ ] MIDI Learn assigns controller knobs.
- [ ] Snapshots are stored, the A↔B morph smoothly moves the whole track, the morph is recorded as automation.
- [ ] All tests green, CI green, README updated.
