# 99. Small things that make the DAW pleasant to use

A list of details that are easy to forget, with the stage where each one is done. Whoever implements a stage checks their section of this list together with the "Done when" section. If a detail is already described in the stage file, there is a reference here; if not, it is part of the stage as a small task.

## Stage 1 (window, project)

- Track and clip color changes with a single click on the palette, without a dialog.
- Double-clicking any numeric field (tempo, volume, parameter) opens keyboard entry; `Enter` confirms, `Esc` cancels.
- Dragging a knob with `Shift` is fine adjustment (×0.1); a double click resets to the default value.
- The mouse wheel over a knob changes it; over a list it scrolls.
- Column width in Session View is resized by dragging the header edge.
- Hints (InfoBar) exist for every element, including device menu items.
- Window title: the project name and `•` if there are changes.
- Recent projects in the start window with a screenshot thumbnail (a snapshot of the window on save, 320×200).
- Deleting a track with clips and devices does not ask for confirmation: there is `Cmd+Z`.
- CPU indicator in the transport; clicking it opens the audio settings.
- MIDI input indicator in the transport blinks on incoming events, with a separate one for "Control" (06).
- Empty states for the browser ("Nothing found for “…”") and for an empty project ("Drag an instrument from the browser here").
- Keyboard shortcuts are shown to the right of menu items.

## Stage 2 (MIDI)

- A played-note counter in the "10 min" tape is not needed, but the hover hint shows "Last 10 minutes: 1,240 notes".
- A note in the piano roll dragged past the clip edge extends the clip loop (confirmed by highlighting).
- `Cmd+Shift+arrows` in the piano roll: transpose the selection while staying in the scale.
- A "Chord play" mode for the Mac keyboard (one key plays a triad of the scale), off by default.
- Invert note selection; "Select all notes of this pitch" by clicking a key.
- Note preview when a note is selected (headphones in the panel).
- An empty MIDI clip is not created after a recording with no notes; instead, the message "No notes were played" appears.

## Stage 3 (Arrangement)

- Clicking an empty spot on the ruler during playback jumps quantized; with `Shift`, immediately.
- Marker colors and a marker list in a dropdown menu for quick navigation.
- "Consolidate" (`Cmd+J` on a time selection across several tracks): clips are joined on each track.
- Track freeze (Freeze, right-click on the header): the track is rendered to audio by the offline engine (04) and plays from the file; devices become unavailable until unfreeze. MIDI tracks only here; it comes after 04, so in practice it is done in 04.
- The grid shows bar numbers on the mini-map as well.

## Stage 4 (export)

- A macOS sound and a notification when a long export finishes, if the window is not focused.
- The last export folder is remembered.
- Freeze and Flatten (see stage 3).
- Export a time selection with one button from the ruler's right-click menu.

## Stage 5 (audio)

- Clip normalize (right click) and "Reverse" without rewriting the file.
- Automatic trimming of silence at the start of a recording (option).
- A warning if the file's sample rate differs from the project's (with automatic resampling during playback).
- A free disk space counter in the recording window, with a warning at < 2 GB.
- "Delete Unused Recordings…" and "Collect and Save" (copy all external files into the project).

## Stage 6 (effects, mixer)

- "Reset peak" by clicking the meter, with the peak value shown above the meter.
- Full-screen mixer mode (`Cmd+Alt+M` twice), wide faders.
- Dragging a device from one track to another (Alt copies).
- Group volume change for selected tracks (moving one fader changes all selected tracks).
- A mono switch and phase inverter in the chain (a "Utility" device: volume, pan, width, mono, phase, DC).
- No limiter on the master by default, but there is a "Mastering" template in the chain browser (08).

## Stage 7 (warp)

- Clicking the tempo in the transport with `Alt` is tap tempo, no separate button needed.
- Loop preview in the browser in sync and at the project tempo (by stretching).
- Automatic key detection for a sample (chromagram + Krumhansl profiles), shown in the browser.

## Stage 8 (library)

- Preset hot-swap, previews, tags: described in 08.
- A user project template from the library ("New Project from Template ▸").
- Community sound packs: the `.dawpack` format (08) and a "Packs" folder in the browser.

## Stages 9–10 (synths)

- A random preset with locks on selected sections (Prisma), a "More Like This" button (Alloy, by tags).
- Saving a preset directly from the DAW also saves its preview.

## Later (phase 3 and release)

- Link (the open tempo-sync protocol), MIDI Clock (leader and follower), MIDI outputs to external synths (an "External Instrument" track).
- Push, Launchpad, Launchkey (`ControlSurface` scripts), Maschine MK3 in MIDI mode.
- Additional UI languages beyond English and Russian (all strings are already in `Strings.h`).
- Apple code signing and notarization, auto-update (Sparkle), crash reports with user consent.
- Isolated plugin scanning in a separate process.
- A video track for scoring music to picture.
