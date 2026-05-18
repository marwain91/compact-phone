#!/usr/bin/env bash
# Regenerate every derived icon asset (Linux PNG set, macOS .icns,
# Windows .ico) from branding/compactphone-appicon.svg, the canonical
# source of truth.
#
# Requirements (Linux / Debian-family):
#   apt-get install -y librsvg2-bin icnsutils icoutils
# On macOS the same tools are available via Homebrew:
#   brew install librsvg libicns icoutils
# (Alternatively macOS users can rely on the system `iconutil` and run
#  `iconutil -c icns branding/compactphone.iconset` after producing the
#  iconset directory from the PNGs below.)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BRAND_DIR="$REPO_ROOT/branding"
GEN_DIR="$BRAND_DIR/generated"
SRC_SVG="$BRAND_DIR/compactphone-appicon.svg"

if [[ ! -f "$SRC_SVG" ]]; then
    echo "error: $SRC_SVG not found" >&2
    exit 1
fi

mkdir -p "$GEN_DIR"

# Linux hicolor sizes — sufficient for KDE, GNOME, Cinnamon, XFCE.
SIZES=(16 32 48 64 128 256 512 1024)
for sz in "${SIZES[@]}"; do
    rsvg-convert -w "$sz" -h "$sz" "$SRC_SVG" -o "$GEN_DIR/icon_${sz}x${sz}.png"
done

# Windows .ico — embedded in the .exe via src/compactphone.rc.
icotool -c -o "$BRAND_DIR/compactphone.ico" \
    "$GEN_DIR/icon_16x16.png" "$GEN_DIR/icon_32x32.png" \
    "$GEN_DIR/icon_48x48.png" "$GEN_DIR/icon_64x64.png" \
    "$GEN_DIR/icon_128x128.png" "$GEN_DIR/icon_256x256.png"

# macOS .icns — referenced by CFBundleIconFile in src/Info.plist.in.
png2icns "$BRAND_DIR/compactphone.icns" \
    "$GEN_DIR/icon_16x16.png" "$GEN_DIR/icon_32x32.png" \
    "$GEN_DIR/icon_48x48.png" "$GEN_DIR/icon_128x128.png" \
    "$GEN_DIR/icon_256x256.png" "$GEN_DIR/icon_512x512.png" \
    "$GEN_DIR/icon_1024x1024.png"

echo "ok — regenerated $BRAND_DIR/compactphone.ico, $BRAND_DIR/compactphone.icns,"
echo "and $GEN_DIR/icon_*.png from $SRC_SVG"
