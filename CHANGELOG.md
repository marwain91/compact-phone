# Changelog

All notable changes will be documented here. The format is loosely based
on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the
project follows [SemVer](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.5] - 2026-06-05

### Fixed
- Call History again shows just the dialled number for calls to a bare SIP
  address. The list row labels itself from the remote *display name* and only
  falls back to the number when that's empty — but for these calls PJSIP hands
  us the remote Contact header (`<sip:95.80.200.178:5060;transport=tcp>`) as
  the "display name", so the row printed the raw URI verbatim. A display name
  that is actually a SIP URI is now treated as no name and reduced to the
  number.

### Changed
- The **Settings → General** theme picker is now a compact row of wrapping
  chips (swatch + name) instead of large preview tiles. The old fixed-size
  two-column tiles overflowed the window width; the chips size to their
  content and always fit.

## [0.1.4] - 2026-06-05

### Fixed
- The active-call screen no longer lets a long SIP address run off the edge
  of the window. It now shows just the number (consistent with Call History)
  and ellipsizes if it still doesn't fit. The caller name was missing a width
  bound, so its `elide` never engaged.

## [0.1.3] - 2026-06-05

### Fixed
- Hammering Enter (or the Call button) on the dialpad no longer places
  several duplicate calls to the same number. The dial action was
  synchronous with no re-submit guard, so each keypress — natural when
  you're unsure the first one registered — started another concurrent
  call. A dial to a target that already has a live outbound call is now
  ignored; deliberately calling a *different* number (second line /
  attended transfer) and re-dialing the same number after the call ends
  both still work.
- Selecting a different microphone or speaker in **Settings → Audio** now
  actually switches the device. The device list keyed each entry by its
  position in the enumeration instead of its pjsua2 device id; the two
  are not guaranteed equal (notably on macOS CoreAudio), so picking a
  device applied the wrong id — or none — and the picker could snap back.
  Entries now carry the real device id.
- Call History shows just the dialled number (or the contact's name when
  known) instead of the full `sip:user@domain` URI.
- Daktela sign-in no longer fails with "Could not fetch SIP credentials".
  Two bugs: the user profile (`whoim`) lists SIP devices under
  `user.extensions[]` (an array) but we only looked for a singular
  `extension`, so we never found the device; and the device-record URL was
  wrong (`/api/v6/extensions/sipdevices/…` instead of the top-level
  `/api/v6/sipDevices/{name}.json`).

### Changed
- The provider sign-in wizard now prompts for the SIP password when it
  can't be fetched automatically — e.g. your Daktela account lacks
  permission to read the device record — instead of failing outright. It
  already has every other account detail from your profile.
- The sign-in wizard advances on **Enter**: it submits each step, and Enter
  on the username field jumps to the password field.

## [0.1.2] - 2026-06-04

### Added
- **Launch on startup** — start Compact Phone automatically when you log
  in. Registers a per-user OS login item (macOS Login Items via
  `SMAppService`, the Windows `Run` key, or a Linux XDG autostart entry).
  Hidden on systems without a desktop session.
- **Start minimized to tray** — open hidden in the system tray instead of
  showing the window. Applies to every launch; pair it with Launch on
  startup to come online silently at login, ready to take calls.

### Changed
- Minimum macOS is now **13 (Ventura)**, up from 12 (Monterey). The Launch
  on startup integration uses `SMAppService`, which requires macOS 13;
  macOS 12 is no longer supported.

### Fixed
- Dialpad no longer overflows when more than one account is configured.
  The v0.1.1 account selector was a separate fixed-width control that
  pushed the keypad and Call button off the right edge of the window; the
  registration status pill and the account switcher are now a single
  dynamic element (the pill gains a chevron and opens an account menu when
  multiple accounts exist).

## [0.1.1] - 2026-06-02

### Fixed
- Dialpad account selector now appears when more than one account is
  configured. Its visibility was bound to a non-reactive `rowCount()`
  call that latched `false` before accounts finished loading, so the
  picker stayed hidden even with multiple accounts. `AccountsModel` now
  exposes a reactive `count` property and the dialer binds to it.
- Long Call History entries now truncate with an ellipsis instead of
  overflowing the row — the `elide` was already set but the text had no
  width constraint to clip against.

### Changed
- Call history is now bounded: at most 50 entries and 90 days are kept.
  Older entries are pruned automatically on each new call so the log no
  longer grows without limit.

## [0.1.0] - 2026-05-26

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
- Daktela account provisioning and Daktela-specific branding in the dialer
  and active-call views.
- Collapsed-sidebar opener handle for returning to the navigation rail.

### Changed
- Settings pane is now split into General / Audio / Calls / Advanced
  tabs.
- Midnight theme accent shifted from sky blue to the brand red-orange
  so the running app stays visually aligned with the dock icon.
- Main window gradient now follows the active theme's accent tint
  rather than a hard-coded color.
- Active-call controls now resize and space themselves within the compact
  window, including when the sidebar is expanded.

### Packaging
- macOS `.icns`, Windows `.ico`, and Linux hicolor PNG set generated
  from the canonical SVG via `tools/generate-icons.sh`.
- `CFBundleIconFile` wired into `Info.plist.in`.
- Windows MSI scaffold (`packaging/windows/installer.wxs`) + CI job
  producing an unsigned MSI on every push.
- macOS DMG build/notarization pipeline (`packaging/macos/build-dmg.sh`)
  + entitlements file.
- Sparkle 2 appcast schema documented (`docs/appcast.md`).
