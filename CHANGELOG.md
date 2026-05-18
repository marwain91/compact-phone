# Changelog

All notable changes will be documented here. The format is loosely based
on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the
project follows [SemVer](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Boot-time provisioning: CLI flags (`--sip-server`, `--sip-user`,
  `--sip-password-file`, `--config`) and a JSON provisioning file
  (`/etc/compactphone/provisioning.json` or `--config <path>`) for
  mass deployment. See `docs/provisioning.md`.
- Codec selection UI in the account editor — reorder + enable/disable
  per account.
- Speed-dial favorites strip on the dialer. Star contacts to pin them.
- Contact import from vCard 2.1/3.0/4.0 and CSV (Google Contacts shape).
- In-app keyboard shortcuts: Ctrl+Shift+A/H/D/R for answer/hangup/DND
  toggle/redial last (while the window has focus).
- Diagnostics export: Settings → Advanced → produces a redacted text
  file containing the recent log tail + account summary.
- Call quality indicators: RTCP-derived MOS / packet loss / RTT / jitter
  via `PhoneController.streamStats(callId)`.
- Internationalization scaffold (Qt Linguist `qt_add_translations`).
  Czech seed file under `src/i18n/`.
- macOS sleep/wake re-registration via NSWorkspaceDidWakeNotification.
- Sentry crash-reporting scaffold gated on `COMPACTPHONE_ENABLE_SENTRY`
  and an explicit user opt-in.
- Native keychain backends: macOS Security.framework + Windows
  Credential Manager. AES file fallback for everything else.

### Changed
- Settings pane is now split into General / Audio / Calls / Advanced
  tabs.
- Midnight theme accent shifted from sky blue to the brand red-orange
  so the running app stays visually aligned with the dock icon.
- Main window gradient now follows the active theme's accent tint
  rather than a hard-coded color.

### Packaging
- macOS `.icns`, Windows `.ico`, and Linux hicolor PNG set generated
  from the canonical SVG via `tools/generate-icons.sh`.
- `CFBundleIconFile` wired into `Info.plist.in`.
- Windows MSI scaffold (`packaging/windows/installer.wxs`) + CI job
  producing an unsigned MSI on every push.
- macOS DMG build/notarization pipeline (`packaging/macos/build-dmg.sh`)
  + entitlements file.
- Sparkle 2 appcast schema documented (`docs/appcast.md`).
