# The effects area

Guide for the owner of the effects area (Codex). Read [AGENTS.md](../../AGENTS.md) and [00-rules.md](../spec/00-rules.md) first; the task list is [EFFECTS-BACKLOG.md](EFFECTS-BACKLOG.md).

## What you own

| Path | What it is |
|---|---|
| `core/src/fx/*.cpp`, `core/src/fx/*.h` | One file per effect, the shared base `BasicEffect.cpp`, the registry `Registry.cpp`, private building blocks (`DelayLine.h`, `Dynamics.h`, `ModDelay.h`, `Oversampler.h`) |
| `core/include/daw/fx/BasicEffect.h`, `Helpers.h` | The common half of every effect, smoothers, biquads |
| `core/tests/fx/` | `daw_fx_tests`: one file per family of effects, `ContractTests.cpp` and `GenericTests.cpp` for all of them |
| `app/Source/FxStrings.h` | Russian names of effects, parameters, choices and categories |
| `plugins/fx/` | Later: AU/VST3 builds of the effects (backlog item FX-9) |

`core/include/daw/fx/Effect.h` is the contract between your area and the app. You may add to it (a new optional virtual with a default), but a change to an existing signature needs an issue and Claude's agreement first, because the engine, the mixer and the automation depend on it.

You do not need to touch anything else. The app finds a new effect by itself: the browser lists every entry of `effectTypes()`, `BuiltInEffectProcessor` turns its parameters into automatable host parameters, the device panel shows its key parameters as knobs, projects save the values by parameter id, and the sidechain picker appears when `acceptsSidechain()` is true.

## Adding an effect, step by step

1. **Pick the task** from [EFFECTS-BACKLOG.md](EFFECTS-BACKLOG.md), top first, and branch `codex/fx-<id>` from a fresh `main`.
2. **Write `core/src/fx/<Name>.cpp`.** Copy the shape of `Crush.cpp` (the shortest one): a `final` class derived from `BasicEffect` in an anonymous namespace, the parameter list in the constructor, `type()` returning the id, `processWet()` doing the work in place, and one free function at the end, `std::unique_ptr<Effect> make<Name>()`. CMake picks the file up by itself (a glob over `src/fx/*.cpp`).
   - `BasicEffect` appends Mix (0–100 %) and Output (±24 dB) and keeps the dry path aligned with your latency. Pass `withMix = false` only for an effect where a dry blend makes no sense (a utility).
   - Allocate only in `prepareWet()`. Recompute coefficients in `paramChanged()` (it runs on the audio thread too, so no allocation there either).
   - Read parameters with `value (index)`; non-stepped values arrive already clamped, and `BasicEffect` smooths Mix and Output. Smooth your own continuous parameters with `Smoother` (`Helpers.h`) so that a moving knob never clicks.
   - Report `latencySamples()`, `maxLatencySamples()` and `tailSeconds()` truthfully: the null test, export tails and, later, delay compensation rely on them.
   - Tempo-synced effects read `setTransport()` (tempo, song position in quarter notes, playing).
   - Meters and curves for the device: `readout (index)` (document the indices in the class comment) and `response()` for a magnitude curve. Write readouts with relaxed atomics.
3. **Register it** in `core/src/fx/Registry.cpp`: declare `make<Name>()`, add one line to `effectTypes()` (id, English name, category, key parameters) and one branch to `createEffect()`. The order of `effectTypes()` is the order of the browser; append within the category.
   - **Key parameters**: up to eight parameter ids, most important first. They are the knobs of the compact device; pick what a musician reaches for, not the parameter list in order.
   - **Categories**: `eq`, `dynamics`, `space`, `colour`, `filter`, `modulation`. A new one (for example `utility`, `spectral`) needs its English and Russian name in `categoryName()` in `FxStrings.h`.
4. **Name it in Russian** in `app/Source/FxStrings.h`: the effect in both maps of `effectName()`, every new parameter id in `paramName()`, every new choice in `choice()`. Reuse an existing parameter id (`freq`, `attack`, `mix`, `width`, ...) when it means the same thing; it is then already translated.
5. **Test it** in `core/tests/fx/<Family>Tests.cpp` with `FX_TEST ("<id>: what it checks")` (helpers in `FxTest.h`: `sine`, `noise`, `impulse`, `run`, `rms`, `peak`, `toneAmplitude`). The backlog task lists the numbers to check. Every check is numeric: levels in dB, frequencies, sample positions. Include a CPU check (10 s of stereo 48 kHz noise under 1 % of real time, like the other families).
6. **Run** `cmake --build ../build && ctest --test-dir ../build --output-on-failure`. The contract tests run on your effect automatically, see below.
7. **Open the PR**: "Before / After / How", what the effect does in a musician's words, the numbers your tests measure, and a short description of any picture the device should show (curve, meter) for the UI area. Update the effects section of `docs/HANDOFF.md`.

## What the contract tests check on every effect

`ContractTests.cpp` and `GenericTests.cpp` run on every entry of the registry, so they test a new effect before it has a test of its own:

- the id is unique, `createEffect` makes it, `type()` returns the id, key parameters exist (at most eight);
- after `prepare()`, `process`, `setParam`, `setTransport`, `readout`, `latencySamples` and `tailSeconds` do not allocate (the test program counts `operator new`);
- every parameter at its minimum and at its maximum gives finite output below 64 (a -6 dBFS input);
- random settings at 44.1, 48 and 96 kHz, mono and stereo, blocks of 1 to 512 samples stay finite;
- Mix 0 nulls against the input delayed by the latency (below -120 dB), and an impulse through Mix 0 comes out exactly `latencySamples()` later;
- silence stays silent after the tail, `reset()` clears it, output stays within ±4 on -6 dBFS noise at default settings;
- defaults are inside the range, ids are unique, values survive the normalised round trip;
- in the app tests (`daw_engine_tests_ru`): the effect and its category have Russian names.

## Rules that the tests cannot see

- The audio thread never locks, waits, logs, touches files or calls anything that may allocate (`std::function` with captures, `std::string`, containers growing past their reserve).
- No unbounded loops per sample; no denormals (pass feedback state through `flushTiny` from `DelayLine.h`).
- Parameter ids, effect ids and the order of choices are saved in projects. Never rename, remove or reorder them after a merge; add new parameters at the end of the list.
- DSP is plain C++20 in `core/`, no JUCE, no new libraries without the owner's permission.
- No UI code. If an effect cannot be used without a special control, describe it in the PR; the UI area builds it.
- Names describe what the effect does; never name other products, in code, comments or UI.
