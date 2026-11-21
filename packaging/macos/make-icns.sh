#!/bin/sh
# Builds a macOS .icns from one large square PNG using Apple's tools.
# Usage: make-icns.sh <source.png> <output.icns>
set -eu
src="$1"
out="$2"
iconset="$(mktemp -d)/yapr.iconset"
mkdir -p "$iconset"
for size in 16 32 128 256 512; do
    sips -z $size $size "$src" --out "$iconset/icon_${size}x${size}.png" >/dev/null
    double=$((size * 2))
    sips -z $double $double "$src" --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$iconset" -o "$out"
rm -rf "$(dirname "$iconset")"
