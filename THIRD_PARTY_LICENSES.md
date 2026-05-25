# Third-Party Licences

Compact Phone is © 2026 Jiri Havlicek and is distributed under the
**GNU General Public License v3.0 or later** (see [`LICENSE`](LICENSE)).

The Compact Phone binary distribution incorporates or links against the
following third-party software. Each component is used under the terms
of its own licence, listed below. The full text of each licence is
either included with the upstream project's source tarball (linked
below) or, for short licences, reproduced here verbatim.

If you received a binary build of Compact Phone and want the
corresponding source code — for any of the components below, or for
Compact Phone itself — you can obtain it at:

> **<https://github.com/marwain91/compact-phone>**

The release tag matching the version shown in the About dialog
(e.g. `v0.1.0`) reproduces the exact source used to build that binary.
The licensee Jiri Havlicek also offers, for at least three years from
the date of distribution of any binary release, to provide a copy of
the corresponding source code on a physical medium for a charge no more
than the cost of physically performing the source distribution, in
accordance with GPLv3 §6(b). Contact details for this offer are in
[`README.md`](README.md).

---

## Compact Phone (first-party source)

- **Copyright:** © 2026 Jiri Havlicek
- **Licence:** GNU GPL v3.0 or later (`GPL-3.0-or-later`)
- **Full text:** [`LICENSE`](LICENSE)

Unless an individual file carries a different notice, every source file
under `src/`, `tests/`, `cmake/`, `tools/`, `packaging/`, `docs/`, and
`web/` is part of Compact Phone and is licensed GPLv3-or-later.

---

## Qt 6 (qtbase, qtdeclarative, qtquickcontrols2)

- **Upstream:** <https://www.qt.io/> · source: <https://download.qt.io/archive/qt/>
- **Licence used here:** GNU LGPL v3.0 (`LGPL-3.0-only`), with parts also
  under GPLv2, GPLv3 and the Qt Commercial Licence. Compact Phone uses
  Qt under **LGPLv3 only**.
- **Compliance notes (LGPLv3 §4):**
  - Compact Phone links Qt **dynamically** — Qt shared libraries (`Qt6Core`,
    `Qt6Gui`, `Qt6Network`, `Qt6Qml`, `Qt6Quick`, `Qt6QuickControls2`,
    `Qt6Widgets`) are shipped alongside the executable. Users may
    substitute their own modified build of Qt by replacing those shared
    libraries inside the install directory; the application will load
    the replacement at next launch.
  - The LGPLv3 text is reproduced upstream at
    <https://www.gnu.org/licenses/lgpl-3.0.txt>. The full source of Qt
    is available from <https://download.qt.io/archive/qt/> matching the
    version reported by the Compact Phone About dialog.
  - LGPLv3 incorporates the anti-circumvention clause of GPLv3 §3;
    Compact Phone does not impose any technological measure that
    prevents users from exercising their LGPL rights.

## PJSIP / pjproject (pjlib, pjlib-util, pjnath, pjmedia, pjsip, pjsua2)

- **Upstream:** <https://www.pjsip.org/> · source: <https://github.com/pjsip/pjproject>
- **Licence used here:** GNU GPL v2.0 or later (`GPL-2.0-or-later`).
  A commercial PJSIP licence from Teluu Ltd. also exists; Compact Phone
  **does not** use it — only the GPL option.
- **Compliance notes:** GPLv2 is compatible with the GPLv3 under which
  Compact Phone is distributed (GPLv3 explicitly permits combination
  with GPLv2-or-later). Source for the exact pjproject version used at
  build time is referenced from the Compact Phone build manifest
  (`cmake/FindPJSIP.cmake` and the development-container Dockerfile).
- **Bundled third-party in pjproject:** pjproject's `third_party/`
  directory contains additional libraries (Speex, GSM, G.722, ilbc,
  resample, SRTP, WebRTC AEC, etc.) under their own permissive or
  BSD-style licences. Their notices are included in the pjproject
  source tree at <https://github.com/pjsip/pjproject/tree/master/third_party>.

## OpenSSL

- **Upstream:** <https://www.openssl.org/> · source: <https://github.com/openssl/openssl>
- **Licence used here:** Apache License 2.0 (`Apache-2.0`), applicable to
  OpenSSL ≥ 3.0. Earlier OpenSSL 1.x releases used the dual
  OpenSSL/SSLeay licence; Compact Phone targets OpenSSL ≥ 3.0 via
  vcpkg's `openssl` port.
- **Compliance notes:** Apache-2.0 is GPLv3-compatible. The full Apache
  2.0 text is at <https://www.apache.org/licenses/LICENSE-2.0.txt>.

## spdlog

- **Upstream:** <https://github.com/gabime/spdlog>
- **Licence:** MIT (`MIT`)
- **Notice:** Copyright (c) 2016 Gabi Melman.
- **Compliance notes:** MIT requires that the copyright notice and
  permission notice be included in all copies or substantial portions
  of the software. This file satisfies that requirement.

```
Copyright (c) 2016 Gabi Melman.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.
```

## SQLite

- **Upstream:** <https://www.sqlite.org/>
- **Licence:** Public domain (`blessing` / `Unlicense`-equivalent).
- **Notice:** The SQLite source code is dedicated to the public domain
  by its authors. See <https://www.sqlite.org/copyright.html>.

## GoogleTest *(test-only — not redistributed in binary releases)*

- **Upstream:** <https://github.com/google/googletest>
- **Licence:** BSD-3-Clause
- **Compliance notes:** GoogleTest is linked only into the unit-test
  binaries under `tests/`. It is not part of any redistributed
  end-user binary. Its licence text is reproduced in the upstream
  repository.

## Sentry SDK *(optional, off by default)*

- **Upstream:** <https://github.com/getsentry/sentry-native>
- **Licence:** MIT
- **Compliance notes:** Only included when the build option
  `COMPACTPHONE_ENABLE_SENTRY` is enabled. Release workflows enable it
  only when the repository has a `SENTRY_DSN` secret configured, and the
  app still requires explicit user opt-in before sending crash reports.

## Fonts and branding assets

- The Compact Phone wordmark, app icon, and ringtone bundled at
  `src/resources/ringtone.wav` (synthesised at build time by
  `src/resources/generate_ringtone.py`) are © 2026 Jiri Havlicek and
  released under the same GPLv3-or-later as the rest of Compact Phone.

---

## Notes on platform integrations

- **macOS:** Compact Phone links against Apple's `AppKit`,
  `Foundation`, and `Security` frameworks via the public macOS SDK.
  These are used under Apple's standard SDK terms and are not
  redistributed.
- **Windows:** Compact Phone links against the standard Win32 SDK and
  the Microsoft Visual C++ runtime (redistributed via the standard MS
  redistributable merge module under Microsoft's redistribution licence).

---

If you spot a missing attribution or a mis-stated licence, please open
an issue at <https://github.com/marwain91/compact-phone/issues> — we
take this seriously.
