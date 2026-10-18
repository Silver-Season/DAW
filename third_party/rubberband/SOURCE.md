# Rubber Band Library

- Upstream: https://github.com/breakfastquay/rubberband
- Release tag: `v3.3.0` (the latest 3.x release)
- Commit: `2be46b0dffb13273a67396c77bc9278736bb03d2`
- Licence: GNU GPL v2 or later (`COPYING`), compatible with the project's AGPLv3.
  Copyright 2007-2023 Particular Programs Ltd.

## What is kept

Only the files the single-file build needs: `single/RubberBandSingle.cpp`, the public headers in `rubberband/`,
the sources and headers it includes from `src/` (`common`, `faster` = R2 engine, `finer` = R3 engine) and `COPYING`.
Not kept: the command-line tool, LADSPA/LV2/Vamp plugins, JNI and .NET bindings, tests, Meson files and the
third-party code under `src/ext/` (kissfft, speex, getopt), which the single-file build does not use.

The upstream files are unmodified. `CMakeLists.txt` and this file are ours.

## Build configuration

`RubberBandSingle.cpp` sets `USE_BQRESAMPLER`, `NO_THREADING`, `NO_TIMING`, `NO_THREAD_CHECKS` and either
`USE_BUILTIN_FFT` or, on Apple platforms, `HAVE_VDSP` (linked with the Accelerate framework). The library builds as
the static target `rubberband` with its warnings silenced; the project uses it only through
`core/include/daw/dsp/TimeStretch.h`.

## Updating

Clone the new tag, list the files the single-file build reads
(`g++ -M -DUSE_BUILTIN_FFT -I. single/RubberBandSingle.cpp`), copy them over these, update the tag and commit above,
and run `daw_stretch_tests`.
