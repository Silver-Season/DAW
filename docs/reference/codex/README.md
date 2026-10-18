# Reference: Codex prototypes

This folder holds files from two standalone plugins that Codex built in the local branch `codex/vocal-plugin` (17.10.2026): the **Prisma Touch** drum pad and the **Prisma Voice** vocal processor. They are not part of the build. This is material for the implementers of stages 08 and 12: we take the ideas and rewrite the code to follow [00-rules.md](../../spec/00-rules.md) (DSP in `core/` without JUCE, colors via `theme()`, strings in `Strings.h`, connection to the project model).

| File | What to take | Where it is useful |
|---|---|---|
| `touchdrums/TouchSurface_mac.mm`, `TouchSurface.h` | Playing on the trackpad via the public `NSTouch`: a transparent `NSView` over the pad grid, `allowedTouchTypes = Indirect`, stable touch tokens, one touch = one hit, cancellation when the window loses focus, swallowing the mouseDown from tap-to-click | 08, task 8.16 |
| `touchdrums/Processor.cpp`, the `buildKit` function | Recipes for 8 synthesized hits (kick with a falling pitch, snare, hats on 4 square oscillators, clap with three bursts, tom, rim, perc with FM) | 08, task 8.8 (`tools/drumgen`) as starting recipes |
| `voicer/Pitch.h` | A YIN-style pitch detector and snapping to a scale. Fine as a base, but needs a filter before decimation, note hysteresis and the project tuning | 12, tasks 12.1–12.2 |
| `voicer/Engine.cpp` | Chain order, aligning the dry signal for latency, the Retune / Natural drift / Formant parameters | 12, only as an example; the live shift is done with PSOLA |
| `voicer/Phrase.cpp` | Phrase model: an immutable source + a vector of notes with target pitch and an edited flag; undo copies only the notes | 12, task 12.5 (graphical mode) |
| `*/README-codex.md` | Codex's user descriptions: scenarios, list of settings | 08, 12 |

Why we do not take them as is (review of 17.10.2026): both plugins live outside the DAW, colors are hard-coded, and there is no `core/` layer. Prisma Voice has 40 ms of latency because of a spectral shifter (live singing needs under 10 ms), has no note hold (the target jumps between adjacent notes), drops the correction to zero and back when the detector loses confidence, hard-codes the tuning at A = 440, has four built-in scales, and stores up to 30 s of raw audio in the plugin state.

License: the code was written for this project and is distributed under AGPLv3, like the whole repository.
