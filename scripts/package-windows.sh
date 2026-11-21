#!/usr/bin/env bash
# Builds dist/yapr/ as a self-contained portable folder (MSYS2 UCRT64).
# Run from an MSYS2 UCRT64 shell, or via scripts/package-windows.ps1.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="$root/build"
dist="$root/dist/yapr"

if [ ! -f "$build/yapr.exe" ]; then
    echo "error: $build/yapr.exe not found - build first" >&2
    exit 1
fi

rm -rf "$dist"
mkdir -p "$dist"
cp "$build/yapr.exe" "$dist/"

if command -v windeployqt6 >/dev/null; then
    windeployqt6 --release --no-translations "$dist/yapr.exe"
else
    windeployqt --release --no-translations "$dist/yapr.exe"
fi

# windeployqt only handles Qt's own DLLs. Walk the import closure of
# everything in dist and copy the remaining UCRT64 dependencies (mingw
# runtime, icu, freetype, ...) until nothing new appears.
changed=1
while [ "$changed" -eq 1 ]; do
    changed=0
    while IFS= read -r -d '' f; do
        for dep in $(ldd "$f" 2>/dev/null | awk '/\/ucrt64\//{print $3}'); do
            base="$(basename "$dep")"
            if [ ! -f "$dist/$base" ]; then
                cp "$dep" "$dist/"
                changed=1
            fi
        done
    done < <(find "$dist" \( -name '*.exe' -o -name '*.dll' \) -print0)
done

echo "portable folder ready: $dist"
