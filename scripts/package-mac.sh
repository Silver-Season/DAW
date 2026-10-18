#!/usr/bin/env bash
# Package an existing Release build, without installing into the user's plugin folders.
set -euo pipefail

if [ "$(uname -s)" != Darwin ] || [ "$#" -ne 2 ]; then
    echo "Usage on macOS: $0 BUILD_DIRECTORY OUTPUT.dmg" >&2
    exit 2
fi

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="$(cd "$1" && pwd)"
mkdir -p "$(dirname "$2")"
output="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
plugin_dir="$build_dir/plugins/prisma/Prisma_artefacts/Release"
app="$build_dir/app/DAW_artefacts/Release/DAW.app"

for bundle in "$app" "$plugin_dir/AU/Prisma.component" "$plugin_dir/VST3/Prisma.vst3" "$plugin_dir/Standalone/Prisma.app"; do
    if [ ! -d "$bundle/Contents/MacOS" ]; then
        echo "Missing Release bundle: $bundle" >&2
        exit 1
    fi
done

stage_dir="$(mktemp -d /private/tmp/prisma-dmg.XXXXXX)"
mounted=0
cleanup() {
    if [ "$mounted" = 1 ]; then
        hdiutil detach "$stage_dir/mount" >/dev/null || return
    fi
    rm -rf "$stage_dir"
}
trap cleanup EXIT
mkdir -p "$stage_dir/image/Plugins" "$stage_dir/mount"

# Stage outside File Provider folders. Copy no quarantine/resource-fork metadata, then sign the copies.
ditto --noextattr --norsrc "$app" "$stage_dir/image/DAW.app"
for relative in AU/Prisma.component VST3/Prisma.vst3 Standalone/Prisma.app; do
    ditto --noextattr --norsrc "$plugin_dir/$relative" "$stage_dir/image/Plugins/$(basename "$relative")"
done
for bundle in "$stage_dir/image/DAW.app" "$stage_dir/image/Plugins/"*; do
    codesign --force --deep --sign - --timestamp=none "$bundle"
    codesign --verify --deep --strict "$bundle"
done

architectures="$(lipo -archs "$stage_dir/image/DAW.app/Contents/MacOS/DAW")"
for bundle in "$stage_dir/image/Plugins/"*; do
    plugin_architectures="$(lipo -archs "$bundle/Contents/MacOS/Prisma")"
    if [ "$architectures" != "$plugin_architectures" ]; then
        echo "Architecture mismatch: $bundle ($plugin_architectures), app ($architectures)" >&2
        exit 1
    fi
done

cp "$repo_dir/packaging/INSTALL.txt" "$repo_dir/packaging/INSTALL-ru.txt" "$repo_dir/LICENSE" "$stage_dir/image/"
mkdir -p "$stage_dir/image/Licenses"
cp "$repo_dir"/app/Resources/fonts/OFL-*.txt "$stage_dir/image/Licenses/"
cp "$repo_dir/packaging/licenses/JUCE-8.0.4.md" "$stage_dir/image/Licenses/"
cp "$repo_dir/third_party/rubberband/COPYING" "$stage_dir/image/Licenses/RubberBand-COPYING.txt"
cp "$repo_dir/third_party/rubberband/SOURCE.md" "$stage_dir/image/Licenses/RubberBand-SOURCE.md"
ln -s /Applications "$stage_dir/image/Applications"
{
    echo "Prisma DAW — preview build"
    echo "Source commit: $(git -C "$repo_dir" rev-parse HEAD)"
    echo "Source branch: $(git -C "$repo_dir" branch --show-current)"
    echo "Architectures: $architectures"
    echo "Built package (UTC): $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    echo "Ad-hoc signed; not Apple Developer ID signed or notarized."
    echo "Source: https://github.com/Silver-Season/DAW"
} > "$stage_dir/image/BUILD-INFO.txt"

created=0
for attempt in 1 2 3; do
    if hdiutil create -volname "Prisma DAW" -srcfolder "$stage_dir/image" -ov -format UDZO "$output"; then
        created=1
        break
    fi
    sleep 2
done
[ "$created" = 1 ] || exit 1
hdiutil verify "$output"
hdiutil attach -readonly -nobrowse -mountpoint "$stage_dir/mount" "$output" >/dev/null
mounted=1
for bundle in "$stage_dir/mount/DAW.app" "$stage_dir/mount/Plugins/"*; do
    codesign --verify --deep --strict "$bundle"
done
hdiutil detach "$stage_dir/mount" >/dev/null
mounted=0
(cd "$(dirname "$output")" && shasum -a 256 "$(basename "$output")") > "$output.sha256"
echo "Verified DMG: $output ($architectures)"
