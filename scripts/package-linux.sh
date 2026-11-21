#!/usr/bin/env bash
# Builds dist/yapr-<version>-linux-x86_64.AppImage from a Release build.
# Needs: qmake for the Qt used to build (set QMAKE, or have qmake6/qmake on
# PATH), curl, and a configured+built tree in $BUILD_DIR (default: build/).
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${BUILD_DIR:-$root/build}"
dist="$root/dist"
version="$(sed -n 's/^[[:space:]]*VERSION[[:space:]]\+\([0-9.]\+\).*/\1/p' "$root/CMakeLists.txt" | head -1)"

appdir="$dist/AppDir"
tools="$dist/tools"
rm -rf "$appdir"
mkdir -p "$appdir" "$tools"

DESTDIR="$appdir" cmake --install "$build" --prefix /usr

# linuxdeploy + its Qt plugin bundle Qt, its plugins, and other non-system
# libraries, then wrap everything into an AppImage.
fetch() {
    [ -x "$tools/$1" ] || {
        curl -fsSL -o "$tools/$1" "$2"
        chmod +x "$tools/$1"
    }
}
fetch linuxdeploy-x86_64.AppImage \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
fetch linuxdeploy-plugin-qt-x86_64.AppImage \
    https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage

export PATH="$tools:$PATH"
export APPIMAGE_EXTRACT_AND_RUN=1   # CI runners have no FUSE
export QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake)}"
export OUTPUT="$dist/yapr-$version-linux-x86_64.AppImage"

linuxdeploy-x86_64.AppImage \
    --appdir "$appdir" \
    --desktop-file "$appdir/usr/share/applications/io.github.cornish.yapr.desktop" \
    --plugin qt \
    --output appimage

echo "AppImage ready: $OUTPUT"
