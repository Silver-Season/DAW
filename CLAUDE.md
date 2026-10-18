# CLAUDE.md

Notes for an AI assistant (or a person) picking up work in this repository from scratch.

## What this is

An independent, open-source digital audio workstation for macOS with its own synthesizers (Prisma, later Alloy): a Session grid for launching clips across tracks and scenes, a linear Arrangement with takes and punch-in, MIDI and audio recording, built-in effects, a drum machine and a mixer. C++20 + JUCE 8, AGPLv3. The project owner is Silver-Season.

Languages: the owner writes in Russian, so conversations with the owner are in Russian. Everything in the repository is English: code, comments, commit messages, README and docs. The UI is English by default with Russian as a second language (Settings → Appearance → Language). Describe the app by what it does, never by comparison with other products.

## Read first

1. [docs/spec/00-rules.md](docs/spec/00-rules.md): build, real-time rules, undo, style, tests, screenshots. Mandatory.
2. [docs/HANDOFF.md](docs/HANDOFF.md): what is in progress right now.
3. [docs/spec/README.md](docs/spec/README.md): the list of stages and their dependencies, then the file of the current stage.
4. [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): layers and roadmap.
5. [README.md](README.md): what the app already does, stage by stage, with screenshots.

## Where we are

- Done: stage 1 (project model, engine, window, browser, themes, saving, versions, audio settings), stage 2A (piano roll, scales, quantize, A4 tuning), stage 2B (metronome, count-in, MIDI recording into slots, overdub, always-on capture, controllers), stage 3 (Arrangement: clips and markers, song clock with loop and relocation, chasing notes, Session versus Arrangement, timeline editing, recording to the timeline with takes and punch, Session recorded into the Arrangement, taking a track from a version), stage 5 (audio tracks, playback, audio recording, latency compensation), stage 6 (13 built-in effects, sends and returns, sidechain, drum machine, Inspiration generators, automation), the core of stage 4 (offline export to WAV/AIFF/FLAC), English UI with Russian as a second language.
- The current state and the next step are always in [docs/HANDOFF.md](docs/HANDOFF.md).
- Next, in order: warp (stage 7, in progress on a branch), the rest of stages 4–6, then the synths (09 Prisma to release level, 10 Alloy) and the library (08). Ideas agreed with the owner for later: stem separation (Demucs/Open-Unmix via CoreML); a generator plugin (presets by sound type and scale-aware MIDI, after the synths); stage 11 DJ (2–4 decks, Pioneer DDJ-FLX6, sync with the transport, Link/MIDI Clock for Maschine); 12 vocal tools (draft in docs/spec/12-vocal.md); 13 mastering module; 14 an AI assistant through the Anthropic API with the user's own key. Later: GPU (Metal) rendering of the UI for large projects.

## Building and checking in a Linux container

```bash
sudo apt-get update && sudo apt-get install -y libasound2-dev libxrandr-dev libxinerama-dev libxcursor-dev \
     libfontconfig1-dev libgl1-mesa-dev libxcomposite-dev xvfb xdotool imagemagick
cmake -S . -B ../build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build ../build && ctest --test-dir ../build --output-on-failure
```

UI screenshots: `Xvfb :99 -screen 0 1600x1000x24 &`, then `HOME=<empty folder> DAW_NULL_AUDIO=1 DAW_THEME=prism|neon|studio|paper DAW_LANG=en|ru ../build/app/DAW_artefacts/Release/DAW`. The window is named after the project ("Untitled"), resize it with `xdotool windowsize`, capture with `import -window root`. On a Mac, `./scripts/build-mac.sh` builds; CI builds macOS and Linux and publishes a nightly DMG from `main`.

## Pitfalls we already hit

- **Keys are read by key code, not by character.** The owner types with a Russian layout: `getTextCharacter()` returns Cyrillic while `getKeyCode()` stays Latin. Every new shortcut compares the key code (see `keyLetter` in `plugins/prisma/Source/PrismaEditor.cpp` and `app/Source/ui/ComputerKeys.cpp`).
- **Engine tests without a real audio device:** tests create `AudioEngine` with `openDevices = false`. On macOS CI a real CoreAudio callback raced the hand-driven one and broke the tests.
- **Colors only from the theme.** The whole UI takes colors through `theme()` (JSON tokens in `app/Resources/themes`), so a new screen looks right in all four themes. No hard-coded colors. The one exception for now is the Prisma window, which moves to themes in the synth stage.
- **Every UI string lives in `app/Source/Strings.h`** as `tr ("English", "Русский")`. A new string needs both languages; when word order or plurals differ, branch on `isRussian()`. The engine tests run in both languages (`daw_engine_tests` and `daw_engine_tests_ru`), so a test that checks UI text compares against `Strings::...` or `Strings::tr (...)`, never a bare literal.
- **Audio thread:** no allocation, no locks, no `juce::String`, no `ValueTree`. Debug has `DAW_RT_CHECKS`, and the "no heap use" test must stay green.
- **One action = one undo transaction**; a mouse gesture is one transaction too.

## How we work

- One branch per stage, PR into `main`, merge only with green CI. PR description: "Before / After / How".
- Every stage has tests for each task and screenshots of new UI (`media/`), in one theme unless colors or layout tokens change; README.md gets a section for the stage.
- The owner tests live on a Mac (sound, external audio interface, MIDI keyboard) after each stage and sends feedback in one message. The owner installs the DMG when told "можно ставить" (ready to install), not after every merge.
- Codex is used only for isolated tasks (icons, illustrations), not for the engine or the UI.
