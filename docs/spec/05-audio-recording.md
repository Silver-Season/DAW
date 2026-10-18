# 05. Audio tracks, audio recording, takes, always-on audio capture

## Goal

Record vocals, guitar, microphones and any audio interface inputs exactly on the beat, stack takes and assemble the best one from them. Drop samples and loops from the browser onto tracks. Always-on capture now keeps audio in memory too: a good guitar riff played without recording can be pulled out with one button.

At the end the user can: create an audio track with `Cmd+Shift+T`, choose the "Guitar" input, turn on monitoring, record three takes over a loop, assemble the best one from pieces, drag a loop from the browser and hear the recording exactly in time.

## Scope

Included: the audio clip model, the audio file pool, playback of audio clips in the session and the arrangement without warping, fades, gain and transposition (via resampling, pitch changes together with tempo, like tape speed), track inputs and monitoring, recording to disk, recording latency compensation, the loopback test, takes and comping, file import, sample preview, always-on audio capture, the MIDI / audio record source switch.

Not included: warping and tempo matching without pitch change (07), effects (06), the sampler and the 16-pad drum machine (08).

---

## Task 5.1. Model and file pool

```
TRACK type="audio"  input="audio:0-1" (interface channels, "0" mono, "0-1" stereo) monitor="auto"
└── SLOTS/SLOT/AUDIOCLIP …   and   ARRANGEMENT/ACLIP/AUDIOCLIP …
AUDIOCLIP  id  name  colour  file=<id in the pool>
           sampleStart=0  sampleEnd=<length>   (in file samples)
           loopStart  loopEnd  loop=0
           gainDb=0  transpose=0 (semitones, −48…48)  detune=0 (cents)  reverse=0
           fadeIn=0  fadeOut=0  (beats; in the arrangement, 4 ms auto-fades by default)
           warp=0  (field reserved for 07; always 0 in this stage)
PROJECT/AUDIOFILES/FILE  id  path (relative to the project folder, if inside it)
                         absPath  sampleRate  channels  lengthSamples  hash (xxHash64 of the first 1 MB + length)
```

- A file dropped from outside the project is copied into `Samples/Imported/` (setting "Copy samples into project", on by default). Identical files (same hash) are not duplicated.
- A missing file on open: the clip is gray, "File not found". The menu item "Find Missing Files…" searches by name and hash in a chosen folder.
- `engine/AudioFilePool`: open `AudioFormatReader`s by `FILE id`, files up to 30 seconds fully in memory, longer ones streamed with read-ahead (`BufferingAudioReader` on a shared `TimeSliceThread`, 3-second buffer). The audio thread never waits for the disk: if data is not available, it outputs silence and increments a "disk dropouts" counter, and the UI shows a disk icon in the transport.
- Waveform overview: `juce::AudioThumbnail` + `AudioThumbnailCache` (disk cache in `Samples/.thumbs/`).

## Task 5.2. Audio clip playback

- `core`: `AudioClipPlayback`: reading from a source at a position, looping with a 2 ms crossfade at the seam, gain, reverse, fades (equal-power curve), transposition through the resampler (the same windowed-sinc from 04, in a real-time mode with fewer taps: 16 per phase).
- In the session an audio clip launches and loops like a MIDI clip (quantized launch, `ClipPlayer` is generalized to both types). Loop length in beats = length in samples ÷ samples per beat at the file's **original** tempo, if known (`acid` chunk or a file name like `*_124bpm*`), otherwise at the project tempo at the time of insertion. Without warping, the clip is not stretched when the project tempo changes; this is fine for this stage and is noted in the clip editor: "Tempo matching arrives in stage 7".
- In the arrangement, an ACLIP with an AUDIOCLIP plays from its position, taking into account `sampleStart` and trimming. 4 ms auto-fades at the edges to avoid clicks.

## Task 5.3. Inputs and monitoring

- Audio track header: an input menu "Input 1 · Mic", "Inputs 3/4 · Guitar (stereo)", "Resampling" (master output), "None". Channel names come from settings (01, 1.9).
- Monitoring: **Auto** (the input is audible when the track is armed and no clip is playing), **In** (the input is always audible), **Off**. Monitoring goes through the track chain with the lowest possible latency (the current buffer).
- The input level meter on the header and in the mixer is visible as soon as the track is armed, before recording. A peak above −0.5 dBFS lights the meter red until it is clicked.
- The **record source** switch on a MIDI track (from 02) now works: "Audio" records the output of this track's instrument into an audio clip (handy for "freezing" a synth part). On an audio track the source is always audio.

## Task 5.4. Recording to disk

- Activated the same way as MIDI (02, 03): slot, transport ●, F9, punch, count-in, metronome.
- The audio thread copies the track's input samples into an SPSC ring (`AbstractFifo`, capacity 2 seconds per channel, preallocated). A background thread writes to disk via `AudioFormatWriter::ThreadedWriter` into `Samples/Recorded/<track> <date time>.wav`, 24-bit WAV (setting: 16 / 24 / 32 float), at the project sample rate.
- If the ring overflows (the disk cannot keep up), recording continues, the lost part is filled with silence, and after recording a warning is shown with the time of the dropout.
- During recording, the clip in the slot or on the timeline draws its waveform as data arrives (from level data from the audio thread, without reading the file).
- Stopping recording closes the file and creates the FILE in the pool and the clip in one transaction. `Cmd+Z` removes the clip; the file stays on disk until "Delete Unused Recordings…" in the File menu.

### Recording latency compensation

- Recorded audio is shifted earlier by `inputLatency + outputLatency` samples (driver data), plus a manual adjustment from the "Recording" settings (±100 ms, 0.1 ms step). The shift is applied when the clip is created (`sampleStart`); the file is written in full without trimming.
- **Loopback test** (settings → Recording → "Measure latency"): the user connects an interface output to an input with a cable. The DAW plays 10 clicks (a 1-sample impulse, then 100 ms of silence), records the input, finds each click by the maximum of the cross-correlation and computes the median latency. The difference from the driver data is saved as the manual adjustment. The algorithm is in `core` (`measureRoundTrip`), with a test on a synthetic signal.

## Task 5.5. Takes and comping

- Loop recording in the arrangement creates **take lanes** under the track: each pass gets its own lane, the latest take is active in the main track.
- Model: `ACLIP/TAKES/TAKE id index` and `ACLIP/COMP/SEGMENT start end takeIndex`. The main track plays the comp segments.
- UI: a ▸ button on the track header expands the lanes. Dragging with the mouse across a take lane selects that piece into the comp (a segment); the boundary between segments gets a 10 ms crossfade (adjustable by dragging). Alt-clicking a lane previews that take solo. `Cmd+Shift+D` is "Next Take" for the selected piece.
- Works for the MIDI takes from 03 too (replacing the "Takes ▸" menu).
- "Flatten Comp" (right-click) creates a new file from the segments and replaces the clip with it; the takes stay in the lanes.

## Task 5.6. Samples from the browser and Finder

- The "Sounds" category (01) now drops files: onto an audio track, a clip in a slot or on the timeline; onto a MIDI track, not allowed (no highlight); onto empty space, a new audio track with the clip.
- Preview: selecting a file in the browser plays it from the start (headphones on). If the file's tempo is known and "Sync" is on, the preview starts at the nearest project beat (without stretching until 07).
- Import from Finder: WAV, AIFF, FLAC, MP3, M4A/AAC (on macOS via `CoreAudioFormat`), OGG. Several files dropped at once are laid out across tracks in sequence (in the Arrangement, one below another; in the Session, into slots of one track across scenes; the Cmd modifier switches to "one after another").

## Task 5.7. Always-on audio capture

- For every input channel used by an armed audio track or a track with In monitoring, the engine keeps a float ring buffer. Length is a setting: 1 / 2 / 5 / 10 minutes, default 5. Memory use is shown in settings ("≈ 115 MB per stereo input at 48 kHz, 5 minutes"). Buffers are allocated on the message thread when the set of inputs changes, never in the audio thread.
- The "10 min" strip in the transport shows audio too: a level envelope as a translucent fill in the track color under the MIDI ticks.
- "Capture" on a selected audio track: the phrase is detected by level (a region where the 50 ms RMS is above −50 dBFS; pauses shorter than 4 s do not break the phrase), with a 200 ms margin before the start. If the transport was playing, start and length are aligned to bars as in MIDI capture (02, 2.5), with the position taken from the transport marker recorded together with the audio. If it was stopped, the clip is not tied to bars and starts at the first attack. The audio is written to `Samples/Recorded/Capture <date>.wav` in the background.
- If capture was on a MIDI track, the MIDI capture from 02 is used.

## Tests

1. **Pool.** Importing two identical files creates one FILE; a missing file is found by hash in another folder.
2. **Playback.** A 1-bar clip of a 1 kHz sine in the session plays continuously for 8 passes, the loop seam has no click (the maximum difference between adjacent samples at the seam is no more than inside the sine × 1.5).
3. **Streaming read.** A 10-minute file: with artificially slow reading, the audio thread does not block (block time < 1 ms), dropouts are counted.
4. **Recording.** Through `FakeDevice`, feed impulses to the input at beat positions, record 2 bars: in the clip, impulses sit on the beats within ± 1 sample after compensating the given latency.
5. **Overflow.** With an artificially stalled writer: recording does not crash, the gap is filled with silence, the file length is correct.
6. **Loopback test** (core): a synthetic latency of 371 samples with −60 dB noise is measured as 371 ± 1.
7. **Comping.** Three takes and two segments give the expected sound at the boundary (10 ms crossfade, no discontinuity).
8. **Audio capture.** A 5-minute ring with a phrase of 3 attacks after silence: capture finds the phrase start −200 ms ± 5 ms, and the length in bars with the transport playing.
9. **Real time.** Recording 4 stereo tracks for 1 minute with `DAW_RT_CHECKS=ON`: zero memory allocations in the audio thread.

## Done when

- [ ] An audio track records the interface input in time (test 4 and a check on a Mac with the built-in microphone and an external audio interface).
- [ ] Auto / In / Off monitoring works, the input meter is visible before recording.
- [ ] The loopback test measures latency and saves the adjustment.
- [ ] Three takes over a loop, comping with the mouse, "Flatten Comp".
- [ ] A sample from the browser and a file from Finder land on a track and play in the session and on the timeline.
- [ ] Always-on audio capture captures a guitar phrase played without recording.
- [ ] A MIDI track with the "Audio" source records its synth to audio.
- [ ] All tests green, CI green, README updated.
