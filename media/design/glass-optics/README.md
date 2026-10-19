# Prisma optics study

Open `index.html` in a current browser. Everything is local; no network is needed.

This is the reference for the glass look of the app: a lens that refracts a spectrum bar, a glass capsule transport, light and dark modes.

## What to try

- Drag the large lens over the spectrum, the ticks and the wave. Arrow keys move it; Home centres it.
- Move the mouse around the lens: the highlight follows with a little lag.
- Compare "refraction" and "blur only" with the lens in the same place.
- Switch light/dark, change depth, highlight and accent.
- Drag the Wet/dry slider: its clear thumb refracts the coloured track.
- "Motion" moves the lens slowly on its own; dragging stops it.

## How it works

A WebGL 1 fragment shader. The backdrop is drawn into a separate 2D canvas and used as a texture. The lens shape is a signed-distance rounded rectangle; its local normal shifts the backdrop lookup, with magnification, slight channel dispersion at the rim, directional highlights and a soft shadow. Text and controls stay sharp on a separate foreground layer. It is an approximation, not ray tracing.

## In the app

The native app follows the same model on the CPU: the top-bar spectrum is drawn into an image and a draggable lens refracts it (magnified middle, bent rim, red/blue parting at the edge, rim light). Faders and sliders use smaller lenses that refract an analytic copy of their own track. See [docs/design/GLASS-UI.md](../../../docs/design/GLASS-UI.md).

## Logo and fonts

`prisma.png` is an earlier icon study; the app's own icon is `app/Resources/brand/AppIcon-1024.png` (sources in `media/brand/`). Onest Regular/SemiBold come from the app's resources; the SIL OFL licence is in `OFL-Onest.txt`.
