# Demo song: "Mountain King"

An electronic arrangement of Edvard Grieg's "In the Hall of the Mountain King" (Peer Gynt, 1875, public domain), made as an end-to-end check of the app: one whole track built only with what the DAW has, then exported.

- Project: [Mountain King.prisma](Mountain%20King.prisma) (open it with File → Open).
- Listen: [media/demo-mountain-king.mp3](../../media/demo-mountain-king.mp3) (the export is a 24-bit WAV; this MP3 preview was made before the app could export MP3).
- Rebuild it: `daw_demo_song <folder> [--stems]` (built with the tools, source in [tools/demo_song/MountainKing.cpp](../../tools/demo_song/MountainKing.cpp)). It builds the project through the same model calls the UI makes, saves it, loads it back and renders it with `OfflineRenderer`, the engine of File → Export. `--stems` also renders every track soloed.

![The song in the Arrangement](../../media/demo-mountain-king.png)

## What is in it

B minor, 136 BPM, 60 bars (about 1:48 with the reverb tail). Six scenes (Intro, Build, Drop, Break, Drop 2, Outro) laid out on the Arrangement with markers.

| Track | Sound | Processing |
|---|---|---|
| Kick | drum machine, "Techno" kit | the key of two sidechain compressors |
| Drums | drum machine, "909 Club": hats, claps, open hats, crashes, snare rolls | EQ low cut at 180 Hz, send A |
| Bass | Prisma "Reese Bass", filter and envelope retuned; off-beat eighths on the chord roots | compressor keyed by the kick |
| Pluck | Prisma "Pluck"; the theme an octave down | filter cutoff automated (opens over the intro, closes into the break), sends A and B |
| Lead | Prisma "Supersaw Lead"; the theme, in octaves in Drop 2 | saturator (tape), sends A and B |
| Pad | Prisma "Glass Pad", slower attack; chords per half bar | compressor keyed by the kick, chorus, send A, volume fade in the outro |
| Riser | Prisma "Init" turned into a noise riser (oscillator off, noise, band-pass) | filter cutoff automated before each drop, send A |
| A Hall | return | reverb, 4.5 s |
| B Echo | return | ping-pong delay, 1/8. and 1/16. |
| Master | | glue compressor, limiter at -0.5 dBFS |

## What the check found

Things that were missing or awkward while making the track (none of them stopped it):

1. **No effects on the master in the UI.** The engine plays a master chain (`MASTER/DEVICES`) and the project file keeps it, but nothing in the app shows it or lets the user add a device there. The compressor and limiter of this song were put there through the model.
2. **No tempo changes.** The original piece is one long accelerando; the project has one tempo, with no tempo automation in the Arrangement.
3. **Drum kits are about 20 dB louder than Prisma presets.** With every fader at 0 dB the kick alone hit the limiter while the synths sat around -35 dB RMS; the mix needed the kick at -12 dB and the synths at 0. A common loudness for factory sounds (or a lower default for drum kits) would save every user this step.
4. **Seven Prisma factory presets.** Init, Supersaw Lead, Reese Bass, Wobble Bass, Glass Pad, Pluck, Vox Sweep. No keys, bells, strings, brass or FX sounds; the riser had to be built from Init by hand.
5. **Automation lanes open on Volume.** A track whose only envelope is on a device parameter (the Pluck cutoff) shows an empty Volume lane in automation mode; the envelope is found only through a flat menu of all 100+ Prisma parameters (it is marked with a dot there).
6. **No master automation**, so a filter sweep or fade over the whole mix has to be drawn on every track.
7. **No stem export.** Checking the balance meant rendering each track soloed (the `--stems` switch of the tool).
8. **No way to open a project from outside the app**: no command-line argument, no `.prisma` document type for the Finder; only File → Open.
9. **Return tracks have clip slots** in the Session View, though they can never hold clips.

All nine were fixed afterwards: the master is a track in the detail panel and in automation mode (1, 6), the master has a Tempo envelope (2), drum kits came down 10 dB (3), nine more presets (4), automation opens the drawn envelope (5), File → Export writes stems, MP3 and loudness-normalised files (7), `.prisma` files open from the Finder and the command line (8), return tracks have no slots (9). Time signature changes are still to come.

What worked without trouble: building scenes in the Session View and laying them on the Arrangement with markers, Prisma presets with knobs turned, both drum kits, sidechain from another track, sends to two returns, device-parameter and volume automation with curves, the saved project reopening in the app as it was, and the export (no NaN, the theme comes out on the right notes when checked by pitch detection).
