# 12. Vocal: pitch correction and pitch editing (draft)

> A draft written on 17.10.2026 after reviewing the Codex prototype (Prisma Voice, see [`docs/reference/codex/`](../reference/codex/README.md)). Before the stage starts it needs to be brought up to the level of the other specs: refine the UI with concept screenshots, test numbers and the split into sessions. The direction and the decisions below are already agreed.

## Goal

A built-in vocal processor, **"Vocal"**, that knows the project scale and tuning. Two modes in one device:

1. **Live**: placed on an audio track, the user can sing through it into headphones with barely noticeable latency, and the pitch is pulled toward the project scale.
2. **Graphical**: double-clicking an audio clip with a voice opens a note editor (pitch blobs), where notes can be moved in pitch, vibrato and drift can be straightened, and formants changed. Editing is non-destructive and undoable.

## Scope

In: a pitch detector in `core/`, low-latency live pitch shifting, the "Vocal" device in the track chain, the graphical mode for a single monophonic voice in an audio clip, formants, parameter automation.

Out: polyphony (chords), editing note timing inside a clip (that is warp from 07), harmonizer and double tracking (later, as a separate task), AI voice conversion (stage 14). We do not build a compressor, de-esser, echo or reverb into the device: the effects from 06 and the factory "Vocal …" chains from 08 cover that.

Builds on: 02 (scale `scaleRoot/scaleMode`, `Scales.h`, tuning `PROJECT.tuningA4`), 05 (audio tracks and clips, latency compensation), 06 (devices in the chain, PDC, automation), 07 (Rubber Band R3 and the audio editor).

## Tasks (outline)

### 12.1. Pitch detector: `core/include/daw/vocal/PitchTracker.h`

- YIN / pYIN without JUCE. A mandatory anti-aliasing filter before decimation (the Codex prototype lacks it: averaging 4 samples lets aliases through). The window is sized to the lowest note of the range (the "Voice" setting: Soprano, Alto, Tenor, Bass, Instrument), so as not to hold 64 ms for everyone.
- Output every ~2.7 ms: frequency, confidence, a "voiced" flag. Protection against octave errors: continuing the track is preferred over an octave jump if the confidence is comparable.
- Real time: fixed buffers, 0 allocations.

### 12.2. Note selection: scale, tuning, hold

- Frequency → note via the project's `tuningA4`, not a hard-coded 440 Hz.
- Allowed notes come from the project scale (`Scales.h`). In the device the user can choose "Same as Project" (default), another scale, or notes by hand (12 buttons), plus a MIDI input: held notes define the allowed ones (a MIDI target mode).
- **Hysteresis**: the current target changes only when the voice has moved more than 15–20 cents past the boundary between notes and stayed there for 20–40 ms. Without this the target jumps between adjacent notes (the main defect of the prototype).
- When confidence is lost (consonants, breaths), the last correction is **held** and fades out smoothly over 50–80 ms rather than dropping to zero at once.

### 12.3. Live pitch shifting: PSOLA

- Time-domain shifting, pitch-synchronous overlap-add using the period marks from 12.1. Target algorithmic latency ≤ 5 ms at 48 kHz, reported to the engine for PDC (05, 06). Spectral shifters (phase vocoder, signalsmith-stretch, Rubber Band) are not suitable for the live mode: latency of 40 ms and above and a smeared "phasey" voice.
- Parameters: Strength (0–100%), Speed (0–400 ms, 0 = the "robotic" effect), Naturalness (keeps vibrato and small drift: the correction is applied to a smoothed curve, and deviations around it are kept), Transpose ±12, Formants ±12 semitones, "Preserve formants" (on).
- Unvoiced fragments pass through unprocessed, with a crossfade at the transition.

### 12.4. The "Vocal" device in the track chain

- A built-in device `vocal` (like the effects in 06): parameters are automatable, colors only via `theme()`, strings in `Strings.h`.
- View: a large note scale with the current pitch and the target (an arrow/bar showing deviation in cents), a row of allowed notes, the knobs from 12.3, and an "Open Editor" button (12.5) for the selected clip.
- Monitoring goes through the track's regular monitoring from 05; the device has no audio route of its own (the prototype had a separate standalone monitor, and sound was muted by default).

### 12.5. Graphical mode

- Analysis of the audio clip on a background thread: a pitch curve + notes. A note is not split by pitch rounding: vibrato and scoops stay inside one note; a boundary is a change of stable pitch by more than 60 cents for 80+ ms, or a pause.
- Edit data is stored in the project model on the clip (`AUDIOCLIP` → `PITCHEDIT`: notes, target pitch, drift and vibrato straightening in %, formants), not in plugin state. Audio is not copied: a reference to the clip's file (the prototype kept up to 30 s of raw audio as base64 inside its state). Take the prototype's phrase model (an immutable source + a vector of notes, undo copies only the notes) as a reference.
- Rendering: Rubber Band R3 from 07 with formant preservation, on a background thread, with a disk cache next to the project; the audio thread reads the cache like a warp clip. Every edit is a project undo transaction.
- The editor opens in the detail panel in place of the audio editor from 07: note blobs over a keyboard with scale highlighting, dragging in pitch (with `Alt`, without snapping to the scale), the tools "Straighten" and "Snap All to Scale", and double-clicking a note snaps it to the nearest scale note.

## Tests (outline)

1. Detector: sines and saws at 80–1000 Hz, accuracy ±5 cents; a synthetic voice with 6 Hz vibrato at ±50 cents: no octave errors.
2. Tuning: with `tuningA4 = 432`, the note A4 is recognized at 432 Hz.
3. Hysteresis: a glissando between E4 and F4 that stops at the boundary gives no more than one target change.
4. Hold: a 30 ms confidence dropout in the middle of a note does not change the correction by more than 10%.
5. PSOLA: latency ≤ 5 ms at 48 kHz; a ±7 semitone shift on a synthetic voice gives the expected frequency ±5 cents; 0 allocations on the audio thread.
6. Graphical mode: a note edit + undo return the original render; a project with edits saves and reopens, and the sound matches.

## Done when (outline)

- [ ] The user sings through "Vocal" on an audio track on a Mac with an external audio interface, does not notice the latency, and the pitch is pulled to the project scale.
- [ ] In the graphical mode, an off-key note in a recorded clip can be fixed, undone, and the project saved.
- [ ] All tests green, CI green, README updated.
