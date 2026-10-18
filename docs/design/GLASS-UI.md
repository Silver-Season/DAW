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

`app/Source/ui/GlassMaterial.*` provides the shared surface, accent gradient, knob and control lens. It is used through `UiCommon`, the app look and feel and existing controls. The top bar has a navigation row and a transport row; its existing recorder, punch, loop, follow, capture and spectrum behaviour is preserved. Export opens the current real export dialog.

The native lens refracts an analytic copy of the control's own track. It uses a rounded shape, edge displacement and a directional specular rim. A bounded cache holds up to 96 small images (160 × 160 pixels maximum), generated on the UI thread. It never captures a parent component during paint and never touches the audio callback.

**This is not a window-wide live backdrop shader.** The approved HTML optics study uses WebGL and samples its canvas backdrop. Native JUCE controls currently use the smaller local-track model; pointer-following highlights and full-background refraction are not implemented. The material is a shared native rendering layer, not a screenshot or a browser embedded in the audio application.

## Identity

The latest main's brand implementation is retained: `ui::drawLogo` draws the faceted prism, input beam and six parallel spectrum tracks in theme colours. `media/brand/` contains the source variants; `app/Resources/brand/AppIcon-1024.png` remains the app bundle icon. No second competing logo renderer or app icon is substituted.

## Verification

The engine/UI tests cover preference round-trip and settings-file reopening, invalid colours and parameter limits, accent contrast, semantic colours, reduced transparency, theme reload and native logo rendering in both glass themes. Existing tests remain enabled in both UI languages.

Native component captures can be generated without opening an audio device or loading a user's project:

```sh
DAW_LANG=en DAW_UI_SNAPSHOTS=/private/tmp/prisma-ui-shots /path/to/DAW.app/Contents/MacOS/DAW
```

Use an absolute output path. This creates a demo in memory, its own settings/library inside the output folder, and captures Session, MIDI editor and Arrangement in all six themes, plus Appearance and the built-in synth in both glass themes. It exits automatically. These captures are actual JUCE component rendering; they exclude the macOS title bar.

Build outside the repository. `PRISMA_COPY_AFTER_BUILD` defaults to OFF, so a verification build does not install the synth into the user's plugin folders. Release packaging can enable the option explicitly.

## Checkpoint, 2026-10-19

Paused at the owner's request to conserve usage. The merged branch includes GitHub main `be186ab` (PR #25 branding). All 10 CTest suites passed after the merge; the macOS arm64 Release app built successfully. See `media/ui-glass/index.html` for 22 final native captures. Audio/Warp, drums, export and additional interaction states still need a visual pass; Appearance/synth have captures only in the two glass themes. Full backdrop refraction and pointer-following highlights remain unimplemented. Existing SDK/upstream warnings remain; universal builds, CI and live hardware audio were not verified in this pass. No push or PR was made.
