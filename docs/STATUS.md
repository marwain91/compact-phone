# CompactPhone — Project Status

**Branch:** `main` preparing first public release `v0.1.0`.
**Tests:** 193 CTest cases passing in the Linux dev container: 169 unit/static-contract tests + 24 Asterisk-backed integration tests.

## What's done

### Core SIP (v0.1, v0.2)
- Qt 6 + QML + C++17 + PJSIP, Linux dev container + macOS host build path
- Multi-account registration over **UDP, TCP, TLS** (with SRTP optional/required/disabled per account)
- Per-account TLS verify (strict by default, `allowUntrustedCert` opt-out functional via dedicated per-account TLS transports)
- Outbound + inbound calls
- Hold / unhold, mute, DTMF (RFC 2833 and SIP INFO per account)
- Call lifecycle leak fix (post-disconnect queued cleanup)

### Call control (v0.3a)
- Blind transfer (REFER)
- Attended transfer (REFER with Replaces)
- Call waiting — second incoming rings, accepting auto-holds the previous call
- Audible ringtone (bundled WAV via Qt resources, extracted to AppData on first run)
- Transfer-result snackbar

### Productivity (v0.3b)
- Local contacts (SQLite-backed CRUD, click-to-dial)
- Call history (auto-logged on disconnect, click-to-redial)

### Settings + polish (v0.3c)
- Settings KV store (SQLite), Settings pane (log level, ringtone toggle)

### Build / dev infrastructure
- vcpkg manifest mode (Qt6, OpenSSL, spdlog, gtest, sqlite3)
- PJSIP built from upstream tarball in the dev container Dockerfile
- Linux dev container with Asterisk fixture (UDP + TLS transports, self-signed test cert)
- GitHub Actions CI matrix (Linux dev container + macOS native + Windows placeholder)
- Encrypted file-based keychain (AES-256-GCM via OpenSSL EVP)

## Test suite

**Unit/static (169 tests across 3 executables):**
- SIP engine lifecycle, database migrations, keychains, account/call/contact/history/message/line/settings managers, models, provisioning, update checks, QML layout contracts, and release-packaging contracts.

**Integration (24 tests):**
- Register, outbound/inbound call flow, hold/unhold, DTMF, TLS registration, transfers, forwarding, conference, recording, instant messages, and call policies — all PASS against the Asterisk fixture.

## What's NOT done (path to v1.0 GA)

These are all v0.4 / pre-GA work — none of them affect the core SIP feature set.

| Item | Milestone | Why deferred |
|---|---|---|
| macOS audio acceptance (real CoreAudio, headset, mic permissions) | Manual, host-side | Requires running on macOS host; can't be done in Linux container |
| Signed/notarized macOS production DMG | v0.4 | Procurement of Apple Developer ID and release secrets |
| Signed Windows MSI | v0.4 | Procurement of EV/OV certificate and Authenticode release wiring |
| Auto-update (Sparkle, WinSparkle) | v0.4 | Tied to installers |
| Autostart/login item polish | v0.4 | OS integration |
| Crash reporting (Sentry) | Release hardening | Optional release wiring exists; needs project `SENTRY_DSN` secret and live crash validation |
| NAT matrix QA (same-LAN, STUN, symmetric NAT, TLS-only egress) | Pre-GA | Real PBX matrix |
| Interop matrix QA (Asterisk, FreeSWITCH, 3CX, cloud PBX) | Pre-GA | Real PBX matrix |
| External-client inbound test for the 3 disabled integration tests | Pre-GA | Needs an external SIP client setup |
| vCard import | v0.4 polish | Out of v0.3b scope |
| Codec priority UI in account edit | v0.4 polish | PJSIP defaults work fine for most PBXes |
| Audio device picker UI | Host-only | Needs real audio devices |

## Suggested next move

1. **Run the macOS acceptance test** on host — first time the audio path runs end-to-end. Procedure in `docs/acceptance/v0.1.md` (extends to all of v0.2 + v0.3 since the audio path is unchanged).
2. If acceptance passes and release secrets are ready, tag `v0.1.0`.
3. Plan post-0.1 hardening: signing/notarization, auto-update, NAT/interop QA, and remaining OS integration.
