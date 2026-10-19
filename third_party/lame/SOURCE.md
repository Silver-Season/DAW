# LAME (libmp3lame)

- Upstream: https://lame.sourceforge.io/
- Archive: https://downloads.sourceforge.net/project/lame/lame/3.100/lame-3.100.tar.gz
- Version: 3.100
- SHA-256 of the archive: `ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e`
  (MD5 `83e260acbe4389b54fe08e0bdbf7cddb`, checked against the file listing of the SourceForge project)
- Licence: GNU LGPL v2 or later (`COPYING`, `LICENSE`), compatible with the project's AGPLv3.

## What is kept

`include/lame.h`, the encoder sources in `libmp3lame/` (without `mpglib_interface.c`, the decoder hook, and of `vector/`
only the header the sources include, not the SSE code), `COPYING`, `LICENSE` and `README`. The upstream files are unmodified. `CMakeLists.txt`,
`config/config.h` and this file are ours.

## Build configuration

`config/config.h` describes a 64-bit macOS or Linux build with no SIMD, so the same objects serve both architectures of
a universal binary. `NOANALYSIS` drops the frame analyser hooks.
