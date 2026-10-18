# 08. Sound library, Drum Machine and Sampler

## Goal

Give the DAW its own sounds and drums. This stage adds a built-in **Sampler** instrument (zones by note and velocity, loops, envelopes, the Prisma filter), a **Drum Machine** device (a pad grid where every pad has its own chain, choke groups, a mixer and sends inside the device), a factory library that we synthesize ourselves with the `tools/drumgen` tool (no third-party samples and no licensing questions), and one preset system for all devices: tags, rating, collections, preview, hot-swap mode, `.dawpack` packs.

At the end the user can: open the browser, pick the "Drums" category, preview the "909 Club" kit with one click, drag it onto empty space (a MIDI track with a Drum Machine is created), drop their own WAV from Finder onto a pad, slice a loop onto pads, save the kit as `.dawkit`, press the hot-swap button on a Reverb device and step through factory presets with the arrow keys without stopping the music.

## Scope

In scope: Sampler (DSP in `core/`, device in `app/`), Drum Machine, slicing a loop onto pads, the factory sound generator `tools/drumgen`, the loop and MIDI groove generator, library layout and index, one preset format for every device type, tag editor, rating, collections, hot-swap, audio previews of presets, import and export of `.dawpack` packs, backup of the user library, factory presets for the stage 06 effects and 50 factory chains.

Out of scope: the effects themselves and their DSP (06, only their presets are here), warp and time-stretching of loops (07; loops here are placed without stretching and are generated at the needed tempos), a full Launchkey and Maschine script (`ControlSurface`, phase 3; only a hook point here, task 8.6), granular and spectral modes (Alloy, phases 2–3), recording samples into the Sampler from an input (a finished audio clip from 05 is used), cloud sync of the library.

Depends on: the model, the browser, the `.dawpreset` and `.dawchain` formats (01, tasks 1.1, 1.5, 1.6), audio clips and file reading (05), built-in effects, sends and returns (06), the onset detector (07, if it already exists; otherwise our own, task 8.7).

---

## Task 8.1. Sampler: DSP in `core/`

Create `core/include/daw/sampler/` and `core/src/sampler/`: `SampleData`, `Zone`, `SamplerVoice`, `SamplerEngine`, `Resampler`, `SamplerParams`. No JUCE, like Prisma.

### Data

```cpp
struct SampleData                       // immutable after loading, shared via shared_ptr
{
    double sampleRate;
    int numChannels;                    // 1 or 2
    int64_t numFrames;
    std::vector<float> frames;          // interleaved; for streamed samples only the head (see 8.3)
    std::array<std::vector<float>, 3> mips;   // downsampled copies /2, /4, /8 for transposing up
    bool streamed;
    std::string path;                   // for streamed samples and for "Show in Finder"
};

struct Zone
{
    std::shared_ptr<const SampleData> sample;
    int keyLow = 0, keyHigh = 127, rootKey = 60;
    int velLow = 1, velHigh = 127;
    int keyFadeLow = 0, keyFadeHigh = 0, velFadeLow = 0, velFadeHigh = 0;  // zone crossfade width
    double tuneCents = 0, gainDb = 0, pan = 0;
    int64_t start = 0, end = -1;        // -1 = to the end
    enum class Loop { off, forward, pingPong, untilRelease } loop = Loop::off;
    int64_t loopStart = 0, loopEnd = 0, loopCrossfade = 0;   // in frames
    int roundRobinGroup = 0;            // 0 = no round-robin
    int roundRobinIndex = 0;
};
```

### Voice and parameters

- **Zone selection**: on note-on, every zone whose note and velocity ranges contain the note is taken (including the crossfade width; then the zone's gain is a linear weight). Layers play simultaneously.
- **Round-robin**: among zones with the same group (> 0) that match the note, the next one by `roundRobinIndex` is taken, cycling; a "random" mode with a fixed seed (`std::mt19937` in the engine, the seed is set in tests). The counter is stored per group.
- **Pitch**: `rate = sampleRate / outRate * 2^((note − rootKey + tuneCents/100 + transpose + fine + pitchBend·range)/12)`. Transpose ±48 semitones, fine ±100 cents, pitch-bend range 0–24.
- **Glide**: time 0–2000 ms, "always" and "legato only" modes, along the log of frequency (as in Prisma).
- **Voice modes**: poly (1–64 voices, default 16), mono, legato (envelope retrigger off, the sample position continues). Voice stealing: the oldest voice in release first, then the quietest. A stolen voice fades out over 5 ms instead of being cut.
- **Envelopes**: two ADSRs from `daw::dsp::Envelope` (amplitude and filter), plus hold and attack/decay curvature. Amplitude velocity sensitivity 0–100 %. The filter envelope acts on the cutoff within ±96 semitones.
- **Filter**: reuse `daw::dsp::SVF` / `SynthFilter` from Prisma (LP/HP/BP/notch, 12/24 dB), cutoff, resonance, drive, key tracking 0–100 %. Do not change Prisma code; if a new method is needed, it is added without changing Prisma's behavior (Prisma tests stay green).
- **LFO**: one per voice (sine/tri/saw/square/S&H, tempo sync), destinations: pitch, cutoff, amplitude, pan.
- **Start modulation**: `sampleStartMod` 0–100 % of the length, from velocity or random; the start is clamped to `end − 64`.
- **Loop with crossfade**: in forward mode the last `loopCrossfade` frames before `loopEnd` are mixed with the frames before `loopStart` using an equal-power law. The crossfade is no longer than `loopStart − start` and half the loop; a `clampLoop` function in `core/` with a test. PingPong needs no crossfade. `untilRelease`: loop until note-off, then play the tail.
- **Parameters** are described by an array of descriptors, like Prisma's `daw::synth::parameterDescriptors()` (id, name, range, default), so that automation (06) and the 8 knobs in the panel work the same way.

### Resampling (decision)

`Resampler` with two qualities, chosen in the device ("Normal" / "High"), default "Normal":
- **Normal**: 4-point cubic Hermite interpolation. Cheap, with no noticeable artifacts when transposing down and moderately up.
- **High**: windowed sinc, 16 taps (8 on each side), Kaiser window β = 8, a 512-phase table with linear interpolation between phases. The table is built once in `prepare`.
- **Anti-aliasing when transposing up**: mip copies of the sample (/2, /4, /8), built at load time with a half-band FIR filter (not on the audio thread). A voice with `rate > 1` takes level `floor(log2(rate))` (at most 3) and divides `rate` by 2^level. No per-voice oversampling: mip copies are cheaper and give the same result. Streamed samples have no mip copies: in "High" quality the sinc for them is scaled by the cutoff `1/rate` (no longer window is needed, shifting the kernel is enough); in "Normal" aliasing is accepted.
- Measure voices per CPU with a `tools/sampler_render` tool similar to `tools/prisma_render` (16 voices, 44.1 kHz, buffer 256) and give the numbers in the report.

## Task 8.2. Sampler as a device

- `DEVICE kind="sampler"` in the model. A child node `ZONES` with `ZONE` children (fields as in `Zone`, `samplePath` relative to the project folder or the library, `sampleHash`). Parameters are `DEVICE` properties keyed by descriptor id.
- Engine: `SamplerProcessor` (a `juce::AudioProcessor` wrapper around `SamplerEngine`, like Prisma in 01). The zone set is published as a snapshot `std::shared_ptr<const ZoneMap>` (rule 00, section 2); playing voices hold their own `shared_ptr` to the sample, so replacing zones during playback does not break the sound.
- Samples dropped into the project are copied into `Samples/Imported/` (as in 05). Samples from the factory library are not copied; the reference looks like `factory:Drums/909 Club/Kick.wav`.
- **Compact view** (width 520 px): the waveform of the selected zone with start, end, loopStart, loopEnd markers and the crossfade region (dragged with the mouse, `Shift` for fine control, snap to zero crossings on by default), 8 knobs: transpose, cutoff, resonance, attack, release, glide, sample start mod, volume.
- **Full view** (↗ window): a "note × velocity" zone map (rectangles, edges are draggable, `Cmd+A`, `Delete`), a zone list with fields, tabs "Envelopes", "Filter", "LFO", "Voices".
- **Multisample import**: dropping a folder or several files onto the Sampler. The note is taken from the file name by the `parseNoteName` function in `core/`: it looks for the last occurrence of the form `C#3`, `Db3`, `c3`, `A-1`, `_60_` (a number 0–127); octave convention: C3 = 60, with a "C4 = 60" switch in the dialog. Velocity comes from the suffixes `v1..v16`, `pp/p/mp/mf/f/ff`, `soft/med/hard`; round-robin from `rr1..rr8`. Then the zones are laid out: each zone covers notes from the midpoint between its root and the neighbor below to the midpoint with the neighbor above. If no note is found in any name, the files are laid out chromatically from C3 in alphabetical order, and the info bar says so.
- **Loop from the file**: if a WAV has a `smpl` chunk, or an AIFF has `MARK`/`INST`, the loop points and the root note are taken from there.

### Edge cases

- Sample not found when opening a project: the zone stays in the model, is drawn in red with the file name, and the zone's sound is silence. The menu command "Find Missing Samples…" searches by name and `sampleHash` in the project folder, Places and the library.
- The file's sample rate differs from the device's rate: accounted for in `rate`, the file is not converted.
- A file longer than 2^31 frames or with more than 2 channels: the first 2 channels are loaded, with a message in the info bar.
- Empty zone map: the Sampler passes MIDI through and stays silent; the compact view shows "Drop a sample or folder here".

## Task 8.3. Disk streaming versus memory

- A threshold in the settings ("Library"): files shorter than 20 s **or** smaller than 32 MB after decoding are loaded fully into RAM. The rest are streamed.
- A streamed sample keeps its head (the first 65,536 frames) in RAM, plus a ring buffer of 3 × 32,768 frames per voice. A background thread `SampleStreamer` (one per application, `juce::TimeSliceThread`) fills the buffers ahead of time. The audio thread only reads ready frames; if data is missing (the disk did not keep up), the voice outputs silence and sets an atomic `underrun` flag, and the UI shows "Disk too slow" in the info bar. No locks and no waiting on the audio thread.
- The head lets the sound start instantly; `sampleStartMod` for streamed samples is limited to the head.
- File reading is `juce::AudioFormatReader` in `app/`; `core/` gets an `ISampleSource` interface with a `read (int64_t frame, float* dst, int n)` method so tests can substitute the disk.

## Task 8.4. Drum Machine: model and engine

```
DEVICE kind="drummachine"  name  viewOffset=36  selectedPad=36  showMixer=1
├── PADS
│   └── PAD  note=0..127  name  colour=<palette index>  choke=0 (0 = none, 1..16)
│            volumeDb=0  pan=0  mute=0  solo=0  outNote=60 (the note the pad's instrument receives)
│      ├── DEVICES        (the pad's chain: first the instrument, then effects; same format as a track)
│      └── SENDS  └── SEND  returnId  levelDb=-inf
└── RETURNS
    └── RETURN  id  name  volumeDb=0  └── DEVICES
```

- 128 pads, a visible 4 × 4 window. A pad is created in the model lazily when something is put on it; an empty pad has no node.
- **Notes**: 128 pads cover all MIDI notes 0–127. A 4 × 4 window of pads is visible; it scrolls by rows of 4 (mouse wheel or the overview of all 128 on the left). `viewOffset` is the note of the bottom-left pad, by default C1 (36 under the C3 = 60 convention), then rightward and upward. A pad receives MIDI for its note and passes `outNote` to the instrument (default 60, so the Sampler plays the sample at its original pitch).
- **Choke**: a note-on of a pad in group `g` silences all sounding voices of other pads in the same group within 3 ms (a `chokeAll` command to the pad's instrument; for the Sampler a fast release, for third-party plugins all-notes-off over their MIDI). Typical case: open and closed hi-hat.
- **Mute and solo** inside the device are independent of the track.
- **Pad mixer**: volume, pan, sends to the device's returns. The device's returns (up to 6) have their own effect chains; their output is summed with the pads inside the device before the device output. No pre-fader sends (post-fader only), to avoid multiplying variants.
- **Engine**: `DrumMachineProcessor` holds a `DrumMachineGraph` snapshot (like `PlaybackGraph` in 01: immutable, published atomically, the old one is deleted on the message thread). Pads with no sounding voices and no effect tail are skipped: each pad has a silence counter, and after 2 s below −120 dBFS with no incoming notes its chain is not called. Buffers for all pads are allocated in `prepare`.
- **PDC inside the device**: the latencies of pad chains are aligned the same way as tracks in 06 (the device reports the maximum outward).
- **Edge cases**: deleting a pad that is currently sounding fades it out over 5 ms; changing `choke` during playback takes effect from the next note-on; a pad instrument that loads asynchronously (a third-party plugin) stays silent until ready, and the pad is drawn with an indicator; a nested Drum Machine in a pad chain is forbidden (the drop is not highlighted), to avoid recursion.
- A MIDI clip on a track with a Drum Machine shows, in the piano roll (02), rows with pad names instead of a keyboard, only non-empty pads (an "all" toggle).

## Task 8.5. Drum Machine: interface

- **Compact view** (width 640 px): on the left the visible 4 × 4 window, to the right an overview of all 128 pads (32 rows of 4 small squares, a dot marks a pad with content, a frame marks the visible window and can be dragged with the mouse), then the selected pad's panel: name, a miniature of the pad's chain (devices in one row, a click expands the pad's chain in the second row of the detail panel), the pad's mixer.
- **A pad** is drawn in the pad's color (theme palette), with its name, note number (`C1`), M and S buttons, and a choke group indicator. A playing pad flashes (brightness by velocity, 150 ms decay). A click on a pad selects it and plays the note at velocity 100 (if preview is on); `Alt`-click only selects.
- **Dragging onto a pad** (one transaction per drop):
  - an audio file from the browser or Finder → a Sampler with that sample is put on the pad (if the pad already has a Sampler, the sample in its first zone is replaced; `Cmd` while dropping adds it as a layer);
  - an instrument, instrument preset or chain → replaces the pad's chain;
  - an effect → appended to the end of the pad's chain;
  - several files → laid out on pads starting from the target one;
  - pad onto pad → move, with `Alt` copy, with `Cmd` swap.
- Right-click on a pad: Rename, Color, Choke Group ▸, Copy, Paste, Clear, Save Pad as Preset…
- **Saving a kit**: the device ▾ menu → "Save Kit…" → `User Library/Drums/<name>.dawkit`. This is an ordinary preset in the unified format (8.10) with `deviceKind="drummachine"`: samples that are not in the factory library are copied next to the kit (`<name>.dawkit` + `<name> Samples/`), references are relative.

## Task 8.6. Controller pads

- `app/Source/control/PadMapping.h`: a "MIDI channel + input note → pad" table per MIDI input device, identity by default. Built-in profiles: Novation Launchkey MK3/MK4 (Drum Mode: notes 36–51, channel 10), Maschine MK3 in MIDI mode (default NI template: notes 36–51, channel 1). The profile is chosen in the MIDI settings for the input.
- Scrolling the device window from the controller and lighting pads in color are groundwork: an interface `IPadFeedback { setPadColour (note, rgb); setViewOffset (note); }`, which the `ControlSurface` layer (phase 3) implements later. In this stage only an empty implementation and calls from the Drum Machine when a color changes and when the window scrolls.

## Task 8.7. Slicing a loop onto pads

- Right-click on an audio clip (05) or an audio file in the browser → "Slice to Drum Machine". Dialog: mode (by transients / by beats 1/4, 1/8, 1/16 / equal parts 2–64), sensitivity 0–100 % (for transients), "Create MIDI clip" (on by default).
- **Onset detector**: if `core/` already has the stage 07 detector, use it. If not, `core/include/daw/analysis/OnsetDetector.h`: spectral flux. Hann window 1024, hop 256, `FFT` from `core/src/FFT.cpp`, log magnitude `log(1 + 10·|X|)`, positive difference from the previous frame, summed over bins; adaptive threshold = median over ±8 frames × (1 + 2·(1 − sensitivity)) + δ; peaks are local maxima with a minimum spacing of 50 ms. The position is refined backward to the nearest zero crossing within 5 ms. Stage 07 can later take over the same class.
- Result: a new MIDI track (or replacing the device on the selected one), a Drum Machine where pad `i` is a Sampler with the same file reference and the slice's `start/end` (the file is not copied in pieces), a short 2 ms fade at the end of the slice, and a MIDI clip that plays the slices in the original order and rhythm. More than 128 slices: the first 128 are taken, with a message in the info bar.
- Source tempo for the MIDI clip: from the file metadata, from the name (`_120bpm`, `120 BPM`), otherwise from the clip length and number of beats; if that fails, the project tempo.

## Task 8.8. Factory sounds: `tools/drumgen`

Principle: all factory drums, percussion and loops are synthesized by our code, deterministically, from recipes in the repository. No third-party samples by default, so there are no licensing questions: the output is distributed under the same license as the project.

### How the tool is built

- `core/include/daw/drumgen/`: DSP blocks and recipes (no JUCE); `tools/drumgen.cpp`: the console program. Writes WAV 24-bit, 48 kHz, mono for kick, snare, toms, clap; stereo for cymbals, hats with width, loops. WAV is written by our own small writer in `core/` (RIFF, `fmt `, `data`, `smpl` with the root note), so the tool does not depend on JUCE.
- A recipe is JSON in `content/drumgen/recipes/<kit>.json`: a list of sounds, each with a generator type and parameters. Noise is `xoshiro128**` with a seed from the recipe. Running again produces a byte-identical file (test).
- Blocks: a sine with a pitch envelope (exponential `f(t) = f_end + (f_start − f_end)·e^(−t/τ)`), a click generator (an impulse through a BP), white and pink noise, 6 square oscillators at inharmonic frequencies (808 metal), the SVF from Prisma, a `tanh` waveshaper and hard limiting, AD/AHD envelopes with curvature, a bitcrusher and sample-rate reduction (lo-fi), a simple modal resonator model (a bank of 4–12 resonators, for toms, bells, cowbell, acoustic drums), a short reverb (room 0.2–0.8 s) only for "acoustic" kits, final peak normalization to −1 dBFS and a 5 ms fade-out.

### Recipes (base values, each kit varies them)

Starter recipes for eight hits exist in the Codex prototype: [`docs/reference/codex/touchdrums/Processor.cpp`](../reference/codex/touchdrums/Processor.cpp), function `buildKit`. They can be carried over as the "Prisma Touch" kit after recalculating them for the blocks below.


| Sound | Recipe |
|---|---|
| Kick 808 | sine, pitch 150 → 48 Hz, τ = 40 ms, amplitude AD 1 ms / 600–1500 ms, `tanh` drive 1.5, click 3 ms at 3 kHz −18 dB |
| Kick 909 | sine 230 → 55 Hz, τ = 25 ms, decay 350 ms, noise click 8 ms through HP 2 kHz −10 dB, drive 2.5 |
| Kick acoustic | modal model (membrane: 60, 96, 128, 155 Hz), strike is 4 ms of noise through LP 1.5 kHz, decay 250 ms, room 0.25 s −20 dB |
| Snare 808 | two sines 180 and 330 Hz, decay 120 ms; noise through HP 1.2 kHz, decay 180 ms, mix 0.6 |
| Snare 909 | body 200 → 170 Hz, decay 90 ms; noise through BP 5 kHz Q 0.7, decay 220 ms, drive 1.8 |
| Clap | noise through BP 1.2 kHz Q 1.5; 4 bursts 10 ms apart with 8 ms decay, then a 180 ms tail |
| Closed hat | 6 squares (205.3; 304.4; 369.6; 522.7; 540; 800 Hz ×1.0–1.6 per kit) through BP 10 kHz and HP 7 kHz, decay 40–60 ms |
| Open hat | the same, decay 350–600 ms; in the kit it is in the same choke group as the closed hat |
| Tom low/mid/high | sine 120/170/240 → ×0.7, τ = 60 ms, decay 400 ms; for acoustic kits the modal model |
| Crash / Ride | metal from 6 squares + noise, BP 6–9 kHz, decay 1.8 s / 2.5 s; ride with a distinct "bell" of 2 modes at 780 and 1050 Hz |
| Rim / Clave | resonator 1.7 kHz / 2.5 kHz, decay 30–60 ms |
| Cowbell | two squares 540 and 800 Hz through BP 2.6 kHz, decay 300 ms |
| Shaker / Tambourine | noise through HP 6 kHz, an envelope of 2–3 grains of 20 ms each; tambourine adds 6-square metal |
| Conga / Bongo | modal model, 2–4 modes, decay 150–300 ms, noise strike |
| Lo-fi versions | any sound above + bitcrusher 8–12 bits, sample rate 11–22 kHz, LP 6 kHz, light saturation |

### Factory library contents (targets)

- **Kits**: 20 kits of 16 pads: "808 Classic", "808 Deep", "909 Club", "909 Hard", "Acoustic Room", "Acoustic Dry", "Brush Jazz", "Lo-fi Tape", "Lo-fi Dusty", "Minimal", "Techno Warehouse", "House Garage", "Trap", "Drill", "Boom Bap", "Electro", "Industrial", "Percussion World", "Latin Perc", "Glitch". The pad layout in each is standard (kick C1, rimshot C#1, snare D1, clap D#1, closed hat F#1, open hat A#1 and so on, as in GM drums), so MIDI grooves fit every kit.
- **One-shots**: at least 400 (kit sounds plus variations: 3–5 pitch and decay variants for each type).
- **Loops**: 100. Generated by the same tool: 25 patterns (from `content/drumgen/patterns/*.json`, 1/16 grid, velocity, swing) × kit × tempos 90, 120, 128, 140, 174. The name includes the tempo: `Boom Bap 01 90bpm.wav`. Loops are 2 or 4 bars, exact in length.
- **MIDI**: 50 files: 30 drum grooves (the same patterns plus variations, GM notes), 20 chord progressions (I–V–vi–IV, ii–V–I and others, in C, with the key in the name). Written by a small SMF writer in `core/`.
- **Sampler presets**: 30 multisamples from Prisma renders (the same `tools/prisma_render` over notes C1–C6 every major third): "Prisma Keys", "Prisma Pad" and so on, to show multisample mode.

### CC0 packs (optional)

- Third-party samples are allowed **only** under the CC0 1.0 license. Each pack is recorded in `content/thirdparty/manifest.json`: name, source URL, retrieval date, SHA-256 of the archive, full license text (or a reference to a `LICENSE-<pack>.txt` file next to it), file list. The `scripts/fetch-cc0.sh` script downloads them and checks hashes; without a manifest entry a file does not get into the build (CI check: every file in `ThirdParty/` is in the manifest).
- Packs are not required in this stage; if the implementer cannot verify the license, do not add it.

## Task 8.9. Library layout, delivery, index

```
Factory Library/                      (read-only)
├── Drums/<kit>.dawkit                the kit references ../Samples/Drums/<kit>/
├── Samples/Drums/<kit>/*.wav
├── Samples/One-shots/<type>/*.wav     Kick, Snare, Clap, Hat, Tom, Cymbal, Perc, FX
├── Loops/<genre>/*.wav
├── MIDI/Grooves/*.mid  MIDI/Chords/*.mid
├── Presets/<device>/<folder>/*.dawpreset    Prisma, Sampler, EQ, Compressor, …
├── Chains/*.dawchain
├── Previews/                          mirrored structure, *.ogg (see 8.12)
└── library.json                       version, date, content hash
User Library/  (~/Music/Prisma DAW/User Library, as in 01)
├── Presets/  Chains/  Drums/  Samples/  Templates/  Themes/  Collections.json  Backups/
```

- **Delivery**: the base factory library (all kits, one-shots, MIDI, presets, previews) lives in `DAW.app/Contents/Resources/Factory Library/`. DMG budget: the library adds no more than **150 MB** compressed. Rough figures: one-shots 400 × ~150 KB ≈ 60 MB, previews ≈ 15 MB, presets < 5 MB. Loops (100 × ~1.5 MB ≈ 150 MB) and CC0 packs ship as a **separate pack** "Loops & Extras" (`.dawpack`, task 8.13), downloadable with a button in the "Library" settings or manually from the GitHub release. If the budget is exceeded, cut loops in the bundle, not one-shot quality.
- **When it is generated**: the CMake target `factory_library` runs `drumgen` and the preview render during Release builds and in release CI; the result is cached by the hash of the recipes (not rebuilt without changes). Generated WAVs are not committed to git, only the recipes.
- **Index**: `LibraryIndex` in `app/Source/library/`. We do not pull in SQLite; a file `~/Library/Application Support/Prisma DAW/library-index.json` (or a binary cache) with records `{ path, kind, deviceKind, name, tags[], rating, colour, mtime, size, previewPath, durationSec, rootNote, tempo }`. Built in the background on first launch, then updated incrementally by `mtime`. The factory part of the index is prebuilt in the bundle's `library.json` and is not scanned. Search runs over the in-memory index; on 20,000 records the response is < 30 ms (test).

## Task 8.10. Unified preset format

Extend `.dawpreset` from 01 to version 2 (the loader opens version 1, migration with a test):

```
DAWPRESET version=2 deviceKind="prisma|sampler|drummachine|alloy|effect:<id>|plugin|chain"
          name  author  created  modified  uuid
          pluginDesc (for plugin)  state(base64)  (or a child DEVICE/DEVICES for sampler, drummachine, chain)
├── TAGS  └── TAG  group="type|genre|character|source"  value="Bass"
├── ATTR  rating=0..5  colour=-1..6  comment=""
└── SAMPLES └── SAMPLE  ref="factory:…|relative:…"  hash
```

- One `PresetFile` class reads and writes all kinds. `.dawchain` and `.dawkit` are the same format with a different extension (for filtering in the browser and Finder). Built-in effects from 06 save `deviceKind="effect:reverb"` and so on.
- **Third-party plugin presets**: `state` from `getStateInformation`, `pluginDesc` to find the plugin. In the browser such presets live in the plugin's folder; if the plugin is missing, the preset is grayed out with a hint.
- **Tags**: a fixed vocabulary in `app/Resources/tags.json` (type: Bass, Lead, Pad, Keys, Pluck, Drums, FX, Vocal, Mix, Master…; genre; character: Warm, Bright, Dark, Wide, Dirty, Clean…) plus the user's own tags. Presets in `Factory Library` get tags from the recipes and the vocabulary during generation.
- **Attributes** of factory presets are not changed in the file: the rating, color and user tags of factory presets are stored in `User Library/Overrides.json` by `uuid`.

## Task 8.11. Browser: tags, rating, collections, hot-swap

- New browser categories: **Drums** (kits, then one-shots by type), **Loops**, **Sampler**. The "Effects" category shows the built-in effects from 06 and their factory presets in folders.
- **Tag filter**: above the list, a strip of tag chips for the current category; a click toggles one, several chips mean intersection. The search field searches by name and tags (`bass warm` → both). A "rating ≥ N" filter.
- **Tag editor**: right-click → "Tags and Properties…" (or `Cmd+Shift+T`): group chips, a new tag field with autocomplete, star rating, color, comment. Works on several selected items at once.
- **Favorites and collections**: the 7 colors from 01 become named collections (renamed by double-click: "For live", "Basses of the week"). Stored in `User Library/Collections.json` as lists of paths and `uuid`s. An item can be in several collections.
- **Hot-swap**: in the header of every device (Prisma, Sampler, Drum Machine, built-in effects, plugins) and of every Drum Machine pad, a ⇄ button. Pressing it puts the browser into selection mode for that device: only matching presets are shown (for a pad: samples and instruments), with a banner at the top "Replacing: Reverb on track Vocals · Esc to exit". Selecting an item loads it into the device immediately, the up/down arrows step through, `Enter` or pressing ⇄ again exits. All loads in one hot-swap session are one undo transaction; `Esc` restores the state from before entering. Loading plugin state happens on the message thread, without rebuilding the graph if the device type did not change.
- **Recent** and **preview** from 01 work for all the new kinds.

## Task 8.12. Preset previews

- For every factory instrument and effect preset there is a preview next to it: `Previews/<preset path>.ogg` (Ogg Vorbis, 44.1 kHz, stereo, ~96 kbit/s, 2–4 s). An Ogg Vorbis decoder is in JUCE (`OggVorbisAudioFormat`, BSD license, compatible with AGPLv3); FLAC is the fallback format for user previews.
- What a preview plays: instruments play the phrase from 01 (a C minor arpeggio) for melodic ones, a single hit for one-shots, the "Basic 1" groove for kits; effects play a factory "dry" excerpt (a drum loop or a vocal-like synth fragment from `drumgen`/Prisma, 3 s) through the effect with the preset.
- Factory previews are rendered by the `tools/preset_preview` tool (offline, the same DSP), part of the `factory_library` target. User previews are rendered in the background when a preset is saved (for built-in devices; for third-party plugins an offline plugin instance on a background thread with a 10 s timeout, on error there is no preview).
- In the browser, selecting a preset with the headphones button on plays the preview file immediately (without creating a device instance). If there is no preview, the behavior from 01 applies (a live instance for Prisma), otherwise silence.

## Task 8.13. `.dawpack` packs and backup

- `.dawpack` is a ZIP (`juce::ZipFile` for reading, `ZipFile::Builder` for writing) with `pack.json` (`name, author, version, created, licence, description, files[] { path, sha256 }`) and folders as in `User Library`.
- **Export**: select items or a collection → "Export Pack…". Samples referenced by presets are included; factory ones (`factory:`) are not, because everyone has them.
- **Import**: double-click the file in Finder (document type in `Info.plist`) or drop it into the browser. The pack is unpacked into `User Library/Packs/<name>/`, hashes are checked, paths with `..` and absolute paths are rejected, on a name clash a "Replace / Keep Both" dialog appears. Progress is shown in the info bar.
- **Backup**: the "Library" settings → "Back Up…" writes the whole `User Library` (without `Backups/`) into a `.dawpack`; "Restore from Backup…". An automatic backup once a week into `User Library/Backups/`, the last 4 are kept (enabled in the settings, on by default).

## Task 8.14. Factory effect presets and chains

- For every stage 06 effect, at least 10 factory presets with tags, in `Factory Library/Presets/<effect>/`: **EQ**, **Compressor**, **Reverb**, **Delay**, **Distortion/Saturator**, **Chorus**, **Flanger**, **Phaser**, **Filter**, **Gate**, **Limiter**, **Bitcrusher**, **Auto-pan**. If some effect does not exist in 06, do not make presets for it and say so in the report.
- Example names (10+ for each): EQ: "Vocal Presence", "Kick Tight", "Low Cut 80", "Air Shelf", "Telephone"; Compressor: "Vocal Gentle", "Drum Bus Glue", "Bass Even", "Parallel Smash"; Reverb: "Small Room", "Plate Vocal", "Hall Large", "Shimmer Pad", "Gated 80s"; Delay: "1/8 Dotted", "Ping-Pong 1/4", "Slapback", "Tape Echo", "Dub Throw"; Distortion/Saturator: "Warm Tape", "Tube Push", "Fuzz", "Bass Grit"; Chorus: "Juno Wide", "Subtle Double"; Flanger: "Jet", "Slow Sweep"; Phaser: "Classic 4-stage", "Stereo Swirl"; Filter: "LP Sweep 1 bar", "HP Riser"; Gate: "Drum Room Cut", "Trance Gate 1/16"; Limiter: "Master −1 dBTP", "Loud Club"; Bitcrusher: "8-bit", "Sample Rate 8k"; Auto-pan: "1/8 Hard", "Slow Drift".
- Presets are described in `content/presets/effects/*.json` (parameter ids from the stage 06 effect descriptors) and assembled into `.dawpreset` by the `factory_library` target, so that manual edits stay readable.
- **50 factory chains** (`Factory Library/Chains/`), groups: Vocals ("Vocal Clean", "Vocal Radio", "Vocal Wide Pop", "Vocal Lo-fi"…), Drums ("Drum Bus Punch", "Lo-fi Tape", "Room Crush"…), Bass, Synths and pads, Master ("Master Gentle", "Master Loud"), Creative ("Tape Stop FX", "Underwater"). Each chain is 2–6 built-in effects, no third-party plugins.

## Task 8.16. Playing on the trackpad

An idea from the Codex prototype (Prisma Touch): on a Mac you can hit the Drum Machine pads with your fingers right on the trackpad, with several fingers at once. No DAW we know of does this. Reference working code: [`docs/reference/codex/touchdrums/TouchSurface_mac.mm`](../reference/codex/touchdrums/TouchSurface_mac.mm).

- `app/Source/control/TouchSurface.h` + `TouchSurface_mac.mm` (on other platforms an empty stub, `supported = false`). A transparent `NSView` via `juce::NSViewComponent` over the visible 4 × 4 window of the Drum Machine. Public API only: `allowedTouchTypes = NSTouchTypeMaskIndirect`, `wantsRestingTouches = YES`, each touch gets its own token from `touch.identity`. No private frameworks and no global event taps.
- Enabling: a "Trackpad" button in the Drum Machine header (an icon with fingers, tooltip "Play the pads with your fingers on the trackpad. Esc to exit"). In this mode, with the cursor over the device, the whole physical trackpad maps onto the 4 × 4 window (AppKit coordinates go bottom to top; the top row on screen is the top row of the trackpad). Exit: `Esc`, pressing the button again, losing window focus (`NSWindowDidResignKeyNotification`); all sounding touches then get "released".
- Behavior: a new touch = one note-on for the pad under the finger; sliding moves only the touch marker and does not retrigger the sound; lifting = note-off (important for pads with a long Sampler in Gate mode). In this mode the mouseDown/mouseUp and scroll events that macOS sends from tap-to-click are swallowed, to avoid a double hit.
- Hit velocity: `NSTouch` does not report finger pressure, so velocity is fixed, with a "Velocity" slider 1–127 next to the button (default 100), saved in the application settings.
- Touch events go to the engine through the same SPSC command queue as pad clicks (00-rules, section 2), with a message thread timestamp; recording into a clip works as with a MIDI keyboard (02).
- Touch markers are drawn as circles in `theme().accent` with transparency; the pad flashes as it does from a note.
- Test: a fake touch source (an `ITouchSource` interface) feeds a began/moved/ended sequence for 3 fingers; the test checks that exactly 3 note-ons and 3 note-offs of the right pads were played, sliding produces no extra hits, and losing focus releases all notes. The user checks the physical trackpad on a Mac.

## Task 8.15. Order of work

Tasks can be handed out in batches; the dependencies are:

1. 8.1 → 8.2 → 8.3 (the whole Sampler; without it there is no Drum Machine).
2. 8.8 (`drumgen`) can be done in parallel with 8.1: it only needs Prisma's DSP blocks and the WAV writer.
3. 8.4 → 8.5 → 8.6 → 8.7 (Drum Machine and slicing).
4. 8.10 → 8.11 → 8.12 → 8.13 (preset format, browser, previews, packs).
5. 8.9 and 8.14 last: they need finished formats and all generators.
6. 8.16 (trackpad) after 8.5, at any time.

If the work does not fit in one session, a sensible split: "Sampler" (8.1–8.3), "Drum Machine" (8.4–8.7), "Generators" (8.8), "Presets and library" (8.9–8.14).

## Tests

In `core/tests/` and `tests/`:

1. **Resampling.** A 1 kHz sine, transposed −12, +7, +19 semitones: the frequency by FFT is within ±0.1 %; at +19 in "High" quality with mip copies the total alias energy is below −80 dB relative to the fundamental, in "Normal" below −50 dB.
2. **Zones and velocity.** A map of 3 zones by note and 2 velocity layers: for 20 (note, velocity) pairs the expected zones and crossfade weights are chosen.
3. **Round-robin.** 6 hits on a note with a group of 3 zones give the sequence 0,1,2,0,1,2; random mode with seed 42 gives a fixed sequence.
4. **Loop.** A sample with a loop and a 256-frame crossfade: over 10 seconds there is no discontinuity above the given threshold at the seam (the maximum difference of adjacent samples in the seam window ≤ 1.5 × the maximum in the rest of the loop); `clampLoop` at edge values.
5. **`parseNoteName`.** 30 file names (`Piano_C#3_v2.wav`, `bass-a1.aif`, `Str 060 rr2.wav`, `Kick.wav`) give the expected note, velocity and round-robin or "not found"; conventions C3 = 60 and C4 = 60.
6. **Streaming.** A fake `ISampleSource` with a delay: when the disk keeps up, the output matches the RAM version sample for sample; with a stalled disk: silence, the `underrun` flag, no locks (`DAW_RT_CHECKS=ON`, 0 allocations).
7. **Choke.** An open hat, then a closed hat in the same group: after 3 ms + 1 block the open hat's level is below −90 dBFS.
8. **Drum Machine without allocations.** 2000 blocks, 16 pads playing, a parallel thread changes samples and pad chains: 0 allocations on the audio thread, no NaN/Inf, no more than 3 live `DrumMachineGraph` instances.
9. **Pad notes.** Note 36 plays pad 36 (bottom-left at `viewOffset=36`), note 52 plays pad 52, notes 0–127 are all reachable; the Launchkey profile maps input notes to the right pads.
10. **Onsets.** A synthetic `drumgen` loop with 16 known hits: 16 ± 0 found, positions within ±5 ms; on white noise at 50 % sensitivity no more than 2 false detections per second.
11. **Slicing.** Slicing a 2-bar loop by 1/8 gives 16 pads and a MIDI clip whose render matches the original loop (RMS difference < −60 dB).
12. **`drumgen` determinism.** Two runs on the same recipe give byte-identical WAVs; the SHA-256 of several sounds matches values fixed in the test (changing a recipe requires a deliberate hash update).
13. **Recipes.** Kick 808: fundamental frequency after 200 ms within 45–52 Hz, peak −1 dBFS ± 0.1; hats: 90 % of the energy above 5 kHz; no NaN, no DC above −60 dB.
14. **Preset format.** A preset of each kind (Prisma, Sampler, Drum Machine, effect, plugin stub, chain) is written and read back with the same tags and attributes; a version 1 file from 01 migrates; `Overrides.json` does not change the factory file.
15. **Index and search.** 20,000 synthetic records: search by name and two tags < 30 ms with the expected results; an incremental update after the `mtime` of one file changes one record.
16. **Hot-swap.** Five preset loads in a row, then `Esc`: the device state equals the original; without `Esc`: one transaction, `undo` returns the original.
17. **`.dawpack`.** Export and import of a kit with a user sample: files and hashes match; an archive with `../evil.wav` and an absolute path is rejected.
18. **Project save.** A project with a Drum Machine (4 pads, choke, a send to a device return) and a Sampler is saved and loaded: 4 bars of audio match sample for sample.
19. **Library contents.** A check of the built `Factory Library`: 20 kits of 16 pads, ≥ 400 one-shots, 50 MIDI files, ≥ 10 presets for each 06 effect, 50 chains, every preset has a preview, every file in `ThirdParty/` is in the manifest with CC0; the size of the bundled part is within budget.

## Done when

- [ ] The Sampler plays a multisample imported by dropping a folder, with correct notes from the names; a loop with crossfade does not click (tests 1–5, screenshot of the full view).
- [ ] A large sample (> 32 MB) streams without dropouts at buffer 256 on a Mac; in the container, test 6.
- [ ] Drum Machine: 128 pads with window scrolling, choke, mute/solo, sends to the device's returns, dropping samples from the browser and Finder, a kit is saved as `.dawkit` and opens (tests 7–9, 18, screenshots in all four themes).
- [ ] A loop is sliced onto pads by transients and by beats, and the MIDI clip reproduces the source (tests 10–11).
- [ ] `tools/drumgen` builds the factory library deterministically; the contents match the targets, the DMG budget is met, size figures are in the report (tests 12, 13, 19).
- [ ] A preset is saved and found by tags for every device kind, including a third-party plugin and a chain; rating, color and collections work for factory items too (tests 14, 15).
- [ ] On a Mac the Drum Machine pads can be played with several fingers on the trackpad, `Esc` exits the mode (test 8.16, checked by the user).
- [ ] Hot-swap steps through presets with the arrow keys on a device and on a pad, `Esc` restores the previous state (test 16, screenshot).
- [ ] Factory preset previews play instantly with the headphones button on.
- [ ] `.dawpack` export and import, backup and restore of `User Library` work (test 17).
- [ ] Every 06 effect has at least 10 factory presets, and there are 50 chains.
- [ ] `tools/sampler_render` figures (CPU for 16 voices) are in the report; all tests are green, old ones too; CI is green, the DMG builds.
- [ ] README updated: Sampler, Drum Machine, library, hot-swap, keyboard shortcuts, screenshots.
