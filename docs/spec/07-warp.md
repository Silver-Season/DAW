# 07. Warp, audio editor, tempo track

## Goal

Any loop and any recording adapts to the project tempo without changing pitch. Tempo and time signature can change over the course of the track. The audio editor shows the waveform and warp markers and lets the user fix the timing of a recording by hand.

At the end the user can: drop a 92 BPM loop into a 124 BPM project and hear it in tempo, fix a warp marker on a beat that drifted, slow the project down in the bridge and speed it up in the drop, and all clips follow the tempo.

## Scope

In: a time-stretching library, warp modes, tempo and onset detection, warp markers, the audio editor, a tempo and time signature track, tempo-dependent time conversion throughout the engine, "Match Project Tempo to Clip", slicing audio into MIDI by transients (Slice to MIDI arrives together with the drum machine in 08; only the algorithm is done here).

Out: editing the pitch of individual notes inside audio (stage 12), polyphonic MIDI extraction from audio.

## Design direction (2026-10-18)

A review of how warp and tempo detection usually work found the same pain points everywhere: an automatic warp that places markers off the beat and has to be redone by hand, a tempo read at half or double speed, the first downbeat in the wrong place, settings for "keep the project tempo or adapt it" buried in project settings, and markers that the user has to create before anything can be dragged. The design below aims at fixing those, in this order of importance.

1. **The first sound sets the tempo.** A sound dropped into a project with no other clips gives the project its tempo, and its first downbeat lands on bar 1. Every later sound follows the project tempo. Nothing has to be configured (`ProjectModel::adoptTempoOfFirstSound`).
2. **One decision strip, not a settings page.** Right after the analysis, the clip editor shows what was found in one line: the tempo with its confidence, and the choices that fix the common errors at once: x2, /2, "Bar 1 here", "Project follows this clip" (Match Project Tempo), "Clip follows project".
3. **Direct manipulation of the beat grid.** The detected beats are drawn on the waveform as the grid of the sound itself (bars numbered). Any beat line can be grabbed and dragged; it becomes a warp marker at that moment. There is no separate step to create markers first. Double-click still adds one anywhere, and Delete removes it.
4. **Show where the analysis is unsure.** Stretches where the beats drift away from a steady tempo are tinted, so the user knows where to look instead of checking the whole file.
5. **Check by ear in place.** A click of the sound's own grid can play along in the editor, to hear at once whether the grid is right.
6. **Played without a click: the project follows the performance** (with the tempo track, task 7.5): the recording is not stretched; the project gets a tempo map from its beats, and everything else follows it.
7. **Audio quantize with strength**, and several tracks locked together (drums recorded with several microphones move as one, so their phase stays intact).

Items 1–5 and the warp markers (7.3) are the first part; the tempo track (7.5), item 6 and rendering ahead of the cursor (7.1) the second; item 7 the third.

---

## Task 7.1. Time stretching

- The **Rubber Band Library v3** (GPL v2 or later, compatible with AGPLv3). Sources in `third_party/rubberband/` with `SOURCE.md` (URL github.com/breakfastquay/rubberband, release tag, commit SHA, license). Built statically via its single-file build (`single/RubberBandSingle.cpp`), FFT either built-in (`USE_BUILTIN_FFT`) or vDSP on macOS (`HAVE_VDSP`), built-in resampler.
- The **R3** engine (`OptionEngineFiner`) is used for the "Complex Pro" mode, and **R2** (`OptionEngineFaster`) for "Complex" and as a fallback under high load. Real-time mode (`OptionProcessRealTime`).
- An in-house simple **Beats** mode (for drums): slicing by onsets into segments, each segment plays unstretched at its original pitch. When slowing down, the gaps are filled with the segment's tail with a "transients" envelope (the "Decay" setting, 0–100%); when speeding up, the segment is truncated with a 3 ms fade. Works without Rubber Band, cheap on CPU.
- **Re-Pitch** mode: plain resampling (pitch changes with tempo), from 05.
- **Tones** mode is R3 with `OptionFormantPreserved` off and `OptionPitchHighConsistency`; **Texture** is R2 with `OptionTransientsSmooth`. Clip modes in total: Beats, Tones, Texture, Re-Pitch, Complex, Complex Pro (default: Beats for files shorter than 8 bars with percussive onsets, Complex Pro for everything else; classification by onset density).
- The audio thread cannot wait for Rubber Band: processing runs ahead on a **background render thread** (one per clip, a pool of N threads = number of cores − 2) that fills a ring buffer 1 second ahead of the playhead. The audio thread reads from the ring. On a jump: the first 50 ms are filled with resampling (Re-Pitch) with a crossfade to the warped signal once the renderer catches up. This is the standard approach; without it, real-time stretching produces dropouts.
- Quality is verified numerically (see tests), not by ear.

## Task 7.2. Onsets and tempo

`core/include/daw/dsp/Onsets.h`:
- **Onset detector**: spectral flux (STFT 1024, hop 256, Hann window, log magnitude, positive per-bin difference, sum), adaptive threshold (median of 11 frames × 1.5 + offset), minimum 50 ms between onsets, position refined by the energy maximum in the time domain within ± 5 ms. Sensitivity 0–100% is a threshold multiplier.
- **Tempo estimation**: autocorrelation of the onset function over 4–16 seconds, candidates 60–200 BPM, preference for 90–150 (a weighting window), octave errors resolved by comparing the peak strengths at `T`, `T/2`, `2T`. Plus hints: the `acid` chunk, the file name (`_124bpm`, `124 BPM`, `124-`), and a file length that is a whole number of bars at a round tempo (if the length is exactly 4.000 bars at 92 BPM, that is a strong candidate).
- **Beat grid**: dynamic programming after Ellis (beat tracking): choose the sequence of beats that maximizes onset strength with a penalty for deviating from the tempo.
- Analysis runs in the background on file import and is cached in `Samples/.analysis/<hash>.xml` (onsets, tempo, beats, mode).

## Task 7.3. Warp markers and the model

```
AUDIOCLIP  warp=1  warpMode="beats|tones|texture|repitch|complex|complexpro"
           beatsDecay=1.0  preserveFormants=0  originalTempo=92.0
└── WARP   └── MARKER  sample=<position in the file>  beat=<position in clip beats>
```

- Between adjacent markers, file time maps to beats linearly. A new clip gets two markers (start and end) from the detected tempo, plus "auto markers" on every beat of the grid (shown faintly, not stored until touched).
- Moving a marker: drag the marker horizontally in the editor (`beat` changes, the segments to its left and right stretch). Ctrl+drag moves the waveform under the marker (`sample` changes). Double-click an auto marker or the waveform to create a marker. `Delete` removes the selected one.
- Right-click on a marker: "Tempo From Here = …" (enter a BPM for the section up to the next marker), "Align Segment to Grid", "Add Markers at All Onsets in Selection".

## Task 7.4. Audio editor

The detail panel for an audio clip (like the piano roll for MIDI, `Shift+Tab`):

```
┌ Clip ───────────┬ ruler (bars, loop, start) ──────────────────────────────────────┐
│ name, color,file│ warp markers (yellow triangles) ────────────────────────────────│
│ gain, transpose │ waveform (stereo in halves, onsets as thin lines)               │
│ position, detune│                                                                 │
│ Warp on, mode   │                                                                 │
│ orig. tempo 92 ×2 ÷2                                                              │
│ Reverse         │                                                                 │
│ Decay (Beats)   │                                                                 │
└─────────────────┴─────────────────────────────────────────────────────────────────┘
```

- The "×2" and "÷2" buttons double and halve the original tempo (a common detector error).
- "Match Project Tempo" (right-click): project tempo = the clip's original tempo.
- Zoom and scroll as in the piano roll, `Cmd+wheel`, pinch, `Z`.
- At high zoom the waveform is drawn per sample (segments and dots); zoomed out, from the `AudioThumbnail` levels.
- The clip's playback cursor moves across the waveform.

## Task 7.5. Tempo and time signature track

### Model

```
PROJECT/TEMPOMAP
├── TEMPO  beat  bpm  curve=0  (0 is a step, 1 is linear up to the next point)
└── SIG    bar   num  den
```

`PROJECT tempo` from 01 stays as the "default" tempo and the first point of the map.

### Time in the engine

- `core/include/daw/engine/TempoMap.h`: conversion `beat ↔ seconds ↔ samples` over a piecewise map. On a linear section the tempo changes linearly with beats, and time = the integral of `60 / bpm(beat)` over beats (analytically `60 * ln(bpm1/bpm0) / k` for a linear function, taking the limit when `bpm0 = bpm1`). Cache of accumulated time at the start of each section, binary search.
- `Transport` moves to `TempoMap`: the block window `[startBeat, endBeat)` is computed from the map, and inside a block with a linear tempo, clip rendering uses the exact position of each event (`samplesAt (beat)`) rather than a constant "samples per beat".
- `AudioPlayHead` reports the current tempo at the start of the block and `ppqPosition` to plugins.
- The metronome, launch quantize, loop, markers and recordings are all in beats, so they keep working without changes. Changes are needed only where the code had "samplesPerBeat = const".
- Warp clips get their stretch ratio from the map in every block (Rubber Band `setTimeRatio` smoothly; in Beats, the segment lengths).
- Time signature: bars and the ruler in Arrangement account for signature changes (the functions `barToBeat`, `beatToBarBeat` in `TempoMap`).

### UI

- In Arrangement, a "Tempo" lane above the tracks (shown with a button in the header or via View → "Tempo Track"). Editing is the same as automation (06): points, steps or lines, vertical range 60–200 BPM (configurable).
- The "Time Signature" lane: a click places a signature change at the start of a bar, a double-click opens input such as "7/8".
- The tempo field in the transport shows the current tempo from the map, in italics if the project has tempo changes. In that case, editing the field scales the whole map proportionally (with a confirmation in the hint bar).
- **Tap tempo**: a TAP button next to the tempo field: 4+ taps set the tempo from the average interval of the last 4.
- A scene with a set tempo (`SCENE tempo` from 01) changes the project tempo when launched.

## Task 7.6. Slicing by transients (algorithm)

- `core`: `sliceByOnsets (onsets, mode, minSlice)` and `sliceByGrid (beats, division)` return a list of segments. Used in 08 (slicing to drum machine pads) and in the audio editor for "Slice at Onsets" (right-click: the clip is cut into clips at onsets or on the grid).

## Tests

1. **Onsets** (core): a synthetic signal of 16 clicks with −40 dB noise: 16 onsets found, position error ≤ 2 ms.
2. **Tempo** (core): synthetic drum patterns at 92, 124, 140, 174 BPM give the tempo ± 0.5 BPM; a 70 BPM pattern with 16th-note accents is not detected as 140 (octave error test). If this does not work out, the test records the behavior as a known limitation and it goes into the report.
3. **Hints:** the name "loop_92bpm.wav" and a length of exactly 4 bars at 92 win over an uncertain detector.
4. **Stretching, length:** a 92 BPM 4-bar clip in a 124 BPM project occupies 4 bars ± 1 ms in the output.
5. **Stretching, pitch:** a 440 Hz sine stretched ×1.35 in Tones, Complex and Complex Pro modes stays at 440 ± 1 Hz (FFT with peak interpolation); in Re-Pitch it gives 440 × 124 / 92 ± 1 Hz.
6. **Beats:** onsets in the result land on project beats ± 1 ms, transients are not smeared (attack peak amplitude ≥ 90% of the original).
7. **Jumps:** after a jump, sound appears within ≤ 1 block, with no click at the transition from Re-Pitch to warp (the difference between adjacent samples in the transition window is within what is normal for the signal).
8. **TempoMap** (core): a linear ramp 100 → 150 BPM over 8 bars: the end time of the section matches numerical integration to 1e-9 s; `beat → sample → beat` returns the original ± 1e-9.
9. **Rendering with the map:** clip notes on beats during a linear ramp land on the samples computed by `TempoMap`, ± 1 sample.
10. **Real time:** 8 Complex Pro warp clips play for 1 minute without dropouts (the ring never ran empty) and without memory allocations on the audio thread.

## Done when

- [ ] A loop at a different tempo from the browser plays at the project tempo; the mode is chosen automatically and can be changed by hand.
- [ ] Warp markers can be placed and moved, auto markers on beats are visible (audio editor screenshots in 4 themes).
- [ ] A tempo track with a smooth ramp: MIDI and audio clips and the metronome follow it.
- [ ] Time signature changes are shown on the ruler and affect bars.
- [ ] Tap tempo and "Match Project Tempo" work.
- [ ] CPU usage with 8 Complex Pro warp clips is in the report (a number from an M1 or an estimate in the container), without dropouts.
- [ ] Rubber Band is in `third_party/rubberband` with `SOURCE.md` and the license.
- [ ] All tests green, CI green, README updated.
