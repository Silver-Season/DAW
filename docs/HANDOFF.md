# Handoff (read right after CLAUDE.md)

This file is updated at every notable step. A new chat, Codex or a person continues from here: what is done, what is in progress, what the next step is. The state lives in the repository, not in a chat.

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

In progress on other branches, not merged (both stopped on 2026-10-18):
- `claude/funny-euler-1vls48`: owner's live-test fixes (Arrangement add-track menus, dropping anything from the browser on lanes, clip editor start and loop act on arrangement clips) and warp: `daw::dsp::estimateTempo/detectOnsets` (Onsets.h, `daw_analysis_tests`), Rubber Band v3.3.0 in `third_party/rubberband`, `daw::dsp::Stretcher/stretchOffline` (TimeStretch.h, `daw_stretch_tests`), `engine/WarpService` (tempo analysis of new files and stretched copies in the background, Re-Pitch until ready), CLIP `originalTempo`, `warpMode`, `warp`; "Warp", mode, file tempo, ×2, ÷2 in the clip editor. Not done: engine tests for warp and import analysis, a full test run, screenshots, warp markers and grid nudge (7.3), PR.
- `claude/fx-ui-wip`: new views for the built-in effects (compact views per type, expanding a card in place, its own window instead of GenericAudioProcessorEditor). The last commit is unfinished; merge it into the working branch on top. Owner feedback: effect windows look cheap, the bottom panel shows 8 random knobs; it needs priority parameters, dropdowns (delay division) and in-place expansion.
- **Merging either branch after the English UI:** `Strings.h` will conflict. Keep both sides, and turn every new `utf8 ("…")` from those branches into `tr ("English", "…")`, so that the English UI has no Russian left (`daw_engine_tests_ru` and a grep for Cyrillic inside `utf8 (` help).

Not done yet (by importance): PDC, MP3 and LUFS in export, snapshots and morph, MIDI Learn, groups, always-on audio capture (5.7), comping with the mouse (5.5), real warp (stage 7), a design pass, English screenshots for the older README sections.

## How to continue

1. Read `CLAUDE.md`, `docs/spec/00-rules.md`, this file, and the spec of the current stage.
2. Work on the stage branch, commit and push often: unpushed work disappears with the container.
3. After each step, update the "Now" section here (what is done, what is next, the PR number).
4. Merge into `main` only with green CI. The owner has authorized merging green stage PRs.
