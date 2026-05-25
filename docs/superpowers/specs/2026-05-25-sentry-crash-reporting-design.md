# Sentry Crash Reporting Design

Date: 2026-05-25

## Goal

Enable useful crash reporting for early Compact Phone builds without surprising
users or expanding the CI cost profile. Sentry should be available in both
test/prerelease and public release artifacts, but uploads must remain opt-in
through Settings.

## Current State

- The app already has `Settings -> Advanced -> Send crash reports`.
- The setting is off by default and persists as `crash_reporting_enabled`.
- `CrashReporting.cpp` is compiled as a no-op unless
  `COMPACTPHONE_ENABLE_SENTRY=ON`.
- `COMPACTPHONE_SENTRY_DSN` exists as a CMake cache variable, but the app does
  not currently expose it to `main.cpp` as a compile definition.
- Release workflows do not currently enable Sentry or pass a DSN.

## Requirements

- Crash reporting is available in macOS, Linux, and Windows release artifacts
  when the repository has a `SENTRY_DSN` secret configured.
- Availability covers both test/prerelease tags and public release tags.
- Crash reporting remains disabled until the user opts in.
- The toggle must not silently pretend to work in builds that lack Sentry
  support.
- Empty, missing, or malformed DSNs must fail closed and must not crash the app.
- No SIP credentials, account data, diagnostics bundles, or application logs are
  uploaded automatically.
- Sentry default PII collection remains disabled.
- Long build workflows remain release/manual only; enabling Sentry must not add
  long-running work to normal PR or merge CI.

## Architecture

### Build Configuration

Release workflows pass two CMake values when `secrets.SENTRY_DSN` is non-empty:

- `-DCOMPACTPHONE_ENABLE_SENTRY=ON`
- `-DCOMPACTPHONE_SENTRY_DSN=<secret value>`

The CMake target converts a non-empty `COMPACTPHONE_SENTRY_DSN` cache value into
a private compile definition for the executable that calls `initSentry()`.
Without the secret, release builds continue to compile with the current no-op
crash reporter.

### Runtime Behavior

`PhoneController` exposes whether crash reporting is available in the current
build. The Settings UI uses that state so the existing checkbox is either
usable or clearly unavailable.

When the user enables the checkbox, the app initializes Sentry during the same
session if possible. If the SDK path cannot be initialized after startup on a
platform, the UI should tell the user that crash reporting will start after an
app restart. In either case, disabling the checkbox persists immediately and no
new reports should be uploaded after the next clean start.

### Privacy

Sentry is configured for crash data only:

- `sentry_options_set_send_default_pii(opts, 0)`
- no credentials, provisioning data, account details, or diagnostics export
  contents are attached
- no automatic log attachment

The existing text "Anonymous, opt-in. SIP credentials are never sent." remains
accurate. If the implementation adds a disabled state, the helper text may add a
short unavailable message for builds without Sentry support.

## Error Handling

- Missing DSN: Sentry is unavailable and the app runs normally.
- Invalid DSN: initialization is skipped or fails closed; the app logs a warning
  and continues.
- User has not opted in: initialization is skipped.
- Duplicate initialization: remains idempotent.
- Shutdown without initialization: remains safe.

## Testing

Add focused tests around the local contract:

- crash reporting remains unavailable/no-op without Sentry support
- settings default off and persistence still work
- DSN compile-definition plumbing is represented in CMake
- invalid or missing DSN paths are safe
- repeated init/shutdown remains safe

Workflow validation should use `actionlint` and targeted Docker builds/tests.
Full platform release validation happens through manual or tag-triggered release
workflows only.

## Rollout

1. Add Sentry wiring in CMake and app-facing availability state.
2. Update release workflows to pass `SENTRY_DSN` only when present.
3. Update Settings UI copy/state so unavailable builds are clear.
4. Add or adjust tests.
5. Document the GitHub secret and release behavior.

## Non-Goals

- Automatic diagnostics or log uploads.
- Product analytics.
- Session replay.
- Crash reporting in ordinary PR/merge CI artifacts.
- Replacing the existing diagnostics export flow.
