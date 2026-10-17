# AGENTS.md

Instructions for coding agents (Codex and others). The project is a macOS DAW in C++20 + JUCE 8; the owner writes in Russian, code and comments are in English, UI strings are Russian.

Read, in this order, before changing anything:
1. `CLAUDE.md`: build commands, known pitfalls (keys by key code, theme colours only, all strings in `app/Source/Strings.h`, real-time rules for the audio thread, one action = one undo transaction).
2. `docs/HANDOFF.md`: what is in progress right now and the next step.
3. `docs/spec/00-rules.md` and the spec file of the current stage in `docs/spec/`.

Rules:
- Work on a branch, open a PR to `main` with a "Before / After / How" description. Merge only with green CI.
- Every task ships with tests (`tests/`, `core/tests/`); `ctest` must pass and the build must have no warnings.
- Engine and audio-thread code must not allocate, lock, or use `juce::String` / `ValueTree`.
- Update `docs/HANDOFF.md` when you stop, even mid-task: what is done, what is not, the next step.
- `README.md` is in English and `README.ru.md` is its Russian version: when a stage adds a README section, update both. Describe features by what they do, not by comparison with other DAWs.
