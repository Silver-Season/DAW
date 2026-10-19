# AGENTS.md

The one instruction for everyone who works in this repository: Claude, Codex, any other coding agent, and people. `CLAUDE.md` imports this file and adds only what is specific to Claude. If a rule here and a file elsewhere disagree, this file and [docs/spec/00-rules.md](docs/spec/00-rules.md) win; fix the other file.

## What this is

An independent, open-source digital audio workstation for macOS with its own synthesizers (Prisma, later Alloy): a Session grid for launching clips across tracks and scenes, a linear Arrangement with takes and punch-in, MIDI and audio recording, built-in effects, a drum machine and a mixer. C++20 + JUCE 8, AGPLv3. The project owner is Silver-Season.

Languages: the owner writes in Russian, so conversations with the owner are in Russian. Everything in the repository is English: code, comments, commit messages, PRs, README and docs. The UI is English by default with Russian as a second language (Settings → Appearance → Language). Describe the app by what it does, never by comparison with other products, and never name other products in code, docs or UI.

## Read first

1. [docs/spec/00-rules.md](docs/spec/00-rules.md): build, real-time rules, undo, style, tests, screenshots. Mandatory.
2. [docs/HANDOFF.md](docs/HANDOFF.md): what is in progress right now.
3. The guide of your area (table below), or [docs/spec/README.md](docs/spec/README.md) and the file of the current stage.
4. [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): layers and roadmap.
5. [README.md](README.md): what the app already does, stage by stage, with screenshots.

## Areas: one area, one agent, one branch

Each area has one owner at a time. Work inside your area; outside it, change only the shared files listed below, with the smallest diff, and say so in the PR. If your task needs a change in someone else's area, open an issue (or ask the owner) instead of making it.

| Area | Paths | Owner | Guide |
|---|---|---|---|
| Effects | `core/src/fx/`, `core/include/daw/fx/` (except the `Effect` interface, see the guide), `core/tests/fx/`, `app/Source/FxStrings.h` | Codex | [docs/agents/EFFECTS.md](docs/agents/EFFECTS.md) |
| Effect plugins (AU/VST3 builds of the effects) | `plugins/fx/` (to be created) | Codex | [docs/agents/EFFECTS.md](docs/agents/EFFECTS.md) |
| Art | `media/brand/`, icons and illustrations in `app/Resources/` | Codex | the issue of the task |
| UI and design system | `app/Source/ui/`, `app/Resources/themes/`, `docs/design/` | Claude (the design thread) | [docs/design/](docs/design/) |
| Engine, model, project files, recording, warp, export | `app/Source/engine/`, `model/`, `project/`, `browser/`, `core/` outside `fx/`, `tests/` | Claude (the stage thread) | the stage file in `docs/spec/` |
| Synths | `core/src/Prisma*`, `core/src/Wavetable*`, `plugins/prisma/`, later Alloy | Claude (the synth stage) | `docs/spec/09`, `10` |

Shared files (any area may touch them, carefully): the root and `core/` `CMakeLists.txt`, `.github/workflows/`, `app/Source/Strings.h`, `docs/HANDOFF.md`, `README.md`, `AGENTS.md`, `CLAUDE.md`. In `HANDOFF.md` edit only the section of your area.

Effects never draw their own UI: the app shows every effect through its parameters, and the UI area decides how. An effect that needs a picture (a curve, a meter) exposes the data through `readout()` and `response()` and describes the picture in its PR.

## Branches and pull requests

- Branch from a fresh `main`. Claude's branches are `claude/...`; Codex's are `codex/<area>-<topic>` (for example `codex/fx-transient`). Never push to someone else's branch or to `main`.
- One PR per finished piece (one effect, or a small batch of related ones). Description: "Before / After / How", in English.
- A PR merges only with green CI (macOS and Linux). Codex's PRs are reviewed by Claude, who merges them when green and the review is clean; Codex does not merge. Claude merges its own green PRs (the owner authorised it).
- Commits are small, messages in English in the imperative mood ("Add transient shaper"). Never rewrite history on a branch someone else has checked out.
- Work locally first: build, test and commit on your machine; push a finished piece as one PR, so CI runs once per piece. On the owner's Mac, hand tests use a DMG built locally (`./scripts/build-mac.sh`, `./scripts/package-mac.sh`).
- The owner installs a build only when told "можно ставить" (ready to install), not after every merge.

## Build and test

```bash
# Linux (CI and containers)
sudo apt-get update && sudo apt-get install -y libasound2-dev libxrandr-dev libxinerama-dev libxcursor-dev \
     libfontconfig1-dev libgl1-mesa-dev libxcomposite-dev xvfb xdotool imagemagick
cmake -S . -B ../build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build ../build && ctest --test-dir ../build --output-on-failure

# macOS
./scripts/build-mac.sh
```

- Warnings are errors. Build on Linux too before pushing work made on a Mac: GCC catches what Clang lets through.
- Every task ships with tests; all existing tests stay green; a test is never disabled, weakened or skipped to get green.
- One effect area check: `ctest --test-dir ../build -R daw_fx_tests --output-on-failure`.
- UI screenshots: `Xvfb :99 -screen 0 1600x1000x24 &`, then `HOME=<empty folder> DAW_NULL_AUDIO=1 DAW_THEME=prism|neon|studio|paper DAW_LANG=en|ru ../build/app/DAW_artefacts/Release/DAW`; the window is named after the project ("Untitled"), resize it with `xdotool windowsize`, capture with `import -window root`. Stop it with `pkill -x DAW`.
- CI builds macOS and Linux, runs the tests and `auval`, and publishes a nightly DMG from `main`.

## Pitfalls we already hit

- **Audio thread:** no allocation, no locks, no `juce::String`, no `ValueTree`, no files or logging. Debug has `DAW_RT_CHECKS`; the "no heap use" engine test and the effect contract tests must stay green.
- **Keys are read by key code, not by character.** The owner types with a Russian layout: `getTextCharacter()` returns Cyrillic while `getKeyCode()` stays Latin. Every shortcut compares the key code (see `keyLetter` in `app/Source/ui/ComputerKeys.cpp`).
- **Colors only from the theme.** The UI takes colors through `theme()` (JSON tokens in `app/Resources/themes`) and, once the UI kit lands, sizes from the theme's `metrics`, so a screen looks right in every theme. No hard-coded colors. The one exception for now is the Prisma window, which moves to themes in the synth stage.
- **Every UI string has both languages:** `tr ("English", "Русский")` in `app/Source/Strings.h` (effect names in `app/Source/FxStrings.h`). When word order or plurals differ, branch on `isRussian()`. The engine tests run in both languages (`daw_engine_tests`, `daw_engine_tests_ru`), so a test that checks UI text compares against `Strings::...`, never a bare literal.
- **One action = one undo transaction**; a mouse gesture is one transaction too.
- **Engine tests without a real audio device:** tests create `AudioEngine` with `openDevices = false`; a real CoreAudio callback on macOS CI raced the hand-driven one.
- **Saved ids are forever.** Effect type ids, parameter ids and preset indices are stored in projects: never rename or reorder them; add new ones at the end. A project format change needs a schema migration.
- **No new third-party libraries** without the owner's permission; each license must be compatible with AGPLv3.

## Design work

- Styling and features are separate pieces of work: a restyle PR does not add features, a feature PR uses the existing components.
- A screen is done when the owner has approved a side-by-side screenshot of it next to its design sheet.
- Screenshots in one theme, unless a change touches colors or layout tokens.

## When you stop

Update your area's section of `docs/HANDOFF.md`, even mid-task: what is done, what is not, the next step, the PR number. The state lives in the repository, not in a chat.
