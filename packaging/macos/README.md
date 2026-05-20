# macOS packaging, signing & notarization

This directory holds the entitlements file and the DMG build/notarize
pipeline.

## Prerequisites you must procure

1. **Apple Developer Program** membership — $99/year, individual or
   organization. Sign up at https://developer.apple.com/programs/.
2. **Developer ID Application certificate** — created in Apple Developer
   account, imported into the Keychain on the signing machine.
   `security find-identity -p codesigning -v` should show it.
3. **App-specific password** — created from Apple ID Security settings
   (https://appleid.apple.com → Sign-In and Security → App-Specific
   Passwords). This is what `notarytool` uses; the regular Apple ID
   password won't work.
4. **Team ID** — 10-character identifier visible at the top right of
   developer.apple.com.

## Build steps

```sh
cmake --preset macos
cmake --build --preset macos

export DEVELOPER_ID_APP="Developer ID Application: Your Name (TEAMID)"
export NOTARY_APPLE_ID="you@example.com"
export NOTARY_TEAM_ID="ABCD123456"
export NOTARY_PASSWORD="abcd-efgh-ijkl-mnop"   # app-specific password

./packaging/macos/build-dmg.sh
```

Output lands in `dist/compactphone-<version>.dmg`. Verify on a fresh
Mac:

```sh
spctl --assess --type install -v dist/compactphone-*.dmg
xcrun stapler validate dist/compactphone-*.dmg
```

## CI

The `release-macos.yml` workflow (TODO) fires on a `v*` tag, decrypts the
signing identity from a runner-local keychain (provisioned via repository
secret `APPLE_DEVELOPER_ID_P12_BASE64`), runs `build-dmg.sh`, and uploads
the DMG to the GitHub release.

Required repository secrets:
- `APPLE_DEVELOPER_ID_P12_BASE64` — base64 of the .p12 export of the
  Developer ID Application cert + private key
- `APPLE_DEVELOPER_ID_P12_PASSWORD`
- `NOTARY_APPLE_ID`
- `NOTARY_TEAM_ID`
- `NOTARY_PASSWORD`

## Entitlements

`compactphone.entitlements` declares what hardened-runtime exceptions the
app needs:
- microphone capture (for SIP audio)
- network client/server (for SIP REGISTER and RTP)
- library validation disabled (Qt plugins are dlopened at runtime; without
  this exception, hardened runtime would reject them)

JIT and unsigned executable memory remain **off** — Qt does not need either
and keeping them off shrinks the attack surface.

## Licence notices in the bundle

The top-level `CMakeLists.txt` attaches `LICENSE` and
`THIRD_PARTY_LICENSES.md` to the `compactphone` target with
`MACOSX_PACKAGE_LOCATION = Resources`, so they end up inside
`compactphone.app/Contents/Resources/` automatically. `Info.plist`
also carries `NSHumanReadableCopyright` with the GPL notice and
source-code URL — that line shows in the Finder Get-Info panel.

`build-dmg.sh` does not need to do anything extra; the licence files
ride along inside the bundle into the DMG.

## What's still missing

- The `release-macos.yml` workflow.
- Apple Developer ID procurement.
- An updates appcast endpoint (see task #8, Sparkle).
- The sleep/wake re-registration hook (task #15).
