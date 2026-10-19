# Effects backlog

Tasks for the effects area, in order. How to build one: [EFFECTS.md](EFFECTS.md). The 13 effects of stage 6 (EQ, compressor, gate, limiter, reverb, delay, drive, bitcrusher, filter, chorus, flanger, phaser, auto pan) already exist; this list adds to them. One task = one PR on `codex/fx-<id>`. Each "Check" line is a test in `core/tests/fx/`; the contract tests apply on top. Parameter lists are a starting point: add a parameter when the sound needs it, keep ids short and stable.

Mark a task done here in the same PR (`Done in #NN`).

## FX-1. Utility (`utility`, category `utility`)

Gain, stereo and phase tools for every track: Gain (-inf..+24 dB), Width (0–200 %, mid/side), Mono below (off, 20–500 Hz, Linkwitz-Riley 24 dB), Pan (-100..100, constant power), Invert L, Invert R, Swap, Channel (Stereo, Left, Right, Mid, Side), DC filter (on/off). No Mix (`withMix = false`).

Check: width 0 % gives L = R exactly; width 100 % and gain 0 dB pass the input bit-exact; mono below 120 Hz makes a 50 Hz side signal mono (side below -60 dB) and leaves a 1 kHz side signal within 0.1 dB; invert L gives -L; swap swaps; DC filter removes a 0.1 offset to below 1e-4 within 0.5 s.

## FX-2. Transient shaper (`transient`, category `dynamics`)

Attack (-100..+100 %), Sustain (-100..+100 %), Speed (fast..slow, the follower time constants), Output. Two envelope followers (fast and slow); their difference is the transient, the slow one the body. Level independent: the same settings do the same thing at -30 and at -6 dBFS.

Check: a drum-like burst (10 ms noise attack, 300 ms decay) at attack +100 % raises the first 10 ms by at least 6 dB and leaves the decay tail within 1 dB; sustain -100 % shortens the -20 dB decay time by at least half; the result at -30 dBFS input differs from the -6 dBFS one only by the 24 dB of level (within 0.5 dB); a steady sine is left within 0.5 dB.

## FX-3. De-esser (`deesser`, category `dynamics`, sidechain)

Frequency (2–12 kHz), Threshold, Range (0–24 dB), Mode (Split: only the band is reduced; Wide: the whole signal), Listen (hear the detected band), Lookahead (0–5 ms, reported as latency). Detector on a band-pass of the key (the input, or the sidechain).

Check: a 7 kHz burst over a 200 Hz tone is reduced by at least the range minus 1 dB in Split mode while the 200 Hz tone moves less than 0.3 dB; Wide reduces both; below the threshold nothing changes (null below -100 dB against the delayed input); Listen outputs the band only.

## FX-4. Multiband dynamics (`multiband`, category `dynamics`)

Three bands with two crossovers (Linkwitz-Riley 24 dB, 40 Hz–16 kHz), per band: Threshold above and Ratio above (compression), Threshold below and Ratio below (expansion or upward compression), Gain, Solo, Bypass; global Attack, Release, Output. Readouts: gain reduction per band.

Check: with all bands neutral the sum of the crossovers is flat within 0.1 dB from 20 Hz to 20 kHz (allpass phase is fine); a -10 dBFS sine in the low band with threshold -20, ratio 4 settles at -17.5 ±0.3 dBFS and a sine in the high band is untouched; solo outputs one band; upward compression raises a -40 dBFS sine at threshold below -30, ratio 2 by about 5 dB.

## FX-5. Frequency shifter and ring modulator (`freqshift`, category `modulation`)

Mode (Shift, Ring), Shift (-2000..+2000 Hz, fine around 0), Ring frequency (1 Hz–5 kHz, log), Spread (the right channel shifts the other way), Feedback, LFO on the shift (rate, depth, sync). Shift through a Hilbert transformer (allpass pairs, no FFT).

Check: shifting a 1 kHz sine by +100 Hz gives 1100 Hz with the image at 900 Hz below -40 dB; ring mode at 300 Hz turns 1 kHz into 700 and 1300 Hz of equal level within 0.5 dB, with 1 kHz below -50 dB; shift 0 is transparent within 0.1 dB above 50 Hz.

## FX-6. Grain delay (`grain`, category `space`)

Time (1 ms–2 s, or synced divisions), Grain size (10–200 ms), Density, Pitch (-24..+24 semitones), Pitch spread, Time spread, Feedback, Freeze, Mix. Overlapping Hann grains read from a delay buffer; random spread from a fixed-seed generator (deterministic per `reset()`).

Check: pitch +12 with zero spreads turns a 440 Hz sine into 880 Hz (strongest component, within 1 %); pitch 0, spreads 0 and size 50 ms passes a sine through the delay at the set time within 1 dB; output after `reset()` is identical between two runs (same seed); freeze holds the buffer: the output stays non-silent for 2 s after the input stops.

## FX-7. Vocoder (`vocoder`, category `filter`, sidechain)

The input is the carrier, the sidechain is the modulator (a voice); without a sidechain, an internal carrier (saw or noise, pitch follows nothing, a fixed note) and the input becomes the modulator. Bands (8–40), Low and High (band range), Formant shift (±12 semitones), Attack, Release, Unvoiced (noise for sibilants), Mix.

Check: a modulator with energy only at 1 kHz lets the carrier through mostly at 1 kHz (the 1 kHz band at least 20 dB above bands two octaves away); silence on the modulator gives silence out (below -90 dB after the release); formant shift +12 moves that band to 2 kHz.

## FX-8. Resonators (`resonator`, category `filter`)

Five tuned resonators: Note (MIDI note of the first), Chord (Unison, Fifths, Octaves, Major, Minor, Sus4, Custom with intervals per voice), Decay (10 ms–10 s), Brightness, Width (spread across the stereo field), Mix. Comb or modal filters; tuning against A4 = 440 by default and a Tuning parameter (415–466 Hz) so the project tuning can drive it later.

Check: an impulse into note 57 (A3) rings at 220 Hz ±0.5 Hz with A4 = 440, and at 216 Hz with A4 = 432; Major gives peaks at the root, major third and fifth (each within 1 % of the frequency); decay 1 s reaches -60 dB at 1 s ±10 %.

## FX-9. Effects as AU/VST3 plugins (`plugins/fx/`)

One generic JUCE wrapper (`plugins/fx/`) that builds a plugin per entry of `effectTypes()`: parameters straight from `params()`, state by parameter id, the generic editor (no custom UI), latency and tail reported to the host, a sidechain bus when `acceptsSidechain()`. Plugin names "DAW <Name>"; manufacturer code `Dawp` as in Prisma; a unique 4-letter code per effect, written in the registry so it never changes. CI validates each AU with `auval` (add the steps to `.github/workflows/build.yml`, a shared file: keep the diff small).

Check (in `tests/` or a small test target of `plugins/fx/`): each wrapper reports the effect's parameter count, round-trips its state, and processes a block without NaN; `auval` passes on macOS CI.

## Later, with the owner first

- Convolution reverb (needs loading impulse files off the audio thread and a file picker in the UI area).
- A spectral freeze or blur (FFT-based; latency and CPU need numbers before starting).
- Tape: wow, flutter, hiss and head bump as one effect (overlaps Drive's Tape mode: ask before starting).
