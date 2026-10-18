# Prisma Voice: vocal correction and timbre change

A standalone **VST3 / AU / Standalone** plugin, prepared for later integration into the DAW ecosystem
by the team lead. The working name is Prisma Voice; the legal availability of the brand has not been checked.
Documentation prepared by Codex on October 17, 2026, for the implemented version.

## Two scenarios

**Live / Auto** processes audio continuously. The same plugin can be placed on a live
microphone input or on a track with already recorded vocals: no separate import is needed for that.

**Phrase editor** works with a local phrase: import or input capture, note detection,
manually changing their target pitch, re-rendering, listening and WAV. Manual edits in this editor
do not automatically replace the host's clip: a file export is used to bring the result back.
ARA and a native link with Prisma clips are not implemented yet.

![Automatic processing](../../media/voicer-preview.png)

## Automatic processing

1. Load the plugin **as an effect on an audio track** with a dry monophonic vocal.
2. For a live input, enable monitoring in the host. In the Standalone, choose the device and input
   via Options; allow microphone access if needed. Use headphones.
3. Choose the root and scale: Chromatic, Major, Minor or Minor pentatonic.
4. Set Correction and Retune, then the voice character and the extra effects.
5. Compare the processing with the source using the Compare dry button, and watch the output level.

| Setting | Behavior |
| --- | --- |
| Correction | Strength of the pull toward the nearest allowed note |
| Retune | Smoothing of correction changes, from instant to 300 ms |
| Natural drift | A small zone around the target note without full pitch straightening; not a separate vibrato analyzer |
| Transpose | Overall transposition from −12 to +12 semitones |
| Formant | Change of the voice's spectral character from −12 to +12 semitones |
| Preserve formants | Compensation of formant changes during transposition |
| Wet / dry | Mix of the source and the processing, aligned for algorithmic latency |
| Compression | Basic dynamic leveling with a fixed threshold |
| De-ess | Dynamic attenuation of the high-frequency content |
| Drive | Saturation |
| Doubler | Short delays that differ per channel |
| Echo | Echo with a fixed time of about 240 ms and feedback |
| Reverb | Reverberation |
| Output | Overall level |

The detected and target notes, the measured pitch history, levels and DSP latency are shown.
All listed parameters are automatable and saved by the host. There is no automatic key
detection: Chromatic is the initial choice, and the musical scale is chosen by the user.

**The current algorithmic latency is 40 ms at 48 kHz**, on top of the audio device.
The plugin reports it to the host; the engine accounts for it in dry/wet, and the phrase export removes the corresponding
pre-roll. This is not yet comfortable low-latency monitoring for every performer.
Before a public live release, the latency has to be reduced and the quality checked on real vocals.

## Phrase editor

![Manual phrase editing](../../media/voicer-editor-preview.png)

- **Import**: WAV, AIFF, FLAC or MP3, one or two channels, up to 30 seconds, 8–192 kHz.
  A longer file is rejected rather than silently truncated.
- **Capture input**: recording of the plugin's own dry input, before its processing. Maximum 30 seconds;
  **Stop** ends the capture. The host must actually pass audio: the button does not arm the
  track or start the DAW transport.
- After stopping, local analysis runs. Before closing the editor or saving
  the project, it is better to wait for the analysis to finish. Closing the editor requests that the capture stop.
- Gray segments are the detected notes. Select a segment and change **Note target**;
  a manual target can be at most an octave away from the original note. A manually set note
  takes priority over the scale; correction strength and speed remain global.
- **Reset note** clears the manual target. **Undo / Redo**, Cmd+Z / Cmd+Shift+Z undo target
  edits within the current editor session.
- **Render edits** re-renders the phrase with the current parameters and manual targets. Changing
  parameters after a render requires a new render: the finished result is a snapshot of the settings.
- **Audition** plays the result through the plugin output, temporarily replacing its live processing.
  This is local listening, not synchronized with the transport. The host must keep processing audio.
- **Export WAV** saves a 24-bit WAV at the source phrase's sample rate. If the result exceeds 0 dBFS,
  export is blocked: lower Output and render again. No automatic normalization is applied.
  With spatial effects, the export contains an extra tail.

The source phrase and the manual targets are saved in the plugin state together with the parameters.
The audio is stored inside the state, so the project can grow noticeably: this is a deliberately
simple way to make a standalone plugin portable until native integration.
A finished capture that has not been analyzed yet can also be saved as the source phrase;
an unfinished recording is not considered a finished saved take.
The Undo history and the rendered audition cache are not included in the state.

## Limits of this version

This is a first working version, not a proven replacement for dedicated pitch-correction tools.

- It is designed for a dry **monophonic vocal**; a choir, a full mix, heavy reverb and noise
  can cause detection errors. The pitch estimation range is approximately 65–1000 Hz.
- The editor changes the target pitch of segments. There is no stretching of individual syllables, no dragging of
  note boundaries, no detailed vibrato curve, no polyphony and no per-note formants.
- Timbre change is done with DSP processing. There is no AI conversion, voice cloning or neural
  network models. A harmonizer and full cleanup of noisy vocals are also not claimed.
- The current parameters and the simple effect set need listening and tuning on real
  recordings. Numerical DSP tests do not confirm how natural the voice sounds.
- The build for Apple Silicon has been checked. Checks in third-party DAWs, AU validation, the microphone
  route, Intel/universal and the full user scenario with file selection are still needed.
- The local test build has an ad-hoc signature, not a Developer ID / Apple notarization.

## Developer materials

- [Build and source map](../../plugins/voicer/README.md).
- [Handoff to the team lead: API, state and integration risks](../VOICER-HANDOFF.md).
- [Dependencies, versions and licenses](../../plugins/voicer/ThirdParty/UPSTREAM.md).
- [Synthetic sound check](../../media/voicer-dsp-test.wav): source, auto correction, manual target.

The integration code follows the repository's AGPLv3. Signalsmith Stretch/Linear are MIT, Onest is SIL OFL;
their notices are kept and included in the bundle. Processing is local, with no cloud services.

## If the recording is not visible or audible

In the Standalone, open **Options → Audio/MIDI Settings**, choose the microphone and uncheck
**Mute audio input**. JUCE disables the input by default to protect against feedback;
use headphones. Allow microphone access in the macOS settings.
When speaking, **INPUT** should move (the meter is now logarithmic, from −60 dBFS).
In a DAW, check the input routing and continuous audio processing.

After Capture input → Stop, the editor shows the source waveform regardless of note
detection. **Play original** lets you hear the source recording without rendering. For the processed
result you need **Render edits → Audition**. If silence was captured, the status line
says so directly; the absence of notes by itself does not mean there is no audio.

### Record and then listen in the standalone app

In the Standalone, the microphone is now **not sent to the speakers** by default, but it does reach
the INPUT meter, the analyzer and the recording. In Options you need to enable the input itself
(uncheck Mute audio input) and choose the microphone. Then Capture input → Stop →
Render edits → Audition. The source is available via Play original.

**Monitor input (headphones)** separately enables live processing into headphones.
Starting a recording or starting an audition automatically turns this monitoring off.
Its enabled state is not saved between launches. In VST3/AU the monitoring route
is controlled by the DAW, this switch is hidden and the track processing stays on.
