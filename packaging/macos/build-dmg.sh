#!/usr/bin/env bash
# Build a signed, notarized DMG of Compact Phone for distribution.
#
# Required environment:
#   DEVELOPER_ID_APP    "Developer ID Application: Acme, Inc. (TEAMID)"
#   NOTARY_APPLE_ID     apple-id-email@example.com
#   NOTARY_TEAM_ID      10-character team identifier
#   NOTARY_PASSWORD     app-specific password (Apple ID Security settings)
#
# Optional:
#   APP_BUNDLE          path to the .app (default: build/macos/src/Compact Phone.app)
#   DIST_DIR            output directory (default: dist)
#
# Steps:
#   1. Sign the .app (deep, hardened runtime, embedded entitlements)
#   2. Bundle into a DMG using create-dmg or hdiutil
#   3. Sign the DMG
#   4. Submit to Apple notarization and wait
#   5. Staple the notarization ticket onto the DMG

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP_BUNDLE="${APP_BUNDLE:-$REPO_ROOT/build/macos/src/Compact Phone.app}"
DIST_DIR="${DIST_DIR:-$REPO_ROOT/dist}"
ENTITLEMENTS="$REPO_ROOT/packaging/macos/compactphone.entitlements"
VERSION="$(grep -E 'VERSION [0-9]' "$REPO_ROOT/CMakeLists.txt" | head -1 | awk '{print $2}')"
DMG_NAME="compactphone-${VERSION}.dmg"

require() {
    if [[ -z "${!1:-}" ]]; then
        echo "error: env var $1 is required" >&2
        exit 1
    fi
}

require DEVELOPER_ID_APP
require NOTARY_APPLE_ID
require NOTARY_TEAM_ID
require NOTARY_PASSWORD

if [[ ! -d "$APP_BUNDLE" ]]; then
    echo "error: $APP_BUNDLE does not exist (run 'cmake --build --preset macos' first)" >&2
    exit 1
fi

mkdir -p "$DIST_DIR"

echo "→ codesigning $APP_BUNDLE"
codesign --force --deep --verify --verbose \
    --options runtime \
    --timestamp \
    --entitlements "$ENTITLEMENTS" \
    --sign "$DEVELOPER_ID_APP" \
    "$APP_BUNDLE"

echo "→ verifying signature"
codesign --verify --deep --strict --verbose=2 "$APP_BUNDLE"
spctl --assess --type execute --verbose "$APP_BUNDLE" || true

echo "→ building DMG"
DMG_PATH="$DIST_DIR/$DMG_NAME"
rm -f "$DMG_PATH"
if command -v create-dmg >/dev/null; then
    create-dmg \
        --volname "Compact Phone" \
        --window-pos 200 120 \
        --window-size 600 360 \
        --icon-size 100 \
        --icon "Compact Phone.app" 160 160 \
        --hide-extension "Compact Phone.app" \
        --app-drop-link 440 160 \
        "$DMG_PATH" \
        "$APP_BUNDLE"
else
    # Fallback: minimal hdiutil-only DMG.
    STAGE="$(mktemp -d)"
    cp -R "$APP_BUNDLE" "$STAGE/"
    ln -s /Applications "$STAGE/Applications"
    hdiutil create -volname "Compact Phone" -srcfolder "$STAGE" \
        -ov -format UDZO "$DMG_PATH"
    rm -rf "$STAGE"
fi

echo "→ signing DMG"
codesign --force --timestamp --sign "$DEVELOPER_ID_APP" "$DMG_PATH"

echo "→ notarizing $DMG_PATH"
xcrun notarytool submit "$DMG_PATH" \
    --apple-id "$NOTARY_APPLE_ID" \
    --team-id "$NOTARY_TEAM_ID" \
    --password "$NOTARY_PASSWORD" \
    --wait

echo "→ stapling notarization ticket"
xcrun stapler staple "$DMG_PATH"
xcrun stapler validate "$DMG_PATH"

echo "ok → $DMG_PATH"
