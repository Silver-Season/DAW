# 04. Export to WAV, AIFF, FLAC, MP3

## Goal

Export a track to a file that can be sent, uploaded to streaming services or handed off for mixing. Export runs faster than real time, produces sample-for-sample the same sound as playback, and supports stems and loudness normalization.

At the end the user can: press `Cmd+Shift+R`, choose MP3 320 or WAV 24-bit, normalization to −14 LUFS, get a file that sounds exactly as in the DAW, and stems of each track separately. And also preview any old version of the project without closing the current one.

## Scope

Included: offline render, the export window, WAV / AIFF / FLAC / MP3 formats, bit depth and sample rate with resampling, dithering, reverb tail, stems, peak and LUFS normalization, a loudness report, exporting a MIDI clip to `.mid`, version preview.

Not included: video export, uploading to services, Ogg/Opus (later, if requested), real-time export through external hardware.

---

## Task 4.1. Offline render

- `engine/OfflineRenderer`: creates a **separate** engine on top of a copy of the model (without touching the one that is playing), with its own plugin instances: the state of each device is copied via `getStateInformation` / `setStateInformation`. The user can keep working during the export.
- Plugins get `setNonRealtime (true)`, a block size of 1024, and the project sample rate. `AudioPlayHead` reports `isPlaying = true`, position and tempo, as during playback.
- Rendering runs on a background thread (`juce::Thread`), progress is an atomic, and the UI shows a window with a progress bar, remaining time and a "Cancel" button. Cancel deletes the unfinished file.
- Range: "Whole song" (from 1.1.1 to the end of the last clip), "Loop" or "Selection" in the Arrangement. If the Arrangement is empty, the window says "The arrangement is empty. Record a performance from the Session view into the Arrangement (stage 3) and export again" and the button is disabled.
- **Tail**: after the end of the range, rendering continues until the output peak stays below −90 dBFS for 1 second in a row, but no longer than 10 seconds. An "Add tail" toggle (on by default).
- **Match with playback**: at the same sample rate and with deterministic devices (Prisma), the offline render matches the render through `FakeDevice` in real-time mode sample for sample. This is a test.
- Plugin latency compensation (06) is taken into account: the first `latency` samples of output are discarded so the file starts exactly at 1.1.1.
- The metronome and the browser preview are not included in the export.

## Task 4.2. Export window

Opens with `Cmd+Shift+R` (the command from 01 becomes active). Fields and defaults:

| Field | Options | Default |
|---|---|---|
| What | Master · All tracks separately (stems) · Selected tracks | Master |
| Range | Whole song · Loop · Selection | Whole song |
| Format | WAV · AIFF · FLAC · MP3 | WAV |
| Sample rate | 44.1 · 48 · 88.2 · 96 kHz | project sample rate |
| Bit depth (WAV/AIFF) | 16 · 24 · 32 float | 24 |
| Bit depth (FLAC) | 16 · 24 | 24 |
| MP3 | CBR 128 / 192 / 256 / 320 kbps, VBR V0 / V2 | CBR 320 |
| Dither | None · TPDF · TPDF with noise shaping | TPDF at 16 bit, otherwise none |
| Normalization | None · Peak (−0.1 / −1 dBFS) · Loudness LUFS | None |
| LUFS target | −14 (Spotify, YouTube) · −16 (Apple Music) · −9 (club) · custom | −14 |
| Tail | on / off | on |
| Destination | folder, default `<project>/Exports/` | |
| Name | template `{project} {date}` / for stems `{project} - {track}` | |

- The last choice is remembered in the project (`PROJECT/EXPORT` node).
- Below the settings, a line: "≈ 3:42, 37 MB".
- After the export, the window shows a summary: duration, peak (dBTP), integrated loudness (LUFS), loudness range (LRA), and the buttons "Show in Finder" and "Preview" (plays the file through the browser preview).
- If, without normalization, the peak is above 0 dBFS, the summary warns: "Peak +1.8 dBFS: the file will clip. Turn on normalization or put a limiter on the master".

## Task 4.3. Formats

- WAV, AIFF, FLAC via JUCE (`WavAudioFormat`, `AiffAudioFormat`, `FlacAudioFormat`). WAV files get BWF metadata: project name, date, tempo in the `bext`/`acid` chunk, so that other DAWs pick up the tempo.
- **MP3** uses the **LAME 3.100** library (LGPL 2, compatible with AGPLv3 under static linking with the app's open source code).
  - The `lame-3.100.tar.gz` sources from the official site (SourceForge), placed in `third_party/lame/` with a `SOURCE.md` file (URL, version, SHA-256 of the archive, license). Verify the SHA-256 against the official source, not from memory.
  - Write `third_party/lame/CMakeLists.txt` that builds only `libmp3lame` (files from `libmp3lame/` and `libmp3lame/vector/` without the frontend and without `mpglib`), with a minimal `config.h` (`HAVE_STDINT_H`, `STDC_HEADERS`, `HAVE_ERRNO_H`, `HAVE_FCNTL_H`, `HAVE_LIMITS_H`, `TAKEHIRO_IEEE754_HACK` off for portability, `HAVE_XMMINTRIN_H` only on x86_64).
  - A wrapper `app/Source/engine/Mp3Writer` with an API like `AudioFormatWriter`: `lame_init`, `lame_set_in_samplerate`, `lame_set_num_channels 2`, `lame_set_brate`/`lame_set_VBR`, `lame_set_quality 2`, `lame_init_params`, `lame_encode_buffer_ieee_float`, `lame_encode_flush`, then `lame_mp3_tags_fid` for the Xing/LAME header (exact length and gapless). ID3v2: title, artist (from the project settings), tempo.
  - MP3 sample rates: 44.1 and 48 kHz (higher rates are resampled to 48).
- **Resampling** when the export sample rate ≠ the project sample rate: rendering always runs at the project rate, followed by a high-quality resampler. In `core/include/daw/dsp/Resampler.h`: a polyphase windowed-sinc (Kaiser window, β = 9, 64 taps per phase, 256 phases), alias rejection ≥ 100 dB. Test: a 1 kHz sine 48 → 44.1 kHz, distortion < −100 dB.
- **Dithering** when reducing bit depth: TPDF (sum of two uniform noises ±1 LSB), and a noise-shaped variant (a 9th-order error filter per Lipshitz or a simple 5th-order E-weighted one). The noise generator uses a fixed seed so the export is reproducible.

## Task 4.4. Normalization and loudness measurement

- `core/include/daw/dsp/Loudness.h`: a meter per **ITU-R BS.1770-4 / EBU R128**:
  - K-weighting: two biquads (high shelf +4 dB at ~1681 Hz and high-pass at ~38 Hz) with coefficients recalculated for any sample rate (formulas from BS.1770, not a 48 kHz table).
  - 400 ms blocks with 75% overlap, absolute gate −70 LUFS, relative gate −10 LU.
  - LRA: 3 s blocks, 2/3 overlap, −20 LU gate, 10th and 95th percentiles.
  - True peak: 4x oversampling (polyphase FIR from the resampler) and the maximum absolute value.
- LUFS normalization requires two passes: the first render goes to a temporary 32-bit float file with measurement, then gain is applied and the final format is written. If after the gain the true peak is above −1 dBTP, only the gain that gives −1 dBTP is applied, and the summary reports: "Reached −15.2 LUFS instead of −14: limited by peak. Put a limiter on the master".
- Peak normalization also takes two passes (or one with a temporary file).

## Task 4.5. Stems

- "All tracks separately": each track is rendered to its own file. The track's sound is after its fader and pan, without the master chain. Return tracks (06) as separate files. Plus a master file if "Include master" is checked.
- For speed, stems are rendered in one pass: the offline engine writes each track's output to its own file in parallel with the master.
- Muted tracks are skipped by default ("Include muted" checkbox).
- Normalization for stems: one common gain for all (so the balance is kept), computed from the master.

## Task 4.6. MIDI export

- Right-click on a MIDI clip (session or timeline) → "Export MIDI Clip…" → `.mid` (type 0, 960 PPQ, project tempo and time signature, notes, controllers).

## Task 4.7. Preview a version

- In the version list (01, 1.7), a "Preview" button for each version. The version is rendered by the offline engine into a temporary file (cached in `History/previews/vNNNN.flac`, deleted when versions are thinned out), with an indicator while rendering. Then the file plays through the preview player from the current project's cursor position, in sync with it: an "A / B" button switches on the fly between the current project and the version at the same position.
- While a version is being previewed, the current project's transport plays but its output is muted, so the positions match.

## Tests

1. **Match.** A demo project on Prisma: an offline render of 8 bars equals the render through `FakeDevice` sample for sample (maximum difference 0).
2. **Tail.** A note with a 2 s release at the end of the song: the file is longer than the range by ≥ 2 s and ≤ 10 s, the last 1 s is quieter than −90 dBFS.
3. **Formats.** WAV 16/24/32f, AIFF, FLAC are read back by JUCE with the same length and (for 32f) the same samples. MP3 decodes back (`juce::MP3AudioFormat` with `JUCE_USE_MP3AUDIOFORMAT=1` in the test build, on macOS also `CoreAudioFormat`), length matches ± 1 frame thanks to the LAME tag.
4. **Resampler** (core): a 1 kHz sine 48 → 44.1 and back, THD+N < −100 dB; a 21 kHz sine 96 → 44.1 is suppressed by ≥ 100 dB.
5. **LUFS** (core): EBU Tech 3341 reference signals (a 1 kHz sine at −23 dBFS stereo gives −23.0 ± 0.1 LUFS; generate the signals in the test), the true peak of a sine at a quarter of the sample rate with a 45° phase gives +3 dB over the sample peak ± 0.1.
6. **Normalization.** After normalizing to −14 LUFS, measuring the file gives −14 ± 0.2, true peak ≤ −1 dBTP; when limited by peak, the message contains the actual value.
7. **Stems.** The sum of the stems (without the master chain) equals the master with the master chain disabled ± 1e-6.
8. **Dithering.** A 16-bit export of silence with TPDF gives noise of ~ −96 dBFS RMS; without dithering, silence stays zero.
9. **Cancel.** Cancelling halfway deletes the file.

## Done when

- [ ] `Cmd+Shift+R` exports the master to WAV, AIFF, FLAC and MP3, and the files open in Music / QuickTime on a Mac.
- [ ] The file's sound matches playback (test 1), the reverb tail is not cut off.
- [ ] Normalization to −14 LUFS gives −14 ± 0.2, the summary shows peak, LUFS, LRA.
- [ ] Stems of all tracks are exported in one pass.
- [ ] Export is at least 5 times faster than real time on the demo project (figure in the report).
- [ ] A MIDI clip exports to `.mid` and opens in another program.
- [ ] Any version from the history can be previewed and compared "A / B" with the current one.
- [ ] LAME lives in `third_party/lame` with `SOURCE.md` and the license, the README lists third-party components.
- [ ] All tests green, CI green, README updated.
