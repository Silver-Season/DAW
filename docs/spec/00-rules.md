# 00. General rules for all stages

Read this file before starting any stage. These rules take priority over convenience: if a stage task would require breaking one, stop and describe the problem in your report.

## 1. Repository and build

- Code: C++20, JUCE 8.0.4 (pulled in via FetchContent), CMake + Ninja. macOS 11+, universal binary arm64 + x86_64.
- Layers (details in [ARCHITECTURE.md](../ARCHITECTURE.md)):
  - `core/`: pure C++20 **without JUCE**: DSP, synths, transport, clips, time model, algorithms (quantize, LUFS loudness, onset detector, etc.). Everything that can be tested without a window and without an audio interface lives here.
  - `app/`: the JUCE application: engine, plugin hosting, UI, project files.
  - `plugins/`: built-in instruments and effects as separate AU/VST3 plugins.
  - `tests/`: JUCE layer tests (`daw_engine_tests`); `core/tests/`: core tests (`daw_core_tests`).
- Building on Linux (for checks in the container):
  ```bash
  sudo apt-get install -y libasound2-dev libxrandr-dev libxinerama-dev libxcursor-dev \
       libfontconfig1-dev libgl1-mesa-dev libxcomposite-dev xvfb xdotool imagemagick
  cmake -S . -B ../build -G Ninja -DCMAKE_BUILD_TYPE=Release
  cmake --build ../build
  ctest --test-dir ../build --output-on-failure
  ```
  Keep the build folder outside the repository. If JUCE is already downloaded, pass `-DFETCHCONTENT_SOURCE_DIR_JUCE=<path>`.
- Building on macOS: `./scripts/build-mac.sh`. CI (`.github/workflows/build.yml`) builds macOS and Linux, runs the tests and `auval`, and publishes a nightly DMG. A push to `main` without green CI counts as unfinished work.
- Compiler warnings are errors. New code builds without a single warning under the project's flags.

## 2. Real time (the most important part)

The audio thread (`audioDeviceIOCallbackWithContext` and everything called from it) **never**:
- allocates or frees memory (`new`, `delete`, `std::vector::push_back` beyond `reserve`, `std::string`, `juce::String`, `std::function` with captures, `juce::var`);
- takes mutexes or a `CriticalSection`, or waits on other threads;
- reads or writes files, logs, or calls `DBG`;
- touches `juce::ValueTree`, `juce::Component`, or anything belonging to the message thread.

How to pass data:
- **From the UI to audio:** immutable snapshots (`std::shared_ptr<const X>`) that the message thread builds and publishes with an atomic pointer swap. The old snapshot is deleted on the message thread, not on the audio thread (see 01, task 1.3 "Playback graph").
- **Small values** (volume, pan, mute): `std::atomic<float>` / `std::atomic<bool>`.
- **Commands** (launch a clip, start recording): a fixed-capacity lock-free SPSC queue (`juce::AbstractFifo` over an array of POD structs).
- **From audio to the UI** (levels, position, recorded events): atomics and SPSC queues that the UI reads on a timer (30–60 Hz).

Check: in a Debug build, enable `DAW_RT_CHECKS=ON` (added in 01, task 1.3), which replaces `operator new` and fires `jassertfalse` if an allocation happens on the audio thread during an engine test.

## 3. Project model and undo

- Everything saved in the project is stored in a `juce::ValueTree` (the schema is described in 01, task 1.1). The UI reads and writes only the model, never the engine directly.
- Each user action is one `UndoManager` transaction: call `undoManager.beginNewTransaction ("Action name")` before the change. Dragging a note with the mouse is one transaction for the whole gesture, not one per pixel.
- The engine listens to the model (`ValueTree::Listener`) and rebuilds the playback graph. The model knows nothing about the engine.

## 4. Code

- Style matches the existing files: JUCE style, 4 spaces, braces on a new line, `camelCase` for methods and variables, `PascalCase` for classes, `kConstant` for constants. A space before call parentheses (`foo (x)`), as in JUCE.
- Comments explain "why", they do not retell the code. Comments and identifiers in code are in English.
- All UI strings live in `app/Source/Strings.h` as `tr ("English", "Русский")` pairs. English is the default UI language; Russian is selectable in Settings → Appearance → Language (applies after restart). Every new string needs both.
- One class, one `.h/.cpp` pair, if the class is larger than ~150 lines. UI files go in `app/Source/ui/`, engine files in `app/Source/engine/`, model files in `app/Source/model/`.
- No third-party libraries beyond JUCE without explicit permission in the stage spec. Allowed libraries and their licenses are listed in the stages (LAME in 04, Rubber Band in 07). Each license must be compatible with AGPLv3.
- Colors, fonts, and padding sizes come only from the theme (`Theme`, created in 01). Literal colors in `paint()` are forbidden.

## 5. Tests

- Each stage task adds tests. The core is tested in `core/tests/`, the engine and model in `tests/` (virtual audio device `FakeDevice`, as in `tests/EngineTests.cpp`).
- Tests are deterministic: no real time, no sleeping, and no randomness without a fixed seed.
- Audio is checked numerically: peak, RMS, absence of NaN and Inf, position of the first non-zero sample, frequency via FFT. "Sounds right" is not accepted.
- All existing tests stay green. A test may not be disabled, weakened, or marked as skipped to pass a stage.

## 6. UI checks

- The UI is checked with screenshots under Xvfb:
  ```bash
  Xvfb :99 -screen 0 1600x1000x24 & export DISPLAY=:99
  ../build/app/DAW_artefacts/Release/DAW &   # path depends on the generator
  xdotool search --name DAW | head -1        # window id
  import -window root /tmp/shot.png
  ```
  Clicks and keys go through `xdotool mousemove x y click 1`, `xdotool key Tab`. Stop the process with `pkill -x DAW` (not `pkill -f`, which also kills your shell).
- For every screen a stage changes, the report includes screenshots in both themes, and the final ones go into `media/` and are updated in the README.
- Compare against the concept [`docs/design/concept.html`](../design/concept.html): colors, padding, element order. A pixel-exact match is not required; the same logic and the same impression are.

## 7. Performance

- Running the test project (16 Prisma tracks with 8 voices each, 44.1 kHz, buffer 256) must not take more than 50% of the buffer time on an M1. In the container, measure relative to phase 0 with the `tools/prisma_render` tool and report the numbers.
- The UI repaints only what changed (`repaint (area)`), animation timers run at no more than 60 Hz, and unused panels do not tick.

## 8. Commits and report

- One branch per stage: `stage-N-<short-name>`. Commits are small and meaningful, messages in English in the imperative mood ("Add piano roll velocity lane").
- The README is updated in the same PR: what is new, new keyboard shortcuts, new screenshots.
- At the end of a stage, report against the "Done when" section: each line marked "verified" with how (test, screenshot, command), or "not done" with why. Never write "works" if it was not verified.

## 9. What not to do

- Do not start tasks from later stages, even if they are "on the way". Groundwork for the future only where the stage spec explicitly asks for it (for example, the track `type` field in 01).
- Do not change the project file format without a migration: the file has a schema version number, and the loader must open all previous versions (01, task 1.6).
- Do not touch Prisma DSP and presets unless the stage requires it.
