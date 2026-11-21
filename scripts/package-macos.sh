#!/usr/bin/env bash
# Builds dist/yapr-<version>-macos.dmg from a Release build of yapr.app.
# Needs macdeployqt from the Qt used to build (on PATH, or set MACDEPLOYQT).
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${BUILD_DIR:-$root/build}"
dist="$root/dist"
version="$(sed -n 's/^[[:space:]]*VERSION[[:space:]]\{1,\}\([0-9.]\{1,\}\).*/\1/p' "$root/CMakeLists.txt" | head -1)"
macdeployqt="${MACDEPLOYQT:-$(command -v macdeployqt6 || command -v macdeployqt)}"

stage="$dist/dmg"
rm -rf "$stage"
mkdir -p "$stage"
cp -R "$build/yapr.app" "$stage/"

# Bundle Qt frameworks and plugins into the app.
"$macdeployqt" "$stage/yapr.app" -always-overwrite

# The release promises a universal app; fail loudly if it isn't.
archs="$(lipo -archs "$stage/yapr.app/Contents/MacOS/yapr")"
echo "yapr.app architectures: $archs"
case "$archs" in
    *arm64*x86_64*|*x86_64*arm64*) ;;
    *) echo "error: expected a universal (arm64 + x86_64) binary" >&2; exit 1 ;;
esac

# macdeployqt rewrites binaries, invalidating signatures; Apple Silicon
# refuses to run unsigned code at all, so re-sign ad hoc. (Not a Developer
# ID signature: users still right-click > Open on first launch.)
codesign --force --deep --sign - "$stage/yapr.app"

# Drag-to-install disk image: the app plus a shortcut to /Applications.
ln -s /Applications "$stage/Applications"
dmg="$dist/yapr-$version-macos.dmg"
rm -f "$dmg"
hdiutil create -volname "yapr $version" -srcfolder "$stage" -ov -format UDZO "$dmg"
rm -rf "$stage"

echo "disk image ready: $dmg"
