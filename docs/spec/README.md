# Specification: phases 1–2

This folder holds the detailed specification for ten stages: seven from the interface design plus the sound library and two synths. Each file is written so that someone without the background discussions can carry it out, including a smaller model (for example, Claude Sonnet). The stage files themselves are in Russian for now.

Design document with the intent and decisions: [DAW: interface and feature design](https://claude.ai/code/artifact/8da3f345-ad9a-4826-9e7e-47df44273770). Interactive concept of the window and themes: [Prism: DAW main window](https://claude.ai/artifact/PUEfYcnAf9Wf62v8Xmkvtc); a copy is in [`docs/design/concept.html`](../design/concept.html).

| File | Stage | Depends on |
|---|---|---|
| [00-rules.md](00-rules.md) | Rules for every stage: build, real time, tests, verification, commits | — |
| [01-layout-project.md](01-layout-project.md) | Project model, new engine, window layout, browser, themes, saving, undo, version history, audio device settings | phase 0 |
| [02-midi-recording.md](02-midi-recording.md) | Metronome, count-in, MIDI recording, overdub, piano roll, quantization, always-on MIDI capture | 01 |
| [03-arrangement.md](03-arrangement.md) | Arrangement View, loop, markers, clip editing, recording from the session | 02 |
| [04-export.md](04-export.md) | Offline render, WAV / AIFF / FLAC / MP3, stems, normalization, comparing versions by ear | 03 |
| [05-audio-recording.md](05-audio-recording.md) | Audio tracks, audio clips, audio recording, latency compensation, takes, always-on audio capture | 04 |
| [06-effects-mixing.md](06-effects-mixing.md) | Six effects, sends and returns, groups, PDC, automation, snapshots and morphing | 05 |
| [07-warp.md](07-warp.md) | Warp, audio editor, tempo track | 06 |
| [08-library-drums.md](08-library-drums.md) | Sampler, 16-pad drum machine, factory drums and loops, one preset format, tags, hot-swap, packs | 06 |
| [09-prisma-pro.md](09-prisma-pro.md) | Prisma to release level: new oscillators and filters, built-in effects, 150+ presets, release UI | 01 (effects shared with 06) |
| [10-alloy.md](10-alloy.md) | Alloy, the flagship multi-engine synth: 4 sources, resynthesis, Transform Pad, preset browsing by attributes | 06, 09 |
| [12-vocal.md](12-vocal.md) | Draft: "Vocal", live pitch correction to the project scale and tuning, and graphical pitch editing | 02, 05, 06, 07 |
| [99-details.md](99-details.md) | Small per-stage details that are easy to forget | all |

Stages 8–10 can run in parallel with the main line after stage 6 (09 can start right after 01 if the effect DSP is moved into `core/daw::fx` as the first step).

An English UI (translation of `app/Source/Strings.h`, a language switch, English screenshots) is a separate, later stage that does not have a spec file yet.

New effects beyond stage 6 are a separate area with its own guide and task list: [`docs/agents/EFFECTS.md`](../agents/EFFECTS.md) and [`docs/agents/EFFECTS-BACKLOG.md`](../agents/EFFECTS-BACKLOG.md).

Prototypes that Codex built separately (a trackpad drum pad and a vocal processor) are in [`docs/reference/codex/`](../reference/codex/README.md) as reference for stages 08 and 12.

## How to hand a stage to an implementer

One stage per session. Tasks inside a stage are numbered and can be handed out one at a time if the whole stage does not fit. Request template:

```
You are working in the DAW repository (C++20, JUCE 8, CMake). Read docs/spec/00-rules.md
and docs/spec/0N-<stage>.md in full, then do tasks N.1–N.k of that stage.
The code of the stages before N is already in main. Follow the rules of 00-rules.md without
exception: a build with no warnings, all tests green, new tests for every task, UI screenshots
under Xvfb. At the end, report against the stage's "Done when" section: what was verified
and how, and what did not work out.
```

## How each stage file is structured

1. **Goal** in one paragraph and what the user sees at the end.
2. **Scope**: what is in and what is deliberately left out (not to be done early).
3. **Tasks**, numbered: which files to create or change, data structures, algorithms, UI behavior, edge cases.
4. **Tests**: the specific checks to write.
5. **Done when**: the acceptance checklist.

If an implementer sees that the specification contradicts the code or asks for the impossible, they do not invent a workaround silently: they say so in the report and pick the simplest option that does not break later stages.
