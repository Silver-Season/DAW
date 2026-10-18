# Brand

The mark: a glass prism, the incoming beam and six spectrum bars that read as tracks.

- `app-icon.svg`: the macOS app icon source (rounded square, dark ground). `app/Resources/brand/AppIcon-1024.png` is rendered from it (`rsvg-convert -w 1024 media/brand/app-icon.svg -o app/Resources/brand/AppIcon-1024.png`) and set as `ICON_BIG` in `app/CMakeLists.txt`.
- `logo-mark-dark.svg`, `logo-mark-light.svg`: the mark alone for dark and light grounds.
- In the app the mark is drawn in code by `ui::drawLogo` (`app/Source/ui/Chrome.cpp`) with the same geometry, in the colors of the current theme (ink = `text`, bars = the theme's hues, glow = `glow`). Below about 60 px it leaves out the inner edges.
- `topbar-themes.png`: the top bar in the Prism, Paper and Neon themes.
