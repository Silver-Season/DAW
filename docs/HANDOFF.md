# Handoff (read right after CLAUDE.md)

This file is updated at every notable step. A new chat, Codex or a person continues from here: what is done, what is in progress, what the next step is. The state lives in the repository, not in a chat.

## Glass appearance integration (2026-10-19)

Branch `codex/glass-ui-current` starts from the verified GitHub main `5cf1c9d64a77164811909aa7444fae43678051d1`. The earlier local design checkpoint is preserved on `codex/glass-ui` (`de942b1`); it was based on an old project version and must not be merged over the current Arrangement/recording/warp/export implementations.

The current branch transfers the common rendering layer and configurable light/dark glass themes onto the real current views. It keeps all current recording, transport, Arrangement, audio/warp, effects, drum, automation and export code. Original themes, custom user themes, UI scale and the English/Russian language switch are preserved. The app's built-in synth follows the host's theme. The native logo uses the approved prism, beam and six parallel spectrum tracks in two contrast variants.

See [GLASS-UI.md](design/GLASS-UI.md) for settings, rendering boundaries and native capture instructions. The local-track CPU lens is intentionally documented separately from the HTML study's full canvas refraction. Full-window refraction, pointer-following glints and a complete one-for-one recreation of every conceptual screen are **not finished**. The prototype is a design reference, not evidence of a native feature. No DSP or project schema changes were made.

Validation status and final capture location are recorded below when the branch is ready for review. Publishing/PR creation remains separate from the local implementation; do not merge an untested design branch into main.

## Now (2026-10-18)

Order agreed with the owner: stage 5 → stage 6 (effects, mixing, automation) → stage 4 (export) → design and spectra → English UI. Goal: the app can be used on its own for a whole track (beat, vocals, processing, export).

In `main` (PR #16): audio recording (5.3, 5.4), 13 built-in effects (`core/src/fx`), sends A/B to return tracks, sidechain for the compressor and gate, the drum machine `DEVICE kind="drums"` (`core/src/drums`), Create → Inspiration (`core/src/gen`, `app/Source/Inspiration.cpp`), automation (`TRACK/AUTOMATION/LANE/POINT`, mode `A` in the Arrangement), export (Cmd+Shift+R: offline render of a project copy, WAV/AIFF/FLAC, peak normalization, tail), loops with a known tempo follow the project tempo (Re-Pitch), a spectrum of the master in the top bar.

English UI and English repository (branch `claude/english-standalone-87c15d`):
- Every UI string in `app/Source/Strings.h` is `tr ("English", "Русский")`; texts with different word order, plurals or decimal commas branch on `isRussian()`.
- The language is chosen in Settings → Appearance → Language and applies after a restart. It is kept in a small `language` file next to the settings file, because the strings are made before `main` (`app/Source/Language.h`, `Settings::setLanguage`). `DAW_LANG=en|ru` overrides it (screenshots, tests).
- Theme JSON files have `name` (English) and an optional `nameRu`; scales in core have `name` and `nameRu`.
- ctest runs the engine tests twice: `daw_engine_tests` (English) and `daw_engine_tests_ru`.
- README, CLAUDE.md, AGENTS.md, ARCHITECTURE and every spec are in English; comparisons with other products were removed from docs, comments and UI text.

Demo song (branch `claude/project-thread-t76imp`): "Mountain King" after Grieg, built through the model by `tools/demo_song/MountainKing.cpp` (target `daw_demo_song`) and rendered with `OfflineRenderer`; the project is `docs/demo/Mountain King.prisma`. The problems the check found are listed in `docs/demo/README.md`; fixing them is the owner's call.

Warp part 1 and the owner's live-test list: merged in PR #22 (branch `claude/project-thread-0292na`).
- Warp (7.1, 7.2): `daw::dsp::estimateTempo/detectOnsets` (Onsets.h; the double-time decision and the beat tracking read the band below 2.5 kHz, so off-beat hats do not double the tempo), Rubber Band v3.3.0 in `third_party/rubberband`, `daw::dsp::Stretcher/stretchOffline`, `engine/WarpService` (tempo analysis of imported files, stretched copies rendered in the background, Re-Pitch until ready; `setRenderInline` for export), CLIP `originalTempo`, `warpMode`, `warp`; schema 4 migrates older warped clips. Tests: `tests/WarpTests.cpp` (`DAW_TEST_ONLY=warp`), `daw_analysis_tests`, `daw_stretch_tests`.
- Live-test list: 1 per-track Session/timeline switch (`AudioEngine::launchClipOnBar`, `backToArrangement (trackId)`, button in `ArrangementHeaders`); 2 grid inside clips (`ArrangementLanes::paintGrid`); 3 "+ MIDI" / "+ Audio" and a drop area under the last track; 4 add-track menus in the Arrangement; 5 pan ignores the wheel (`Knob::setWheelChangesValue`, `PanDrag`); 6 warp above; 7 waveforms from samples when zoomed in (`Waveform.cpp`), zoom in the audio clip editor.

Warp part 2 (branch `claude/project-thread-aqlatg`, thread "warp"; the design direction is at the top of `docs/spec/07-warp.md`):
- The first sound of an empty project sets the project tempo (`ProjectModel::adoptTempoOfFirstSound`, called by the drops and by `AudioEngine::analyseAudioFile`).
- Warp markers: CLIP/WARP/MARKER (`sample`, `beat`), `daw::engine::WarpMap` (core), `ClipRef::warpMap`; the engine reads the original along the map (`AudioPart::warp`) and renders stretched copies along it (`WarpService::stretched` with key frames, `daw::dsp::stretchOfflineMapped`: Rubber Band offline key frames for Complex Pro and Tones, segment by segment for Complex and Texture, `stretchBeats` for Beats).
- Beats mode of our own (`core/src/BeatsStretch.cpp`), CLIP `beatsDecay`.
- Analyses (onsets, beats, confidence) cached in `<settings folder>/Analysis/<hash>.xml` (`WarpService::analysisFor`).
- Clip editor: decision strip, warp lane (beats of the sound draggable, markers), onsets, tint where the grid misses the beats, right-click menu (Bar 1 Here, Tempo From Here, Add Markers at Onsets, Quantize Markers, Slice at Onsets / on Grid), Decay.
- Tests: `daw_warp_tests` (core), `[7.3]` in `tests/WarpTests.cpp` and `tests/AudioUiTests.cpp`.
- Next (part 3): tempo and time signature track (7.5), the project following a performance played without a click, rendering ahead of the cursor (7.1), tap tempo, audio quantize with strength across tracks.
- `claude/fx-ui-wip`: new views for the built-in effects (compact views per type, expanding a card in place, its own window instead of GenericAudioProcessorEditor). The last commit is unfinished. Owner feedback: effect windows look cheap, the bottom panel shows 8 random knobs; it needs priority parameters, dropdowns (delay division) and in-place expansion. When merging it after the English UI, turn every new `utf8 ("…")` in `Strings.h` into `tr ("English", "…")`.

README screenshots were retaken with the English UI in the Prism theme (one theme per screen; the old Russian ones in other themes were removed). How: `DAW_LANG=en DAW_THEME=prism` under Xvfb, sounds dropped from a Places folder (settings key `placeFolders`) with xdotool drags.

Owner's live-test list after the demo song (2026-10-18), all seven done on `claude/project-thread-0292na`; the demo check's own list is in `docs/demo/README.md`:
1. **Per-track Session/Arrangement switch.** Each track chooses on its own whether it plays the timeline or Session clips (sections, live). Switching a track to live starts its loop on the bar.
2. **Grid inside clips on the timeline.** The bar/beat grid is hidden under clip bodies in the Arrangement, so notes cannot be lined up by eye.
3. **Drag and drop of a track into the song**, with a choice of an existing track or a new one; more generally a fast way to add a track (a button, or empty space below the last track that accepts drops).
4. **New MIDI or audio tracks cannot be created in the Arrangement View.**
5. **Pan sliders react to the scroll wheel**, which causes misclicks while scrolling: mouse drag only.
6. **Tempo detection of dropped audio is often wrong, and there is still no warp.** Critical (stage 7, branch `claude/funny-euler-1vls48`).
7. **Waveforms in the Arrangement look bad when zoomed in, and the audio clip editor at the bottom cannot zoom.**

Not done yet (by importance): PDC, MP3 and LUFS in export, snapshots and morph, MIDI Learn, groups, always-on audio capture (5.7), comping with the mouse (5.5), the tempo track (7.5), a design pass.

## How to continue

1. Read `CLAUDE.md`, `docs/spec/00-rules.md`, this file, and the spec of the current stage.
2. Work on the stage branch, commit and push often: unpushed work disappears with the container.
3. After each step, update the "Now" section here (what is done, what is next, the PR number).
4. Merge into `main` only with green CI. The owner has authorized merging green stage PRs.
