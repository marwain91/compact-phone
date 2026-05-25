# Release feed (appcast)

Compact Phone uses Sparkle 2.x appcast XML as a lightweight
cross-platform update feed. The in-app Qt `UpdateChecker` fetches the
feed for the current OS, compares the appcast version with the running
`Qt.application.version`, and shows a download action in Settings when a
newer installer is available.

This is not full automatic updating. Users still download and run the
new DMG, AppImage, or MSI themselves. On Windows the MSI has a stable
`UpgradeCode` and `MajorUpgrade`, so a newer MSI upgrades the existing
installation when its product version increases.

## Public feeds

Each platform release workflow uploads one appcast next to the installer:

| Platform | Feed |
|---|---|
| macOS | `https://github.com/marwain91/compact-phone/releases/latest/download/appcast-macos.xml` |
| Linux | `https://github.com/marwain91/compact-phone/releases/latest/download/appcast-linux.xml` |
| Windows | `https://github.com/marwain91/compact-phone/releases/latest/download/appcast-windows.xml` |

GitHub's `/releases/latest/download/...` endpoint redirects to the
newest non-prerelease release. Test tags such as `v0.1.0-test7` are
uploaded as prereleases and do not become the public update feed.

## Generation

`tools/release/generate-appcast.py` writes one feed entry for one
artifact. The release workflows call it after packaging and, for signed
platforms, after signing so the uploaded file and feed metadata match.

Optional Ed25519 signing is supported through the
`SPARKLE_ED25519_PRIVATE_KEY` Actions secret. The current in-tree
checker does not verify this signature; it is forward-compatible
hygiene for adopting Sparkle/WinSparkle later.

## Version guard

The release workflows run `tools/release/verify-release-version.py`
before spending hours building. It compares the release tag core version
with `project(CompactPhone VERSION ...)` in `CMakeLists.txt`.

Examples:

```sh
python3 tools/release/verify-release-version.py v0.1.0
python3 tools/release/verify-release-version.py refs/tags/v0.1.0-test7
```

`v0.1.0-test7` is accepted when CMake says `0.1.0`; `v0.1.1` fails
until the project version is bumped to `0.1.1`.
