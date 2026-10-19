# UI kit: one place for every control

Goal (owner, 2026-10-19): lay the interface down once, so that a change of style is a change in one place, not in a thousand. The reference is the ten design sheets of the glass system (local, `media/design/prisma-glass-system/`, not committed: they name other products); sheet 10 lists the components and their states.

## Layers

1. **Tokens** (`Theme`, JSON in `app/Resources/themes`): colours, plus `metrics` — spacing, control heights, radii, text sizes. Views read sizes from `theme().metrics`, never write their own numbers for them.
2. **Kit** (`app/Source/ui/Kit.*`, namespace `ui::kit`): the painting of every control kind in every state — button (plain, primary, toggle, danger, ghost, icon-only), switch, segmented control, field, card, list row, badge, plus knob, fader thumb and lens from `GlassMaterial`. States: normal, hover, pressed, selected/on, keyboard focus, disabled. Classic themes keep their look through the same functions.
3. **Components** keep their behaviour (gestures, undo, help text) and call the kit to paint. A new screen uses the kit and the tokens only.
4. **Glass backdrop on the GPU** (later step): real blur and refraction of what lies under a panel, with the current CPU material as the fallback.

## Steps

- [x] Plan written down (this file).
- [ ] `Theme::Metrics` with defaults, optional `metrics` object in theme JSON, glass values from the design tokens.
- [ ] `ui::kit` painters with states; tests for every kind × state in all six themes (painting does not throw, contrast of labels ≥ 4.5:1).
- [ ] Move the existing controls onto the kit: top bar buttons and tabs, session strip buttons, arrangement header buttons, piano roll chips and mode bar, browser rows and preview, look-and-feel buttons, toggles, combo boxes, text fields.
- [ ] Screens compared against the sheets one by one, starting with the main window.
- [ ] GPU backdrop.
