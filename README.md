# Compact Phone

A compact, open-source SIP softphone for macOS, Windows, and Linux.
Built on Qt 6 and PJSIP, GPL-3.0-or-later.

## Features

- Multiple SIP accounts over UDP / TCP / TLS with SRTP.
- Hold, mute, blind/attended transfer, 3-way conference, DTMF (RFC 2833
  or SIP INFO), call recording.
- BLF / presence, SIP MESSAGE, voicemail (MWI), call forwarding
  (always / on-busy / on-no-answer), do-not-disturb, auto-answer.
- Contacts, call history, click-to-dial (`sip:` / `sips:` / `tel:` /
  `callto:` URLs).
- Themes (light / dark / midnight / ivory / velvet), system tray,
  custom ringtone.
- Encrypted credential storage with native keychain backends on macOS
  and Windows (AES-256-GCM file fallback elsewhere).

## Install

Pre-built signed installers:
- **macOS** — `compactphone-<version>.dmg` (forthcoming on release).
- **Windows** — `compactphone-<version>.msi` (forthcoming on release).
- **Linux** — see "Build from source" below; AppImage on the roadmap.

## Build prerequisites

- CMake ≥ 3.25
- Ninja
- C++17 compiler (Xcode 15+, MSVC 19.38+, gcc 11+)
- [vcpkg](https://vcpkg.io) checked out; `VCPKG_ROOT` env var set

## Build

### macOS (Apple Silicon)

```bash
cmake --preset macos
cmake --build --preset macos
```

### Windows

```powershell
cmake --preset windows -DVCPKG_MANIFEST_FEATURES="windows-pjsip"
cmake --build --preset windows
```

### Linux (dev container)

```bash
make up       # start dev container + Asterisk fixture
make build
make test
```

## Boot-time provisioning

For MDM / GPO / Ansible deployments, accounts can be provisioned without
GUI clicks:

```bash
compactphone \
    --sip-server pbx.acme.com \
    --sip-user 1001 \
    --sip-password-file /etc/compactphone/1001.pass \
    --sip-transport tls \
    --sip-srtp mandatory \
    --replace-account
```

A JSON provisioning file at `/etc/compactphone/provisioning.json` covers
larger fleets. See [`docs/provisioning.md`](docs/provisioning.md).

For SIP client tests, `compactphone-headless` can use the same config/CLI
surface without starting QML:

```bash
compactphone-headless \
    --sip-server pbx.acme.com \
    --sip-user 1001 \
    --sip-password-file /etc/compactphone/1001.pass \
    --call sip:600@pbx.acme.com \
    --play-file /opt/compactphone/prompt.wav \
    --loop-play-file \
    --exit-after-call
```

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md) for the build / test / PR
workflow and [`SECURITY.md`](SECURITY.md) for responsible disclosure.

## License

Compact Phone is © 2026 Jiri Havlicek and is distributed under the
**GNU General Public License v3.0 or later** (see [`LICENSE`](LICENSE)).

The shipped binary also redistributes Qt 6 (LGPLv3), PJSIP (GPLv2+),
OpenSSL (Apache-2.0), spdlog (MIT) and SQLite (public domain). Their
notices and the "where to get source" information are in
[`THIRD_PARTY_LICENSES.md`](THIRD_PARTY_LICENSES.md) — please keep that
file alongside any binary you redistribute.

### Source availability (GPLv3 §6 / LGPLv3 §4)

The complete corresponding source code for any released binary is
available at <https://github.com/marwain91/compact-phone>. Each tagged
release (e.g. `v0.1.0`) reproduces the exact source used to build that
binary. The licensee also offers, for three years from the date of
distribution of any binary release, to provide the corresponding source
on a physical medium at no charge beyond the cost of physical
distribution — contact `jiri@havliczech.eu`.

Qt is linked **dynamically**; users may replace the bundled Qt shared
libraries with their own modified build to exercise their LGPLv3 §4
rights. Compact Phone applies no technological measure that would
prevent this.
