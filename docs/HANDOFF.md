# Handoff (read right after CLAUDE.md)

This file is updated at every notable step. A new chat, Codex or a person continues from here: what is done, what is in progress, what the next step is. The state lives in the repository, not in a chat.

## Track controls, Prisma bank and Distortion (2026-10-21, local review)

Owner request: visible per-track stop and on/off in Arrangement, a factory sound bank for the existing Prisma, and Distortion. Local branch `codex/track-controls-presets` starts at `9b225c5` (both glass passes retained). No push or PR until requested. Scope: expose the existing mute/stop/return commands without changing playback semantics or project schema; append presets without changing the first 16 programs or synth DSP; expose a separate Distortion using the existing oversampled drive DSP with harder defaults. Small related improvements: state-specific help and header context actions. No broad stage-09 synth rewrite.

Files owned: `app/Source/Main.cpp` (opt-in verification captures), `app/Source/ui/ArrangementHeaders.cpp`, `SessionView.cpp`, `UiCommon.h/.cpp`, `app/Source/Strings.h`, `app/Source/engine/BuiltInEffectProcessor.cpp`, `core/src/PrismaParams.cpp`, `core/src/fx/Drive.cpp`, `Registry.cpp`, `tests/ArrangementUiTests.cpp`, `ArrangementTests.cpp`, `MixTests.cpp`, `BrowserTests.cpp`, `core/tests/core_tests.cpp`, `core/tests/fx/ColourTests.cpp`, README, this handoff, verified screenshots under `media/ui-glass/`. Owner's untracked design assets stay untouched.

Implemented: shared power icon in Arrangement and Session, undoable project-persisted output on/off, separate per-track Stop/Resume using the existing stop and Back to Arrangement commands, state-specific EN/RU help, dimmed disabled Arrangement names, header menu commands. Other tracks and transport continue; Session stopping and timeline resumption retain launch quantization. The Session/timeline source switch remains separate. No engine scheduling, audio-thread, synth DSP or project schema changes.

Prisma now has 44 factory programs: 28 appended sounds across bass, lead, pad, pluck, keys/organ, bells, strings, brass and FX, including the owner's subsequent cyberpunk request (Neon Grid Bass, Razor Bass, Chrome Wobble Bass, Overload Bass). Modern basses use mono/sub, drive, sync/fold tables and filter motion; gain was calibrated after measuring every patch. First 16 programs and all parameter values are frozen by a compatibility fingerprint. Distortion is a separate `distortion` effect using the existing 8x oversampled drive engine, Hard mode / 18 dB by default; `drive` keeps its original defaults, IDs and sound. Browser listing, compact macros, state and existing automation/device workflows apply.

Verified: full native arm64 Release build (app, AU/VST3/Standalone, all test/tool targets; plugin installation OFF); all 10 ctest suites pass after linking finishes, 2076 JUCE checks in each language, 6736 core checks, 5370 FX checks. Added coverage: per-track stop leaves another held note and transport running, UI stop/resume, shared power and one-step Undo, compact header hit targets, clip controls hidden on return buses, legacy bank fingerprints, parameter bounds, preset note release, all 44 program levels, browser availability, Distortion harmonics/equivalence, dry/latency/silence checks, state/macros and no audio-thread heap use. An early test run during linking exceeded the existing 1 ms callback timing threshold; the full isolated rerun passes. Existing compiler warnings documented in the glass entries remain; no diagnostics were suppressed. Logs: `/private/tmp/prisma-tracks-all-build.log`, `/private/tmp/prisma-tracks-bus-build.log`, `/private/tmp/prisma-tracks-bus-tests.log`.

Native captures: 20 images in both themes, including the new bass preset and Distortion; gallery and README updated. Local ad-hoc signed application: `dist/track-controls-preview-2026-10-21/DAW.zip` (signature verified again after extracting), unpacked `/private/tmp/prisma-track-controls-preview/DAW.app`. Same folder has `cyberbass-demo.wav`: four raw bass patches in the order above, the same 16-beat phrase at 140 BPM each; measured peaks -12.6 to -13.8 dBFS, no normalization or external effects. Next: owner's live audition and track-control review; any publication requires a new owner request. Nothing pushed and no PR created.

## Glass depth follow-up (2026-10-21, local review)

The owner asked to bring the stronger glass impression of the new dark concept into the existing app quickly, limited to material/theme work. Kept the layout, functions, opaque matte foundation and sharp labels; strengthened diffuse colour depth and selective rim reflections in both themes. Work is local on `codex/glass-theme` from `f03b186`; no push or PR. Files: `app/Source/ui/GlassMaterial.cpp`, `app/Resources/themes/glassdark.json`, `tests/UiTests.cpp`, `docs/design/GLASS-UI.md`, `README.md`, this handoff and verified images under `media/ui-glass/`. Existing reference images and earlier local preview are retained.

Implemented: two broad shape-clipped ambient reflections in shared satin panels and raised controls, brighter narrow rim reflections, a lighter graphite dark palette, more ambient light through plates and cards, and a subdued timeline grid. Reflections scale with glass depth and disappear with the opaque fallback. No content capture, new renderer, animation, timer, layout or engine change. The Session mixer's opaque ground remains intact.

Verified: native arm64 Release app/tests build, all 10 ctest suites pass (1975 JUCE checks in each UI language). New tests sample main/secondary label contrast at three material depths and confirm custom accents do not recolour card reflections. Existing continuity, graphite, opaque-fallback, hidden-splitter and real-time checks stay green. Final native captures cover both themes; the local ad-hoc signed app was archived and its signature checked again after extraction. Preview: `dist/glass-depth-preview-2026-10-21/DAW.zip`; unpacked copy: `/private/tmp/prisma-glass-depth-preview/DAW.app`. Logs: `/private/tmp/prisma-glass-depth-build.log`, `/private/tmp/prisma-glass-depth-final-build.log`, `/private/tmp/prisma-glass-depth-final-tests.log`. The inherited warning limitation documented below remains; no warnings were suppressed. Next: owner's local visual review and remaining warning cleanup before any publication.

## Glass material refinement (2026-10-21, local review)

Codex owns branch `codex/glass-theme`, created from the owner's committed `claude/gpu-glass` (`fa8968b`). The owner is not editing this checkout during the work. Scope: match the approved light Arrangement mockup with a silver-blue/lavender ground, quieter panel and control edges, and flat blue browser selection, retaining layout and control behaviour. The owner also requested a corresponding dark-theme colour pass: neutral graphite, restrained blue/lavender accents and softer edges. The latest reference supersedes the clear-lens selection rule for browser rows.

Files being changed: `app/Resources/themes/glasslight.json`, `glassdark.json`, `app/Source/ui/Theme.h`, `Theme.cpp`, `GlassMaterial.h`, `GlassMaterial.cpp`, `Kit.cpp`, `BrowserPanel.cpp`, `Chrome.cpp`, `tests/UiTests.cpp`, this handoff, `docs/design/GLASS-UI.md`, `README.md`, and verified captures under `media/ui-glass/`. No engine, DSP, project format or plugin changes are planned. Next: implement shared material changes, build without plugin installation, run ctest and capture both themes. Existing untracked design assets belong to the owner and stay out of commits.

Additional files: `app/Source/ui/DetailPanel.cpp` paints its own ambient ground; `SessionView.cpp` keeps the mixer plate opaque beneath satin cards. Owner's final correction: hide resting splitter guide lines, showing them on hover/drag only (`Chrome.cpp`).

Publication override (owner, 2026-10-21): develop and review locally for longer before uploading. Do not push this work or create a PR until the owner asks. Nothing from this branch has been published. A local preview build and captures are the next deliverable; local commits are allowed.

Implemented: silver-blue/lavender light material, neutral graphite dark material with softer clip saturation, theme-owned ambient colours independent of custom control accents (optional tokens with backwards-compatible parsing), narrower rims and calmer control bodies, flat tinted browser rows, sharp labels above the view-switch lens, hidden resting splitter guides, and an opaque ambient ground beneath Session mixer controls. Layout and all hit areas/gestures are unchanged.

Verification: native arm64 Release app and tests build with plugin installation OFF. `ctest` passes all 10 suites, including both UI languages, after the final splitter/mixer changes. New checks cover optional token validation, legacy themes, text contrast, ambient continuity (three composited gradients can differ by up to three byte levels at different raster clip origins), custom-accent isolation, opaque reduced-transparency fallback, neutral graphite and splitter hover/drag visibility. Full screenshots are captured separately from UI tests, whose synthetic keyboard events can otherwise affect the snapshot app. The tests and captures need macOS system-service access: sandboxed CoreMIDI tests crash; the native run passes.

Warning-free verification is not achieved: this base has existing warnings in `Strings.h`, other unchanged sources and JUCE 8.0.4 with the installed AppleClang/SDK. No warnings were suppressed. Local build logs: `/private/tmp/prisma-glass-theme-build.log`, `/private/tmp/prisma-glass-theme-local-build.log`; final tests: `/private/tmp/prisma-glass-theme-local-tests.log`. Xcode's MacOSX26.5 SDK is selected explicitly because the command-line default SDK is incompatible with this linker. Local review app: `dist/glass-theme-preview-2026-10-21/DAW.zip` (ad-hoc signature verified before archiving; unpacked copy at `/private/tmp/prisma-glass-theme-preview/DAW.app`). Next: owner's local visual review, then address remaining warnings before publication. No push, PR or merge.

The owner subsequently requested a separate, freer dark concept, allowing future features, and clarified the material: the large base surfaces are very matte, almost opaque glass; only selected details are clearer. This is image exploration, not authorization to add those features to this implementation.

## UI kit (2026-10-19, in progress)

The owner agreed to lay the interface down once instead of restyling screens one by one: tokens for sizes, one set of control painters (`ui::kit`), then the screens moved onto them, then a GPU glass backdrop. Plan and progress: [UI-KIT.md](design/UI-KIT.md). Branch `claude/project-thread-ayt3r1`; round three (glossy material, sphere knobs) went out as a bundle for its own PR.

## Glass round two (2026-10-19, after PR #27)

Branch `claude/project-thread-ayt3r1` again, from main `e151afe`. The owner asked to bring the whole UI to the ten design sheets (`media/design/prisma-glass-system/`, local only: they name other products), with placeholders "in their ideal look" where a feature does not exist yet. Stage 1 done (main window): Session/Arrangement/Mix switch with a Mix placeholder (`ui::ComingSoonView`), status bar, browser icons, marker section bands, glass device cards, knob values, Capture dot, chevrons. Stage 2 done: piano roll (Score/Step placeholders), settings (icon tabs, theme thumbnails), export (details card), versions (cards), synth editor (envelope/LFO/filter pictures, coloured wavetables), drum pads. Not built from the sheets: a real Mix view with inserts and a routing matrix (05), the expanded library with tags and preview (08), sampler/sequencer/Touch inside the drum rack (07), the audio take comping and Voice panel (04): these are features, not restyling.

## Glass appearance (2026-10-19)

PR #27 (made on the owner's Mac in branch `claude/project-thread-ayt3r1`, thread "glass", pushed as `claude/project-thread-wdk0ad`): the glass themes from Codex's local `codex/glass-ui-current` (built on main `be186ab`, PR #25 logo kept), then a clean-up pass after the owner said the UI looked crooked. Fixed: every cell drew two rims (now `glass::cell`, flat with a top light); clip fills and selection outlines had different corner radii; the clip colour bar ran past the rounded corners; transport buttons and fields were separate boxes of mixed heights (now one glass capsule, borderless buttons, separators between groups); lit chips and mixer buttons used the dark text accent (now the fill accent with contrast-checked ink); the drop hint was squeezed horizontally; combo boxes and text fields had square corners; lens thumbs were drawn at 10 % opacity (a shadow colour left in the context before `drawImage`). New: a draggable glass lens that refracts the top-bar spectrum (see [GLASS-UI.md](design/GLASS-UI.md)). Then, from the owner's two reference sheets (Session and Arrangement in light glass, "make it like this"): floating rounded cards with gaps (`ui::CardFrames`), a top row with project name, pill view switch, version, undo/redo, Export and a settings gear, a Library button over the browser and the transport capsule with a round green play, pastel clips with deep-coloured notes and Session note miniatures, glass faders with a dB scale, thin meters, a green level slider for the Arrangement header volume. The sheets' Mix tab, Scenes header with launch quantisation, clip progress outline and the bottom panel's preview/EQ/reverb cards are not built (no such features yet). Packaging: `scripts/package-mac.sh`, used by CI; `PRISMA_COPY_AFTER_BUILD` is off by default and on in `scripts/build-mac.sh`. A new settings file starts in Glass · Dark; saved themes are kept.

Captures: `media/ui-glass/` (22 PNGs, `DAW_UI_SNAPSHOTS`). Not done: window-wide backdrop refraction, pointer-following glints, a visual pass of Audio/Warp, drums, export, the effect windows and the four classic themes' settings/synth windows.

Still local on the owner's Mac, not in this branch: `codex/vocal-plugin` (Prisma Voice, a standalone vocal processor) and `codex/touch-drums` (Prisma Touch). They sit on an old base and need their own branch onto main. Concept images and the design-system study in `media/design/` (untracked) stay out of the repository: they name other products.

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

Logo and app icon (thread "design and logo"): `ui::drawLogo` redrawn as the glass prism with the incoming beam and six spectrum bars, theme-aware; the app icon is `app/Resources/brand/AppIcon-1024.png` (`ICON_BIG`), sources in `media/brand/`. A design pass is being discussed with the owner: three directions for the main window (glass and light, instrument, living canvas); The owner subsequently selected the glass direction; see the integration status at the top of this file.

README screenshots were retaken with the English UI in the Prism theme (one theme per screen; the old Russian ones in other themes were removed). How: `DAW_LANG=en DAW_THEME=prism` under Xvfb, sounds dropped from a Places folder (settings key `placeFolders`) with xdotool drags.

Owner's live-test list after the demo song (2026-10-18), all seven done on `claude/project-thread-0292na`; the demo check's own list is in `docs/demo/README.md`:
1. **Per-track Session/Arrangement switch.** Each track chooses on its own whether it plays the timeline or Session clips (sections, live). Switching a track to live starts its loop on the bar.
2. **Grid inside clips on the timeline.** The bar/beat grid is hidden under clip bodies in the Arrangement, so notes cannot be lined up by eye.
3. **Drag and drop of a track into the song**, with a choice of an existing track or a new one; more generally a fast way to add a track (a button, or empty space below the last track that accepts drops).
4. **New MIDI or audio tracks cannot be created in the Arrangement View.**
5. **Pan sliders react to the scroll wheel**, which causes misclicks while scrolling: mouse drag only.
6. **Tempo detection of dropped audio is often wrong, and there is still no warp.** Critical (stage 7, branch `claude/funny-euler-1vls48`).
7. **Waveforms in the Arrangement look bad when zoomed in, and the audio clip editor at the bottom cannot zoom.**

After the demo song (branch `claude/project-thread-vk84kl`): every item of `docs/demo/README.md` and part of the backlog.
- Master as a pseudo-track: `ProjectModel::kMasterTrackId` (0), `findTrackOrMaster` (only chain, detail, automation and browser-drop code use it; `findTrack` never returns the master), `TrackRef::isMaster/hasInstrument`. Master automation `PlaybackGraph::masterAutomation`, built by `AudioEngine::automationOf`.
- Tempo envelope (7.5, tempo part): lane `song:tempo` on the master (`automation::tempoFromNormalised`, 40–240 BPM), applied in the audio callback before the transport advances, read at the middle of the block; `AudioEngine::getCurrentTempo`; the tempo field scales the envelope. Time signature changes are not done.
- Export: `core` `daw::dsp::measureLoudness` (BS.1770 LUFS, LRA, true peak), LAME 3.100 in `third_party/lame` (`engine/Mp3Writer`), stems via `TrackRuntime::stemBuffer` + `AudioEngine::setStemCapture`, LUFS normalization capped at −1 dBTP, TPDF dither at 16 bit. Tests: `tests/MixTests.cpp` (`DAW_TEST_ONLY=mix`).
- VST3 folders: `engine/PluginFolders`, Settings → Plug-ins. `.prisma` document type, `project::projectFileFromCommandLine`, `MainComponent::openFromSystem`.
- Prisma factory presets 8–16 appended (programs are stored by index: new presets only at the end).

Not done yet (by importance): PDC, snapshots and morph, MIDI Learn, groups, always-on audio capture (5.7), comping with the mouse (5.5), time signature changes, a design pass (another thread).

## How to continue

1. Read `CLAUDE.md`, `docs/spec/00-rules.md`, this file, and the spec of the current stage.
2. Work on the stage branch, commit and push often: unpushed work disappears with the container.
3. After each step, update the "Now" section here (what is done, what is next, the PR number).
4. Merge into `main` only with green CI. The owner has authorized merging green stage PRs.
