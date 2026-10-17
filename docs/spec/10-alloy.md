# 10. Alloy: the flagship multi-engine synth

## Goal

Build Alloy, the second built-in synth and the project's main instrument. Its defining quality: you can step through presets one after another and each one immediately sounds finished, while underneath sits a deep editor that reaches every parameter. Alloy is built on the Prisma DSP (wavetable, filters, envelopes, LFOs) and the shared `daw::fx` effects, and adds what Prisma lacks: four sources A–D with different engines (VA/wavetable, sampler, granular, additive and spectral resynthesis), morphing between sources, the Transform Pad with eight snapshots of all parameters, performance knobs, MSEG, a large mod matrix, an arpeggiator and a preset library with attributes.

At the end of stage 10C the user can: open Alloy in the DAW or as an AU in a third-party host, filter presets by category and timbre in the "Browse" tab, step through them with the arrow keys while playing, drag the point around the Transform Pad and hear a smooth transition between eight variations of the sound, turn the 8 knobs, and in the "Detail" tab load their own WAV, decompose it into partials and play it across the keyboard.

## Scope

The stage is large, so it is split into three sub-stages. Each sub-stage is a separate branch (`stage-10a-alloy-engine`, `stage-10b-alloy-resynth`, `stage-10c-alloy-library`), a separate report, and a fully working synth at the end.

| Sub-stage | Tasks | Result |
|---|---|---|
| 10A | 10.1–10.15 | Engine with 4 sources (VA/wavetable, sampler, granular), filters, source morph (crossfade), modulation, Transform Pad, performance controls, effects, preset format, "Browse", "Play" and "Detail" tabs, DAW device, AU/VST3/Standalone. At least 60 factory presets |
| 10B | 10.16–10.20 | Additive resynthesis (partials), spectral resynthesis (phase vocoder), spectral source morph, partial and spectrum editors. At least 150 presets |
| 10C | 10.21–10.24 | Arpeggiator, step sequencer, library of 300+ tagged presets, library lint tool |

In scope: everything in the table. Out of scope: formant and pitch-shifting source elements, importing third-party preset formats (closed formats), MPE (phase 3), painting the spectrum with a brush (image import only), network preset sharing, multithreaded voice rendering.

Rule 00 §9 "do not touch Prisma DSP" applies: Alloy reuses the `daw::dsp` headers as is. If shared code needs a change (for example, a new LFO shape), it is done by addition, and a Prisma render through `tools/prisma_render` before and after the change matches sample for sample.

### Concept map

The main concepts of Alloy and the tasks that implement them. Items marked "verify by ear" are design choices of this spec whose feel the implementer checks by listening and reports on.

| Concept | Where in Alloy |
|---|---|
| Browse view: attribute columns (category, subcategory, genre, timbre), search, stepping with arrow keys | "Browse" tab, task 10.12 |
| Simple view: Transform Pad and performance controls | "Play" tab, task 10.13 |
| Transform Pad: 8 parameter snapshots, a point morphs between them, the point's path is recorded and automated, there is a morph time | Task 10.8. Our own interpolation law (IDW), verify by ear |
| 8 performance knobs and 2 XY pads | Task 10.9 |
| Advanced view: sources A–D, each source has an Additive, Spectral, Granular, Sampler or VA element | "Detail" tab, tasks 10.3, 10.16–10.19 |
| Source morph: crossfade and morph modes, XY morph A–D | Tasks 10.6 and 10.20 |
| MSEG, LFO, AHDSR, mod matrix, assigning modulation by right-clicking a knob | Task 10.7, 32 slots |
| Step arpeggiator, sequencer | Tasks 10.21, 10.22 |
| Effects inside the synth | Task 10.10, through `daw::fx` |

---

## Task 10.1. Skeleton: core, plugin, parameters

- Core in `core/include/daw/synth/alloy/` and `core/src/alloy/`, namespace `daw::synth::alloy`. No JUCE, like Prisma. CMake targets: sources are added to `daw_core`, tests go in `core/tests/alloy_tests.cpp` (a separate `daw_alloy_tests` executable, so `daw_core_tests` does not keep growing).
- Plugin `plugins/alloy/` modeled on `plugins/prisma/`: `AlloyProcessor`, `AlloyEditor`, `PluginEntry.cpp`. `PRODUCT_NAME "Alloy"`, `BUNDLE_ID "com.dawproject.alloy"`, `PLUGIN_MANUFACTURER_CODE Dawp`, `PLUGIN_CODE Aloy`, formats AU (macOS only), VST3, Standalone, `IS_SYNTH TRUE`, `AU_MAIN_TYPE kAudioUnitType_MusicDevice`.
- Offline renderer `tools/alloy_render` modeled on `prisma_render`: preset + MIDI file → WAV and render time. Needed for tests and CPU budgets.

### Parameters

As in Prisma, a single descriptor table is the only source of ids, ranges and default values:

```cpp
struct AlloyParamDescriptor          // modeled on daw::synth::ParamDescriptor
{
    std::string id, name, group;     // id is stable forever: "a.va.wtPos", "flt1.cutoff", "mod.07.amount"
    Kind kind;                       // Float | Int | Bool | Choice
    float min, max, def, skewCentre;
    std::string unit;
    std::vector<std::string> choices;
    uint32_t flags;                  // kNoMorph, kNoAutomation, kGlobal (see 10.8)
    int offset;                      // index into AlloyParams::values
    float toNormalised (float v) const;   // takes skewCentre into account, like the knob
    float fromNormalised (float n) const;
};

struct AlloyParams
{
    static constexpr int kMaxParams = 1024;
    std::array<float, kMaxParams> values;   // flat array: snapshots and morph work on it as a whole
};
const std::vector<AlloyParamDescriptor>& alloyParameterDescriptors();
```

- A flat array instead of nested structs was chosen for the Transform Pad: morphing is a loop over indices, with no pointers to members. For readable DSP code there are typed wrappers (`SourceView src = params.source (0)`) that compute offsets from the table once.
- The expected parameter count is ~550 (source ~70 × 4, filters, 4 LFOs, 6 envelopes, 32 modulation slots × 4 fields, performance controls, effects). All parameters except those marked `kNoAutomation` are visible to the host under their ids; the host order is fixed by `offset`, and new parameters are only appended at the end.
- Non-numeric data (MSEG points, samples, resynthesis models, the Transform Pad path, arpeggiator steps) is stored outside `AlloyParams` in `AlloyPatchData` (10.11) and is not morphed.

## Task 10.2. Voices

- `AlloyVoice` holds 4 `SourceSlot`s (A–D), one filter per source, 2 global filters, 6 envelopes, 4 per-voice LFOs, 4 MSEGs. Global (non-voice) LFOs: 2, in `AlloySynth`.
- Voice allocation: a new shared class `daw::synth::VoiceAllocator` (`core/include/daw/synth/VoiceAllocator.h`): note stack for mono, legato, glide, stealing the oldest released voice, then the oldest sounding one, sustain pedal. Take the logic from `PrismaSynth` (`findVoiceToUse`, `monoStack`). Moving Prisma to this class is optional and only if the Prisma render matches sample for sample; if it does not, Prisma stays as is, and this is noted in the report.
- Polyphony is a parameter, 1–32 (default 16), `kMaxVoices = 32`. A stolen voice fades out over 3 ms instead of being cut.
- Modulation and morph are computed at control rate, block `kControlBlock = 16` samples, as in `PrismaVoice`. Everything that feeds volume, pan, cutoff and wavetable position is smoothed within the block (linearly or with a one-pole of 2–5 ms) so there is no stepping.
- A voice skips disabled sources entirely (zero cost), and a voice whose amplitude envelopes have all finished is freed.

## Task 10.3. Sources and elements

Each source A–D has: `on`, `element` (Choice: VA, Sampler, Granular; Additive and Spectral are added in 10B), `level`, `pan`, `octave`, `semi`, `fine`, `keyTrack`, `velSens`, a send to the global filters (`route`: Filter1, Filter2, both, bypass), its own filter (10.5) and its own amp with an envelope. Each element has its own parameters. The parameters of all elements exist at the same time (switching the element does not lose settings); only the selected one is active.

### VA / wavetable

- Two `dsp::WavetableOscillator` oscillators (all `dsp::factory` tables and user tables of the same 2048 × N frames format), a sub-oscillator, noise. Parameters as in the Prisma oscillator (`OscParams`): position, unison up to 16, detune, spread, blend, phase.
- Mix modes of the two oscillators: sum, ring mod, hard sync (B to A). Sync is implemented by a phase reset with polyBLEP correction of the discontinuity; without the correction, aliasing is noticeable in test 4.
- VA shapes (saw, square with PWM, triangle, sine) are made as wavetables built with `Wavetable::fromFrames` (PWM is 64 frames with different duty cycles), not as a separate algorithm.

### Sampler

- Up to 32 zones per source: `SampleZone { sampleRef, rootNote, lowKey, highKey, lowVel, highVel, tune, gain, start, end, loopStart, loopEnd, loopMode (Off | Forward | PingPong), loopXfade }`. The zone map is data, not parameters.
- Interpolation: 16-point windowed sinc with a precomputed polyphase table (256 phases). When transposing up by more than an octave, a precomputed half-rate copy of the sample is used (a mip, as with wavetables); otherwise there is aliasing.
- Source parameters: `start` (modulatable), `loopOn`, `reverse`. No slicing.

### Granular

- A cloud of grains over the source sample (the first zone). Parameters: `position` (0–1), `posSpeed` (−2…2, 0 is a freeze frame), `spray` (random position scatter, ms), `grainSize` (5–500 ms), `density` (grains/s, 1–200), `pitchRand` (cents), `panRand`, `window` (Hann | Tukey | Exp), `reverse` (probability 0–1).
- Up to 64 simultaneous grains per voice, the pool is fixed, and on overflow a new grain is skipped. Grain scheduling is a sample counter with period `sr / density` and a random offset of ±10 %; the random number generator is deterministic (LCG as in `dsp::LFO`), with the seed derived from the voice number.
- Volume is normalized by the expected overlap `grainSize × density`, so density does not change the level by more than 3 dB.

## Task 10.4. Sample import

- Formats WAV, AIFF, FLAC, 16/24/32-bit and float, mono and stereo, any sample rate. Reading is done with `juce::AudioFormatManager` in the plugin layer; the core receives `SampleData { std::vector<float> l, r; double sampleRate; int rootNote; loop points from metadata (smpl/inst), if any; std::string hash }`.
- Loading and preparation (conversion to float, mip copies, resynthesis analysis in 10B) happen on a background thread (`juce::ThreadPool`). The finished `std::shared_ptr<const SampleData>` is published by atomic swap, and the old one is freed on the message thread (00 §2). While the sample loads, the source is silent and the UI shows progress.
- Limits: up to 10 minutes per sample and 1 GB per instance; exceeding them gives a clear error in the UI, not a crash.
- Root note: from metadata, otherwise detected by the YIN pitch detector (the same one as in 10.16), otherwise C3. The user edits it in the zone editor.
- Reference in the preset: a path relative to the library (`Factory Library/Samples/...` or `User Library/Samples/...`) plus `hash` (SHA-1 of the content). On open it is looked up by path, then by hash in the library folders; if not found, the source is marked "Sample not found: name" and the preset still loads. An "Embed samples in preset" option (FLAC inside the file) for sharing presets.
- Dragging a file from Finder or the DAW browser onto a source loads it into that source.

## Task 10.5. Filters

- Each source has one `dsp::SynthFilter` (all `FilterType` types), with parameters `type`, `cutoff`, `resonance`, `drive`, `keyTrack`, `envAmount` and its own envelope.
- Two global filters per voice with routing `filterRouting` (Serial | Parallel) and balance `filterBalance`.
- New types (comb, formant) are not made. If the implementer needs them, they write a proposal in the report.

## Task 10.6. Source mixing and morph

- `sourceMixMode` (Choice): `Mix` (plain sum with levels), `Crossfade`, `Morph` (appears in 10B, task 10.20).
- `Crossfade`: XY morph `morphX`, `morphY` in [0, 1], with sources at the corners A (0,0), B (1,0), C (0,1), D (1,1). Weights are bilinear: `wA = (1−x)(1−y)`, `wB = x(1−y)`, `wC = (1−x)y`, `wD = xy`; source gain is `sqrt (w)` (equal power). There is also a one-dimensional mode `morphLinear` (A→B→C→D on one knob, switch `morphAxis` = XY | Linear).
- `morphX`, `morphY`, `morphLinear` are ordinary modulatable parameters: they can be driven by LFOs, MSEGs and performance knobs.
- A source with weight below −90 dB is not rendered, but its envelopes keep running, so that when the weight comes back the sound does not restart from the attack.

## Task 10.7. Modulation

- **Envelopes:** 6 per voice (AHDSR with curvature for each segment, −1…1). Env1 is the voice amplitude, the rest are free.
- **LFO:** 4 per voice and 2 global. Shapes from `dsp::LfoShape` plus `Custom` (shape from an MSEG); tempo sync (Prisma's `syncDivisionNames()`), retrig, phase, delay and fade-in (`delay`, `fadeIn`), unipolar mode.
- **MSEG:** 4 per voice. `Mseg { std::vector<MsegPoint> points; int loopStart, loopEnd; bool sync; float lengthBeats; }`, `MsegPoint { float time, value, curve; }`, up to 64 points. Modes: envelope (one pass, loop while the note is held, release segment after loopEnd) and LFO (infinite loop). On the audio thread the MSEG is read from an immutable snapshot; editing the points publishes a new one.
- **Mod matrix:** 32 slots `ModSlot { source, via, dest, amount, curve }`. `via` is a second source that scales the depth (for example, the mod wheel). `curve` is Linear | Exp | Log | S. Sources: envelopes, LFOs, MSEGs, velocity, mod wheel, aftertouch, pitch bend, key track, per-note random, the 8 performance knobs, the axes of the 2 XY pads, the step sequencer (10C), CC 1–127 via MIDI Learn. Destination: any parameter with the modulatable flag (almost all Floats).
- Depth is computed in the parameter's normalized units: `n = clamp (n0 + Σ amount_i · s_i, 0, 1)`, then `fromNormalised`. So a depth of 0.1 on the cutoff means "a tenth of the knob", which matches what the user sees.
- In the UI: right-click a knob → "Modulate → source", or drag a source icon onto a knob; a depth arc is drawn around the knob in the `accent2` color.

## Task 10.8. Transform Pad

The main feature of the stage. Eight snapshots of all morphable parameters around an XY pad; the position of the point determines the blend.

### Data

```cpp
struct TransformSnapshot
{
    bool used = false;
    std::array<float, AlloyParams::kMaxParams> norm;   // normalized values 0..1
    std::string name;                                  // "Dark", "Open"...
};

struct TransformPad
{
    std::array<TransformSnapshot, 8> snapshots;
    std::array<Point, 8> positions;   // default: on a circle r = 0.42 around (0.5, 0.5), 45° step, first at top left
    float morphTimeSec = 0.0f;        // time for the point to travel to the selected snapshot
    TransformPath path;               // recorded path, see below
};
```

- Parameters `transform.x`, `transform.y` (Float, automatable by the host), `transform.on` (Bool).
- Descriptor flags: `kNoMorph` means the parameter is never morphed (polyphony, bend range, MIDI channel selection, master volume by default). `kGlobal` is the same, but the parameter is shown in a separate list. User exclusion is `std::bitset<kMaxParams> morphExclude` in the preset data: right-click a knob → "Don't morph". Excluded parameters take their value from the editor's "current" state.

### Morph math

For point `p` and the used snapshots `i` with positions `q_i`:

1. Distances `d_i = |p − q_i|`. If `d_i < 0.06` for some `i`, the point is "in the snapshot": `w_i = 1`, all others 0.
2. Otherwise inverse-distance weights: `w_i = 1 / d_i²`, normalized by their sum. (This gives the full snapshot inside its cell and a smooth blend between cells; verify by ear.)
3. Float: `n = Σ w_i · norm_i`, value `fromNormalised (n)`. Interpolating in normalized units is deliberate: for a skewed cutoff frequency it gives a perceptually even transition instead of a jump in the upper kilohertz.
4. Int: the same, then rounding.
5. Choice and Bool: for each option `c`, the sum of the weights of the snapshots where `c` is selected; the maximum wins (on a tie, the lower snapshot index). For two snapshots this is exactly "switch at 50 %". Hysteresis 0.02: the option changes only if the new sum exceeds the current one by 0.02, so automation near the threshold does not chatter.
6. A Choice change inside a source (element, filter type, wavetable) crossfades for 5 ms between the old and new configuration inside the voice, with no click. For this, `SourceSlot` holds two states during the transition.
7. Unused snapshots do not take part. If 0 snapshots are used, the pad is inactive; if 1, its values are applied in full.

The morph is computed on the audio thread once per control block (16 samples) from the snapshot arrays of the immutable `TransformPadState` (published atomically when a snapshot is edited). Cost: ~550 × 8 multiply-adds per block, negligible. The result is the base parameter values, on top of which the mod matrix (10.7) works. The smoothing from 10.2 removes zipper noise.

### Snapshots and morph time

- Clicking a snapshot cell: the point travels to its position over `morphTimeSec` (0–10 s, in a straight line, with `smoothstep` ease-in and ease-out). At 0 it is instant, but the values still pass through the 10.2 smoothing.
- "Save snapshot" into a cell copies the current normalized values. "Copy", "Paste", "Clear", "Rename" are in the cell menu. Each operation is one undo transaction of the editor (Alloy has its own `juce::UndoManager` in the processor; in the DAW, the shared one via `DeviceRef`).
- Dragging the point with the mouse: the UI writes `transform.x/y` via `setValueNotifyingHost` with `beginChangeGesture`/`endChangeGesture` around the whole gesture, so the host records automation.
- What the user edits when the point is between snapshots: editing a knob changes the "current" value; when the pad is on, the pad header shows "Edits not saved to snapshot", and the "To nearest snapshot" button writes them.

### Path

```cpp
struct TransformPath
{
    std::vector<PathPoint> points;     // { float t; float x, y; }, t in beats or seconds
    bool sync = true;                  // t in beats (host tempo) or seconds
    float lengthBeats = 4.0f;
    enum class Mode { Off, OneShot, Loop, PingPong } mode = Mode::Off;
    bool retrigOnNote = true;
};
```

- "Record path" button: while it is on, point movements are recorded with thinning (no more than 100 points/s, then Ramer–Douglas–Peucker simplification with tolerance 0.005). The path is shown on the pad as an `accent2` line, and its points can be moved.
- When the path plays, the point follows it (linear interpolation between points); `transform.x/y` are not changed by the host meanwhile but are shown in the UI. Paths have up to 512 points.
- Parameter `transform.pathPos` (0–1, automatable) lets you drive the point along the path manually or from the DAW; this is what the integration in 10.15 uses.

### Snapshot thumbnails

Each snapshot cell has a 48×48 thumbnail: a schematic "fingerprint" of the sound made of 5 bars (cutoff, resonance, source morph, levels A–D, attack), drawn in the theme palette colors. It is drawn into a cached image and redrawn when the snapshot is saved. The thumbnail is for recognizing snapshots, not for reading exact values.

## Task 10.9. Performance controls

- 8 knobs `perf.1` … `perf.8` (0–1) and 2 XY pads `perf.xy1.x/y`, `perf.xy2.x/y`. Each is a modulation source in the matrix; in addition, each knob has up to 8 "quick assignments" `PerfTarget { paramIndex, min, max, curve }` (like macro knobs), set up by dragging the knob onto a parameter in "Detail" while holding `Cmd`.
- Knob and pad names are stored in the preset ("Brightness", "Motion") and shown in all views and in the DAW.
- Performance knobs are `kNoMorph` by default, so moving the pad does not turn the knobs under your fingers; a preset may clear the flag.
- MIDI Learn for all eight knobs and both pads (right-click → "Assign MIDI"); assignments are stored in the instance state, not in the preset.

## Task 10.10. Effects

- The effects DSP lives in `core/` in the `daw::fx` namespace and is shared by the stage 06 DAW devices, Prisma (if effects are added to it) and Alloy. Alloy does not write its own effects. If at the start of 10A the stage 06 effects live in `app/` with a JUCE dependency, the first task of 10A is to move their pure DSP to `core/include/daw/fx/` without changing the sound (the 06 tests stay green).
- Alloy effects rack: 8 slots after the voice sum, reordered by dragging. All `daw::fx` effects are available (per 06: EQ, compressor, reverb, delay, chorus, distortion; whatever 09 adds as well). Effect parameters are ordinary Alloy parameters (`fx.3.mix`), morphed and modulated by global sources.
- Changing the effect type in a slot is a Choice with a 20 ms crossfade; memory for all effects in the rack is allocated in `prepare`, not on change.

## Task 10.11. Preset format

- File `.alloypreset`, XML via `juce::ValueTree` in the plugin layer; the core provides `AlloyPatch` (parameters + `AlloyPatchData`) and serialization functions into a neutral `PatchNode` tree, so core tests can check the round trip without JUCE.

```
ALLOYPRESET  version=1  name  author  created  modified  uuid
├── TAGS  category="Pad"  subcategory="Evolving"  timbre="Warm,Airy,Wide"  genre="Ambient,Cinematic"
│         articulation="Sustained"  favourite=0
├── PARAMS  <P id="flt1.cutoff" v="0.4372"/> ...   (only values differing from the default, normalized)
├── SOURCES └── SOURCE idx=0  └── ZONE ... sampleRef path hash  (+ EMBED flac base64, if embedded)
│                              └── MODEL kind="additive|spectral" hash  (analysis cache, 10B)
├── MSEGS   └── MSEG idx points="t,v,c;..." loopStart loopEnd sync length
├── TRANSFORM  morphTime  └── SNAP idx name  (only differences from PARAMS) └── PATH mode sync length points
├── PERFORM  └── KNOB idx name  └── TARGET param min max curve
├── MORPHEXCLUDE  ids="a.level,b.level"
└── ARP / SEQ  (10C)
```

- Unknown ids are skipped on load with a log entry; missing ones take the default value. `version` grows on incompatible changes, and the loader reads all past versions (like the project file in 01, task 1.6).
- The resynthesis analysis cache (10B) is stored next to the sample in `~/Library/Caches/Alloy/<hash>.alloymodel`, not in the preset: the preset stays small and re-analysis is not needed.
- Plugin state (`getStateInformation`) is the same XML plus the instance state (MIDI Learn, open tab), compressed with `juce::GZIPCompressorOutputStream`.
- Folders: `~/Music/Prisma DAW/Factory Library/Alloy/` (factory, read-only) and `User Library/Presets/Alloy/` (the same as device presets from 01, task 1.6). A preset from the DAW browser and a preset from the plugin are the same file.

## Task 10.12. "Browse" tab

- On the left, attribute columns: Category, Subcategory, Timbre, Genre. On the right, the preset list, with search at the top and, at the bottom, a mini Transform Pad and the 8 knobs, so the sound can be tweaked without leaving the browser.
- Filter: multiple selections within a column are OR, between columns AND. A value that would yield 0 presets under the current filter is shown in `dimText`. Search by name, author and tags, case-insensitive, Cyrillic and Latin.
- The tag vocabulary is fixed (`core/src/alloy/TagVocabulary.cpp`); the file uses English keys, and UI labels come from the strings file (English by default, Russian as the second language):
  - Category: Bass, Lead, Pad, Keys, Pluck, Arp, Sequence, Synth Brass, Strings, Bells, Vocal, Organ, Drum, Percussion, FX, Soundscape, Init.
  - Timbre: Warm, Bright, Dark, Airy, Gritty, Clean, Metallic, Glassy, Wide, Thin, Fat, Evolving, Static, Aggressive, Soft.
  - Genre: Ambient, Cinematic, Electronica, EDM, House, Techno, Hip-Hop, Pop, Rock, Experimental, Lo-Fi.
  - Subcategories are defined per category (Bass → Sub, Reese, Acid, Wobble, Plucked; Pad → Evolving, Lush, Dark, Choir, ...), the full list is in the same file.
- **Stepping.** A single click or the up/down arrows load the preset immediately. This is the main function of the browser: changing presets while playing does not crackle and does not produce more than 50 ms of silence for presets without new samples. How: the processor builds the new `AlloyPatch` and snapshots on a background thread, then the sounding voices get a fast 30 ms release, and new notes already use the new preset. Samples of neighboring presets in the list (±2) are preloaded.
- Favorites (star), "Recent" (last 30), sorting by name and date.

## Task 10.13. "Play" tab

Simple view: a large Transform Pad (360×360 at 100 % scale) with 8 snapshot cells around the circle, on the right the 8 performance knobs in two rows and 2 XY pads, at the bottom an on-screen keyboard (`juce::MidiKeyboardComponent`, as in the Prisma editor) and a row with volume, glide and polyphony. Nothing beyond that: this view is for playing, not for programming.

## Task 10.14. "Detail" tab and the overall editor layout

- Header of all tabs: preset name with ◀ ▶, preset menu (save, save as, tags…), tabs "Browse", "Play", "Detail", "Arp" (10C), "Effects", voice and CPU meter.
- "Detail": four source strips A–D (a collapsed strip shows element, level and filter on one line), below them the source XY morph and the global filters, on the right modulation (tabs for envelopes, LFOs, MSEGs, matrix). MSEG editor: add a point by double-click, curvature by dragging a segment with `Alt`, grid in beats.
- The editor window scales (75–200 %), default size 1100×720. Colors, fonts and corner radii come only from `Theme` (01, task 1.8): `panel`/`panel2` for blocks, `accent` for active elements, `accent2` for morph, modulation and the Transform Pad path, the theme palette for the colors of sources A–D (indices 0, 4, 9, 12). In the plugin outside the DAW, the theme is chosen in the editor menu and defaults to "Prism"; the theme JSON files are embedded in the plugin as the same files.
- UI text lives in `plugins/alloy/Source/AlloyStrings.h` per rule 00 §4.

## Task 10.15. DAW integration

- `DEVICE kind="alloy"` in the project model (01, task 1.1); `state` is the plugin state. In the DAW browser, Alloy and its presets appear under "Instruments" and "Presets" with the same attributes (attribute columns act as a filter inside the Alloy folder). Preset preview in the browser works as for Prisma.
- **Compact view** in the detail panel (560 px wide): a 150×150 mini Transform Pad with the point and snapshot cells (clicking a cell travels to the snapshot), the 8 performance knobs with names from the preset, the preset name with ◀ ▶. The ↗ button opens the full editor.
- **DAW snapshots and morph (stage 06).** The DAW morph interpolates device parameters between project snapshots. For Alloy the device has a "DAW morph controls" setting: `Parameters` (default for third-party plugins: host parameters are morphed, as for any device) or `TransformPad` (default for Alloy): the DAW morph knob 0–1 is passed to `transform.pathPos` if the preset has a path, otherwise to the point position along a straight line between the snapshots the user chose in the setting (default 1 → 5). In the second mode, Alloy parameters are not written into project snapshots, so the two morphs do not fight.
- Automation of `transform.x/y`, `transform.pathPos` and the performance knobs in the DAW works like any parameter.

## Task 10.16. Additive analysis (10B)

`core/src/alloy/AdditiveAnalyser.cpp`, runs offline on a background thread, on the mono sum of the sample.

1. **Pitch.** YIN (threshold 0.15) over 2048-sample frames with hop 256 → f0 curve and a "tonal frame" flag. 5-frame median filter. The median f0 over tonal frames is the root note, if metadata has none.
2. **STFT.** 4-term Blackman–Harris window, length `N` such that the window holds at least 4 periods of the median f0 (1024 to 8192, power of two), hop `N/8`, zero padding ×2. FFT is `daw::dsp::fft` (offline, double precision).
3. **Peaks.** Local magnitude maxima above `max(−90 dB from the frame maximum, −70 dB from the local level in a ±1/6 octave window)`. Frequency and amplitude are refined by parabolic interpolation over the log magnitude of three bins. No more than 600 peaks per frame.
4. **Tracks (McAulay–Quatieri).** A peak in frame `k+1` continues a track from frame `k` if `|Δf| < max(3 %·f, 20 Hz)`; conflicts are resolved greedily by smallest `|Δf|`. A track that is not continued "dies" with a fade to zero over one frame, and a new peak is "born" with a fade-in from zero. Tracks shorter than 3 frames are discarded.
5. **Harmonic mode** (default for tonal samples): tracks are mapped to harmonic numbers `h = round (f / f0)` when the deviation is < 0.35 of a harmonic; the ratio `f / (h · f0)` is stored, so subtle inharmonicity is preserved when playing across the keyboard. Inharmonic mode stores absolute frequencies relative to the root note.
6. **Residual.** The synthesized sum of partials is subtracted from the original, and the residual is described by an envelope in 24 Bark-scale bands per frame (the noise part).
7. Result:

```cpp
struct AdditiveFrame { std::vector<float> ratio, amp; std::array<float, 24> noiseBands; float f0; };
struct AdditiveModel
{
    double hopSeconds; int rootNote; bool harmonic;
    std::vector<AdditiveFrame> frames;   // same number of partials per frame: missing ones have amp = 0
    std::string sourceHash;
};
```

The model is serialized to `.alloymodel` (binary, with a version and `sourceHash`) and cached (10.11). Analysis of a 10-second sample fits in 5 s on an M1 (in the container, report the figure), can be canceled with a button and shows progress.

## Task 10.17. Additive synthesis (10B)

- `Additive` element: a bank of sine oscillators per voice, up to `partialLimit` partials (parameter 16–600, default 128). The sine is a 4096-point table with linear interpolation; amplitudes and frequencies are interpolated linearly between model frames within the control block.
- Partials with frequency above 0.45·sr are not computed (cut off with a smooth roll-off starting at 0.40·sr), so there is no aliasing.
- Parameters: `position` (position in the model, 0–1), `speed` (−2…2, independent of pitch), `loopStart/loopEnd`, `freeze`, `partialLimit`, `oddEven` (−1…1, odd/even balance), `stretch` (inharmonicity `f_h = f0 · h · (1 + stretch · h)`), `noiseLevel` (level of the noise part: white noise through 24 bandpass filters, envelopes from `noiseBands`); `formantShift` is not made.
- The noise part is computed once per control block, only for bands with a level above −80 dB.
- Partial editor in the "Detail" tab: amplitude bars of the current frame (the first 64); dragging draws a multiplier on top of the model (`partialGain[64]`, preset data).

## Task 10.18. Spectral analysis (10B)

- STFT with a Hann window, `N = 2048`, hop `N/4`, the analysis rate is fixed at 48 kHz (the sample is resampled before analysis). Stored are the magnitude (float; 16-bit in the file, in dB with a step of 0.01) and the instantaneous frequency of each bin, derived from the phase difference between adjacent frames (standard phase vocoder).
- `SpectralModel { int fftSize, hop; std::vector<std::vector<uint16_t>> magDb; std::vector<std::vector<float>> instFreq; int rootNote; std::string sourceHash; }`.
- Importing an image (PNG/JPEG) as a spectrum: pixel brightness → magnitude, Y axis logarithmic from 20 Hz to 20 kHz, X axis is time (default 4 s), instantaneous frequency = bin center. This gives a "spectral image" sound source.

## Task 10.19. Spectral synthesis (10B)

- `Spectral` element: a phase vocoder that preserves phase coherence (identity phase locking after Laroche–Dolson: the phase of a peak bin advances by its instantaneous frequency, and the neighboring bins in its region get the same phase difference as in the analysis). Inverse FFT of size 2048, Hann window, overlap-add with hop 512.
- Pitch: bin frequencies are scaled by the ratio `f_note / f_root`, with magnitude redistributed into neighboring bins; when shifting up by more than an octave, bins above Nyquist are discarded.
- Parameters: `position`, `speed`, `freeze`, `pitchFollow` (on: plays the key's pitch; off: original pitch), `spectralTilt`, `blur` (magnitude smoothing over time), `bands` (coarsening the spectrum in frequency, a "vocoder" sound).
- The element's latency is one hop (512 samples) plus the window; it is compensated by shifting note start for the other sources only if `latencyAlign` is on (off by default, because shifting all sources feels like a sluggish attack).
- FFT on the audio thread needs a fast, allocation-free implementation: write `daw::dsp::RealFFT` (split-radix or radix-4, float, tables in `prepare`). The offline `dsp::fft` is too slow for this. If the implementer wants to use PFFFT (BSD license, compatible with AGPLv3) instead of their own, that is allowed; put the license in `third_party/pffft/`.

## Task 10.20. Spectral source morph (10B)

- Mode `sourceMixMode = Morph` is available when all enabled sources are Additive or Spectral (with any other combination the mode works as Crossfade, and the UI says so).
- Additive ↔ Additive: weights A–D from 10.6 (without the square root); for partial number `h` the amplitude is `Σ w · amp`, the frequency is the weighted mean in the log domain (`exp (Σ w · ln f)`), noise bands are the weighted mean in dB. The morph happens before synthesis: one oscillator bank sounds, not four.
- Spectral ↔ Spectral: bin magnitude is the weighted mean in dB, instantaneous frequency comes from the source with the largest weight. Additive ↔ Spectral: the additive source is converted to a spectrum (partials are distributed into bins) and morphed as Spectral.
- Smoothness: when `morphX/Y` move, the weights are smoothed over 10 ms.

## Task 10.21. Arpeggiator (10C)

- Modes Up, Down, UpDown, DownUp, AsPlayed, Random, Chord; octaves 1–4; synced rate (1/32 … 1 bar, triplets and dotted); gate; swing; latch; "Retrig on note".
- Step pattern of up to 32 steps: each step has `on`, `velocity`, `length`, `tie`, `transpose` (±24).
- The arpeggiator sits before the `VoiceAllocator` and emits events with sample accuracy from the host position (`AudioPlayHead`); when the transport is stopped, it runs from its own counter at the host tempo.

## Task 10.22. Step sequencer (10C)

- 2 sequencers as modulation sources: up to 32 steps, step value −1…1, glide between steps, synced length and rate, per-voice mode (from the note) or global mode (from the transport). The editor is bars drawn with the mouse.

## Task 10.23. Factory library (10C)

- At least **300** presets by the end of 10C (60 by the end of 10A, 150 by the end of 10B). The distribution is not strict; as a guide: Pad 50, Bass 45, Lead 40, Keys 25, Pluck 25, Arp/Sequence 30, Soundscape/FX 30, Strings/Brass/Bells/Vocal 40, Drum/Percussion 15.
- Requirements for every preset: category, subcategory, 1–4 timbres, 0–3 genres and author are filled in; all 8 performance knobs have a name and a target; at least 4 Transform Pad snapshots that differ noticeably; peak level on note C3 at velocity 100 between −12 and −3 dBFS; no clipping above 0 dBFS on 16 simultaneous notes; CPU within budget (see below).
- Library samples are only the project's own recordings or CC0 (the source and license of each file in `Factory Library/Alloy/Samples/LICENSES.md`). Samples taken from commercial synths, DAWs or commercial sample libraries are forbidden.
- Tool `tools/alloy_lint`: checks all the requirements above for a preset folder (rendering through the core), prints a table of violations, exit code ≠ 0 on any violation. Runs in CI.
- Presets are made by hand in the editor. Generating random parameters as "filler" is forbidden; variations of one preset are allowed if they have different names, tags and a noticeably different sound.

## Task 10.24. Hints and help (10C)

The DAW info bar (01) shows the description of the parameter under the mouse, and so does the plugin editor (the editor's bottom line). Descriptions live in the descriptor table (`hint`), in English by default with a Russian translation, for all parameters.

---

## CPU budgets

Measured with `tools/alloy_render`, 48 kHz, block 256, single thread. On an M1 the targets are a fraction of one core; in the container, report relative to the Prisma "Init" render with 8 voices (`prisma_render`) and check the ratios.

| Configuration (per voice unless stated otherwise) | M1, % of core | Relative to Prisma Init / voice |
|---|---|---|
| VA, 1 osc., no unison, 1 filter | ≤ 0.4 | ≤ 1.3× |
| VA, 2 osc. × unison 8, 2 filters | ≤ 2.0 | ≤ 6× |
| Sampler, 1 zone, stereo | ≤ 0.4 | ≤ 1.3× |
| Granular, 64 grains | ≤ 2.5 | ≤ 8× |
| Additive, 128 partials + noise | ≤ 2.0 | ≤ 6× |
| Spectral, FFT 2048 | ≤ 2.5 | ≤ 8× |
| Empty instance with no notes (effects rack off) | ≤ 0.1 per instance | — |
| Default 10A preset, 16 voices, rack of 3 effects | ≤ 15 per instance | — |

No factory preset exceeds 25 % of a core on 16 notes (checked by `alloy_lint`). The UI has a per-instance CPU meter (`processBlock` time over block duration, averaged over 500 ms).

## Tests

In `core/tests/alloy_tests.cpp` (core) and `tests/AlloyPluginTests.cpp` (plugin):

1. **Descriptors.** All ids are unique, `offset`s are unique and < `kMaxParams`, the default value is within range, `fromNormalised (toNormalised (v)) == v` to 1e-5 at 100 points of every Float.
2. **Real time.** With `DAW_RT_CHECKS=ON`, 2000 blocks of 256 samples with 16 notes, a preset change every 100 blocks from a background thread, Transform Pad movement and source element changes: 0 allocations on the audio thread, no NaN or Inf.
3. **Pitch.** For each element, note A3 (220 Hz) gives a fundamental of 220 Hz ± 1 cent by FFT (for Spectral and Additive, on a 440 Hz sine sample with root A4).
4. **Aliasing.** VA saw and hard sync on note C7 at 44.1 kHz: energy of inharmonic components (aliasing) below −60 dB relative to the fundamental.
5. **Morph math.** The point at snapshot `i`'s position gives exactly its values; the point at the center with 8 snapshots gives the mean of the normalized values; a Choice with two snapshots switches at weight 0.5 ± 0.02 with hysteresis (tested in both directions); excluded parameters do not change.
6. **Morph without zipper noise.** VA sine source, snapshots differ by 12 dB of source level and 3 octaves of filter cutoff; the point moves between them over 200 ms. The envelope difference between adjacent samples (RMS over 16 samples) never jumps by more than 0.5 dB per block, and energy above 8 kHz does not exceed −70 dB relative to the fundamental (no clicks). Changing a Choice element during the morph does not produce a peak overshoot of more than +1 dB above the neighboring 10 ms.
7. **Path.** A recorded path of 1000 points is simplified to ≤ 512, deviation from the original ≤ 0.005; `pathPos = 0.5` gives the point at the middle of the path in time.
8. **Source morph.** Bilinear weights at the corners give exactly one source; at the center, 4 sources at −6 dB each; total power when moving along the diagonal is constant ± 0.5 dB (uncorrelated noises).
9. **Sampler.** A 1 kHz sine sample played at +12 and −12 semitones: 2 kHz and 500 Hz ± 1 cent; a Forward loop without a click at the seam (sample jump < 0.01 with loopXfade 10 ms); WAV, AIFF and FLAC with identical content give sample-identical sound.
10. **Granular.** A fixed seed → sample-identical render across two runs; changing `density` from 10 to 100 changes RMS by no more than 3 dB.
11. **Additive resynthesis (10B).** A 440 Hz sine, 1 s: after analysis and synthesis, fundamental ± 1 cent, level ± 0.5 dB, everything else below −50 dB. A band-limited 110 Hz saw (50 harmonics): amplitudes of the first 40 harmonics within ± 1 dB, mean log-spectral error (over a 1/3-octave envelope up to 10 kHz) ≤ 1.5 dB. A sine gliding 220→440 Hz over 1 s gives one continuous track.
12. **Spectral resynthesis (10B).** Without modifications (speed 1, pitch = root), reconstruction of a saw and of white noise with SNR ≥ 40 dB after latency alignment; a ×2 stretch of a 440 Hz sine keeps the frequency ± 1 cent and the length ×2 ± 1 hop.
13. **Spectral morph (10B).** Morphing two additive models of 440 and 880 Hz sines at 0.5 gives one partial near 622 Hz (geometric mean) ± 5 cents, not two.
14. **Presets, round trip.** A random (fixed seed) `AlloyPatch` with snapshots, a path, MSEGs and zones → XML → `AlloyPatch` gives equal parameters (± 1e-6) and data. A version 1 preset with an unknown id and half the parameters missing loads without error.
15. **Plugin state.** `getStateInformation` → a new instance `setStateInformation` → a 2 s render of a MIDI phrase matches sample for sample.
16. **Missing sample.** A preset referencing a nonexistent file loads, the source is marked, the others sound; a file found by hash in another folder is picked up.
17. **Preset change.** With 8 notes sounding, a preset change via background loading: silence between the old and new sound ≤ 50 ms, output peak does not exceed the previous one by more than 1 dB.
18. **Factory presets.** All library presets load, pass `alloy_lint`, and use tags from the vocabulary.
19. **Arpeggiator (10C).** Up, 2 octaves, 1/16 at 120 BPM on a C-E-G chord: 6 notes in 0.75 s, event offsets are sample-accurate; latch holds the pattern after release.
20. **DAW integration.** `DEVICE kind="alloy"` saves and loads with the same sound; the DAW morph in `TransformPad` mode moves `transform.pathPos`, and in `Parameters` mode it does not.

## Done when

### 10A
- [ ] Alloy builds as AU, VST3 and Standalone; `auval -v aumu Aloy Dawp` passes in CI.
- [ ] Four sources with VA/wavetable, sampler and granular elements sound; WAV/AIFF/FLAC load by drag and drop.
- [ ] Transform Pad: 8 snapshots, mouse morphing, morph time, path recording and playback, automation of `transform.x/y` in the DAW and in an external AU host.
- [ ] 8 performance knobs and 2 XY pads control the sound and are visible in the compact DAW device.
- [ ] The "Browse" tab filters by attributes, and the arrow keys step through presets while playing without clicks.
- [ ] The stage 06 DAW morph drives Alloy's Transform Pad.
- [ ] Screenshots of "Browse", "Play", "Detail" and the compact view in all four themes, in the report and in `media/`.
- [ ] At least 60 factory presets, 10A CPU budgets met (table of figures in the report).
- [ ] Tests 1–10, 14–18, 20 green, old tests too.

### 10B
- [ ] Additive and spectral elements work on a user sample; analysis runs in the background with progress, the result is cached.
- [ ] Image import as a spectrum works.
- [ ] `Morph` mode blends additive and spectral sources with a single bank.
- [ ] Tests 11–13 green with the stated tolerances (actual figures in the report), CPU budgets for Additive and Spectral met.
- [ ] At least 150 factory presets.

### 10C
- [ ] Arpeggiator and 2 step sequencers run in sync with the host; test 19 green.
- [ ] At least 300 factory presets, `alloy_lint` in CI with no violations, sample licenses recorded.
- [ ] In the report: the list of "verify by ear" items with the result of the listening check.
- [ ] README updated: Alloy, its tabs, keyboard shortcuts, screenshots; `docs/ARCHITECTURE.md`: Alloy status.
