#!/usr/bin/env bash
# Builds the DAW app and the Prisma plugin (AU, VST3, Standalone) as universal binaries.
# Needs Xcode (or the Command Line Tools) and CMake: `brew install cmake ninja`.
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
echo
echo "App:    build/app/DAW_artefacts/Release/DAW.app"
echo "Plugin: installed to ~/Library/Audio/Plug-Ins/Components (AU) and ~/Library/Audio/Plug-Ins/VST3"
