# Configurable glass appearance

The design integration is based on GitHub `main` at `be186abcfe834fd22ab73c11200dad6d8ca5f070` (2026-10-19). It retains the current Arrangement, recording, audio/warp editor, effects, drums, automation and export implementations. No audio DSP or project schema changes are part of this work.

## Controls

Open **Appearance** in the top bar, or Settings → Appearance. The original Prism, Neon, Studio and Paper themes remain available, alongside Glass · Light and Glass · Dark. Saved theme choices are retained; a new settings file defaults to Glass · Dark.

- Custom accent: two HEX colours and eight quick swatches. Press Enter or leave the colour field to apply a valid six-digit colour. Invalid values get a red outline.
- Gradient: enable/disable and choose its angle. Endpoints may be toned toward the primary colour to keep button labels readable.
- Glow: controls the existing playback highlights.
- Glass depth: adjusts the material and local lens strength.
- Reduce transparency: uses opaque controls and disables lenses/glow.
- UI scale, animations and language remain available. Language changes apply after a restart.

The appearance is stored in application settings, outside the musical project. Recording/play colours and track palette indices retain their meaning. Text accents are adjusted for at least 4.5:1 contrast on the chrome and panel surfaces. Saturated fill colours remain separate from text colours.

## Rendering

`app/Source/ui/GlassMaterial.*` is the shared material, used through `UiCommon`, the app look and feel and the existing controls:

- `cell`: flat fill with a soft top light and no rim, for clip slots, fields and meters (`fillCell` in glass themes). A grid of them stays calm; outlines come only from `strokeCell` where a view asks for one.
- `surface`: frosted body with a bright upper rim, for buttons and panels.
- `capsule`: the transport row of the top bar, with a soft drop shadow.
- `accent`: lit buttons and track headers, the accent gradient when enabled; the label colour is chosen for 4.5:1 contrast.
- `thumb`: the knob of a switch.
- `lens`: fader and slider thumbs. It refracts an analytic copy of the control's own track; a bounded cache (96 images, 160 × 160 px at most) lives on the UI thread.

**The spectrum lens** (`Spectrum::paintLens`) is the native counterpart of the HTML optics study: the spectrum, with a faint band of the palette under the curve, is drawn into an image at the screen's pixel density, and every pixel of the capsule-shaped lens samples that image through the lens: magnified by 0.72 in the middle, pulled outward along the rim normal, red and blue sampled a little apart at the edge, then a rim light and an upper glint. It is draggable along the spectrum; a double-click returns it. It costs one small image per frame (about 90 × 50 px at 2×) and only repaints when the spectrum does or the lens moves. Nothing here touches the audio callback.

Note for new drawing code: `juce::Graphics::drawImage` uses the context's current opacity, so after `setColour` with a translucent colour (a shadow) call `setOpacity (1.0f)` first; the glass lenses were drawn at 10 % opacity before this was fixed.

Not implemented: window-wide backdrop refraction (panels refracting what lies behind them) and pointer-following glints.

## Identity

The latest main's brand implementation is retained: `ui::drawLogo` draws the faceted prism, input beam and six parallel spectrum tracks in theme colours. `media/brand/` contains the source variants; `app/Resources/brand/AppIcon-1024.png` remains the app bundle icon. No second competing logo renderer or app icon is substituted.

## Verification

Engine/UI tests cover preference round-trip and settings reopening, invalid colours and limits, accent contrast, semantic colours, reduced transparency, theme reload, the logo in both glass themes, the two-row top bar fitting at 1210 px in the glass themes, and the spectrum lens (inside the spectrum, drag, double-click, info-bar help, drawn over the spectrum; absent in the other themes). Both UI languages run.

Native captures without an audio device or the user's project:

```sh
DAW_LANG=en DAW_UI_SNAPSHOTS=/absolute/output DAW_UI_SNAPSHOT_SCALE=2 /path/to/DAW.app/Contents/MacOS/DAW
```

This builds a demo in memory with its own settings and library inside the output folder, captures Session, MIDI editor and Arrangement in all six themes plus Appearance and the built-in synth in both glass themes, and quits. `DAW_UI_SNAPSHOT_SCALE` (1–3, default 1) renders at Retina density for close inspection; the committed captures in `media/ui-glass/` are at 1×. A snapshot run may start next to a running copy of the app.

Build outside the repository. `PRISMA_COPY_AFTER_BUILD` defaults to OFF so a verification build does not install the synth into the user's plugin folders; `scripts/build-mac.sh` turns it on.
