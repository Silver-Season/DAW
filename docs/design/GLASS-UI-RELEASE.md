# Glass UI preview package — 2026-10-19

- Branch: `codex/glass-ui-current`; includes GitHub main `be186ab` and its PR #25 brand assets.
- Package source checkpoint: `741872e` (the application UI source is unchanged from the tested `1dab2f2` checkpoint).
- Local file: `dist/Prisma-Glass-2026-10-19-arm64.dmg` (ignored in Git; publish the DMG and its `.sha256` file as release assets).
- Architecture: **arm64 / Apple Silicon only**. This local file is not universal.
- Size: 14,724,366 bytes (14.0 MiB).
- SHA-256: `573a424b978d5dd4439a20eea347d35bb1cc2c202793b5e4f24865b23ff3f78c`.

Contents: DAW.app; Prisma AU, VST3 and Standalone; Applications shortcut; installation instructions in English/Russian; root license and dependency/font license notices; BUILD-INFO.txt with branch, source commit and architectures.

Verified: the app and all three plugin-format bundles exist and have matching arm64 slices; staged and mounted-bundle code signatures pass `codesign --verify --deep --strict`; `hdiutil verify` passes; the read-only test mount was detached. The signature is ad-hoc, with no Developer ID certificate or notarization. The 10 project test suites passed before packaging; no audio DSP changed in this packaging follow-up. Local hardware audio, universal binaries and CI execution were not revalidated. The new AU and Standalone targets built successfully.

Screenshots: [22 native captures](../../media/ui-glass/index.html). Implementation/limits: [GLASS-UI.md](GLASS-UI.md).

## Reproduce

Build Release targets DAW, Prisma_AU, Prisma_VST3 and Prisma_Standalone outside the repository, then run:

```sh
./scripts/package-mac.sh /absolute/build/directory /absolute/output/Prisma-DAW.dmg
```

The same script is used by macOS CI; it verifies the image and writes a portable SHA-256 sidecar. The CI AU validation step installs its test AU on the ephemeral runner explicitly, so local builds can keep plugin auto-installation disabled.

## Publication

The source, scripts, documentation and 22 screenshots are committed locally. GitHub upload is still blocked by missing Git credentials; an HTTPS push reports `could not read Username`. There is no published new DMG, branch, PR or release yet. After authorized GitHub login, push `codex/glass-ui-current`, open a PR with Before / After / How, let CI build/validate its universal artifact, and attach this arm64 preview only if clearly labelled. Do not replace the existing nightly main release with an unmerged preview. Generated concept images/archives under `media/design/` remain local, mostly untracked; the optics HTML prototype is committed.
