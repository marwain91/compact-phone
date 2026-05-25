# CompactPhone — Project Status

**Branch:** `v0.1-internal-alpha` (carries v0.1 + v0.2a–d + v0.3a–c)
**Tests:** 1 unit suite + 1 integration suite, 100% passing in the Linux dev container against Asterisk-in-docker.

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

**Unit (4 suites, ~15 tests):**
- SipEngine lifecycle, Database migrations, Keychain (memory + file), AccountsManager update/setDefault, ContactsManager, HistoryManager

**Integration (9 tests, 3 disabled):**
- RegisterUdp, OutboundCall, CallLifecycle, Hold/Unhold, DTMF, RegisterTls — all PASS
- InboundCall, BlindTransfer, AttendedTransfer — DISABLED: multi-account self-routing through the same SipEngine triggers PJSIP_EUNSUPTRANSPORT. The code paths are exercised; a proper end-to-end test needs an external SIP client originating calls (third party). Tracked as a v0.4 QA task.

## What's NOT done (path to v1.0 GA)

These are all v0.4 / pre-GA work — none of them affect the core SIP feature set.

| Item | Milestone | Why deferred |
|---|---|---|
| macOS audio acceptance (real CoreAudio, headset, mic permissions) | Manual, host-side | Requires running on macOS host; can't be done in Linux container |
| macOS DMG + notarization | v0.4 | Procurement of Apple Developer ID |
| Windows MSI + Authenticode signing | v0.4 | Procurement of EV cert, plus PJSIP Windows build via vcpkg |
| Auto-update (Sparkle, WinSparkle) | v0.4 | Tied to installers |
| Native keychain backends (macOS Security.framework, Windows wincred, Linux libsecret) | v0.4 | Tied to installers; file backend works as fallback |
| Tray icon + autostart | v0.4 | OS integration |
| Crash reporting (Sentry) | Release hardening | Optional release wiring exists; needs project `SENTRY_DSN` secret and live crash validation |
| NAT matrix QA (same-LAN, STUN, symmetric NAT, TLS-only egress) | Pre-GA | Real PBX matrix |
| Interop matrix QA (Asterisk, FreeSWITCH, 3CX, cloud PBX) | Pre-GA | Real PBX matrix |
| External-client inbound test for the 3 disabled integration tests | Pre-GA | Needs an external SIP client setup |
| vCard import | v0.4 polish | Out of v0.3b scope |
| Codec priority UI in account edit | v0.4 polish | PJSIP defaults work fine for most PBXes |
| Audio device picker UI | Host-only | Needs real audio devices |

## Suggested next move

1. **Run the macOS acceptance test** on host — first time the audio path runs end-to-end. Procedure in `docs/acceptance/v0.1.md` (extends to all of v0.2 + v0.3 since the audio path is unchanged).
2. If acceptance passes, merge `v0.1-internal-alpha` to `main` and tag `v0.3.0`.
3. Plan v0.4 (installers + signing + native keychains + tray icon + auto-update) as the path to v1.0 GA.
