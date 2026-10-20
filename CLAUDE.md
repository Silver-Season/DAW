# CLAUDE.md

The rules for every agent are in AGENTS.md, imported here; read it first. This file adds only what is specific to Claude.

@AGENTS.md

## Where we are

- Done: stage 1 (project model, engine, window, browser, themes, saving, versions, audio settings), stage 2A (piano roll, scales, quantize, A4 tuning), stage 2B (metronome, count-in, MIDI recording into slots, overdub, always-on capture, controllers), stage 3 (Arrangement: clips and markers, song clock with loop and relocation, chasing notes, Session versus Arrangement, timeline editing, recording to the timeline with takes and punch, Session recorded into the Arrangement, taking a track from a version), stage 5 (audio tracks, playback, audio recording, latency compensation), stage 6 (13 built-in effects, sends and returns, sidechain, drum machine, Inspiration generators, automation), the core of stage 4 (offline export to WAV/AIFF/FLAC/MP3, stems, LUFS), warp parts 1–2, English UI with Russian as a second language, the glass design.
- The current state and the next step are always in [docs/HANDOFF.md](docs/HANDOFF.md).
- Next, in order: the UI foundation (components with all states, layout tokens, glass on the GPU), the rest of stages 4–7, then the synths (09 Prisma to release level, 10 Alloy) and the library (08). Ideas agreed with the owner for later: stem separation (Demucs/Open-Unmix via CoreML); a generator plugin (presets by sound type and scale-aware MIDI, after the synths); stage 11 DJ (2–4 decks, Pioneer DDJ-FLX6, sync with the transport, Link/MIDI Clock for Maschine); 12 vocal tools (draft in docs/spec/12-vocal.md); 13 mastering module; 14 an AI assistant through the Anthropic API with the user's own key.

## Claude's part

- Claude leads: it owns the engine, the model, the UI and the synths (see the areas in AGENTS.md), keeps `docs/HANDOFF.md` and the specs current, and reviews every PR from Codex.
- At most two Claude threads work in parallel, one per area. A finished thread is closed.
- No shortcuts on the audio engine and the project model: tests and a review always. UI polish comes from the owner's live tests on the Mac (sound, external audio interface, MIDI keyboard), sent as one message per round; bundle one round into one PR.

## Reviewing a Codex PR

Codex works in the effects area ([docs/agents/EFFECTS.md](docs/agents/EFFECTS.md)) and on art. For its PRs:

1. The diff stays inside its area; anything outside goes back as a review comment, not a silent fix.
2. CI is green on macOS and Linux, including the effect contract tests (`daw_fx_tests`) and both language runs.
3. The effect's own tests check what its backlog task asks for, numerically; read the DSP for audio-thread rules the tests cannot see (locks, unbounded loops, denormals).
4. Ids are new, appended, and never renamed; Russian names are in `app/Source/FxStrings.h`.
5. Merge when all of that holds, then note it in `docs/HANDOFF.md`.
