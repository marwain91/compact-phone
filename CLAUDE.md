# CLAUDE.md — orientation for future sessions

This file gives you what the codebase itself can't: which gotchas to
expect, where the load-bearing decisions live, and which paths look
obvious-but-are-traps. Read once at session start.

## What this is

**Compact Phone** — a Qt6 + QML desktop SIP softphone using PJSIP /
PJSUA2 as the audio stack. Targets macOS, Windows, Linux. GPL-3.0-or-
later because PJSIP forces it. Solo-maintained, public repo at
`github.com/marwain91/compact-phone`. Active branch is `main`;
`v0.1-internal-alpha` keeps the granular pre-public history archived.

## Tree map (the non-obvious parts)

```
src/core/                  C++ controllers + managers — the brain
  provisioning/            Pluggable auto-provisioning (Daktela today)
src/ui/qml/                QML views; Main.qml is the window shell
src/ui/qml/components/     AppSwitch, AppIcon, AppComboBox etc. — read
                           these before adding new control wrappers
tools/dev/                 Linux dev container (Dockerfile + compose)
tools/dev/triplets/        Release-only vcpkg triplets for Linux (see below)
tools/release/             Release-time tooling — appcast generator etc.
cmake/FindPJSIP.cmake      Locates PJSIP per-platform — pkg-config on
                           Linux/macOS, env-var glob on Windows
.env.local.example         Template for remote-build VPS hostname
                           (real .env.local is gitignored)
web/docs/pages/            Markdown docs MyWebs pulls into the live site
docs/download-urls.md      Stable public download URLs the MyWebs
                           landing page hardcodes
```

## Build paths

| Target | Command | Where it runs | Why |
|---|---|---|---|
| Linux dev / CI | `make build` | Local Docker dev container | Headless, reproducible |
| macOS native | `make build-macos` | Host (needs Xcode, brew deps, native PJSIP) | Only way to produce `.app` |
| Linux on a VPS | `make remote-build` | Docker on VPS (rsync + ssh) | Heavy compiles off the laptop |
| Windows MSI | GitHub Actions only | `windows-2022` runner | No good local-on-Mac option |
| Tests | `make test` (Linux) / `ctest` (Mac) | Same context as the build | — |

## Things that surprised me; will surprise you

### vcpkg binary cache already works — but per-triplet

The default vcpkg binary cache at `~/.cache/vcpkg/archives/` is
populated and reused across builds **on the same triplet**. macOS uses
`arm64-osx`, Linux dev container uses `arm64-linux` (Apple Silicon
host) or `x64-linux` (Intel/AMD64), Windows uses `x64-windows`. None
of these caches transfer to the others.

If you see vcpkg "rebuilding Qt" unexpectedly, the cause is almost
never "no cache" — it's "your triplet's ABI hash changed". Common
triggers: `vcpkg.json` feature-list edit, vcpkg baseline bump,
overlay triplet added (the release-only fix below).

### Release-only vcpkg triplets

`tools/dev/triplets/{x64,arm64}-linux.cmake` shadow vcpkg's
built-ins with `set(VCPKG_BUILD_TYPE release)`. Without this, a
cold Linux build of `qtdeclarative` peaks at ~30 GB of buildtree
(Debug variant of every Qt sublibrary, with `-g3`, ~350 MB per PCH
file × 19 sublibraries). With it, peak is ~10 GB.

The Linux CMake preset wires the overlay via `VCPKG_OVERLAY_TRIPLETS`.
macOS release/manual validation uses `tools/release/triplets/arm64-osx.cmake`
through `vcpkg-configuration.json` so vcpkg cannot silently ignore it during
manifest install. That triplet also sets `VCPKG_OSX_DEPLOYMENT_TARGET=12.0`.
Windows is not overlaid; the binary is release anyway.

### PJSIP is built out-of-band on every platform — there is no vcpkg port

PJSIP is not in vcpkg upstream. We build it manually:

- **Linux dev container**: `tools/dev/Dockerfile` calls `./aconfigure
  --prefix=/opt/pjproject ... && make install`. The PJSIP `.pc` file
  ends up at `/opt/pjproject/lib/pkgconfig` and our
  `FindPJSIP.cmake` picks it up via `pkg-config`.
- **macOS host**: `make pjsip-macos` does the same, installing to
  `$HOME/pjproject/`. CI's `build-macos` job inlines the same steps.
- **Windows runner**: `.github/workflows/ci.yml` `build-windows`
  downloads PJSIP and msbuilds `pjproject-vs14.sln` against the
  v143 toolset. `PJSIP_ROOT` env var is exported; `FindPJSIP.cmake`
  globs `.lib` files from the per-component lib dirs.

Watch out for: a stale `windows-pjsip` feature in `vcpkg.json` (now
removed) referenced a non-existent vcpkg port. Don't add it back.

### `libb2` needs `autoconf-archive` — in every environment that runs vcpkg cold

vcpkg's `libb2` portfile runs `autoreconf`, which needs `AX_*` m4
macros that ship in the `autoconf-archive` package — NOT the base
`autoconf`. Missing this manifests as a cryptic libb2 BUILD_FAILED.

- `tools/dev/Dockerfile` apt-installs it
- `.github/workflows/ci.yml`'s macOS job `brew install`s it
- `.github/workflows/release-macos.yml`'s `Install build deps` step
  also needs it (PR #22 added it after the first release tag failed
  cold on this exact thing — `ci.yml` had it but `release-macos.yml`
  had drifted)
- Linux CI job builds the dev container so it inherits the Dockerfile fix

If you add a new build environment, install `autoconf-archive` or
expect libb2 to fail on first cold install. **If you add a new
release workflow, mirror this brew/apt install** — release workflows
don't inherit from `ci.yml` and drift is easy.

### gitleaks-action v2 needs `GITHUB_TOKEN`, not `GH_TOKEN`

`.github/workflows/secret-scan.yml` passes `GITHUB_TOKEN`. The action
silently ignores `GH_TOKEN` and fails on every PR with "GITHUB_TOKEN
is now required to scan pull requests" if you change it.

### Default ssh + non-interactive => first-time host key rejection

`ssh almalinux@new-host hostname` (with a command argument)
non-interactively rejects unknown host keys without prompting. Run
`ssh almalinux@new-host` (no command) once interactively to accept
the key, then non-interactive commands work. The `make remote-*`
targets fall into this trap; the troubleshooting note in
`tools/dev/remote-builds.md` covers it.

### Disk-pressure trap on shared VPSes

A cold Linux PJSIP+Qt build on a tight-disk VPS hits ENOSPC during
qtdeclarative if you don't use the release-only triplet. vcpkg keeps
the debug buildtree resident while the release variant builds, so
peak disk is debug + release simultaneous (~22 GB combined for
qtdeclarative alone).

If you're remote-building somewhere that might be production-shared,
warn before launching; check `df -h` first.

### Windows MAX_PATH (260 chars) — moves vcpkg + buildDir to short paths

qtdeclarative's nested buildtree paths blow past Windows' 260-char
MAX_PATH limit when vcpkg installs into the default
`${buildDir}/vcpkg_installed/` under the GitHub workspace
(`D:\a\compact-phone\compact-phone\...`). `cl.exe` and CMake helpers
silently fail with `Cannot open compiler generated file: '': Invalid
argument` mid-Configure.

Three coordinated fixes in `.github/workflows/ci.yml`'s Windows job:

- Clone vcpkg into **`C:\v`** (not the workspace)
- Clone PJSIP source into **`C:\p`** (same reason)
- Override the entire CMake build dir to **`C:\b`** via
  `cmake --preset windows -B C:\b` — this also moves vcpkg_installed
  to `C:\b\vcpkg_installed`

`PJSIP_ROOT`, `VCPKG_ROOT`, and the test/stage/MSI steps all hardcode
these short paths. Don't move them back without verifying the path
budget.

### Windows runner has both MSVC and MinGW gcc — CMake picks the first in PATH

The `windows-2022` runner ships both. CMake's auto-detect picks
whichever appears first; default is MinGW gcc 14. vcpkg deps are
built with MSVC, and linking MinGW-built compactphone against MSVC
Qt is an ABI mismatch — and triggers PJSIP's non-MSVC codepath
(`pj/compat/string.h` requires `wcsicmp` etc. PJSIP doesn't ship).

Fix: run `ilammy/msvc-dev-cmd@v1` with `arch: amd64` BEFORE the
cmake configure step. It runs `vcvarsall.bat` and prepends MSVC's
bin to PATH for the rest of the job.

### `setup-msbuild` adds VS's bundled vcpkg to PATH; force our toolchain

`microsoft/setup-msbuild@v2` (which we need for PJSIP's msbuild step)
also adds VS's bundled `C:\Program Files\...\VC\vcpkg\` to PATH. That
vcpkg's `scripts/buildsystems/vcpkg.cmake` then wins over our
`C:\v\scripts\buildsystems\vcpkg.cmake` despite our `VCPKG_ROOT=C:\v`.

Pass `-DCMAKE_TOOLCHAIN_FILE=C:/v/scripts/buildsystems/vcpkg.cmake`
explicitly on the cmake invocation so our vcpkg always wins.

### PJSIP + Qt on Windows — `PJ_NATIVE_STRING_IS_UNICODE=0`

Qt defines `UNICODE` / `_UNICODE` on Windows (its APIs use 16-bit
chars for Win32 compatibility). PJSIP's `pj/compat/string.h` sees
those defines and enables an internal wide-char codepath that
requires `wcsicmp` / `wcsdup` / etc. wrappers PJSIP doesn't ship by
default. `#error "Implement Unicode string functions"` fires.

Workaround in `cmake/FindPJSIP.cmake`'s Windows branch:
```cmake
target_compile_definitions(PJSIP::PJSIP INTERFACE
    PJ_NATIVE_STRING_IS_UNICODE=0)
```

PJSIP then treats its internal strings as ANSI regardless of the
host app's Unicode posture. Qt is unaffected (still uses QString).

### PJSIP on Windows — must build with `/MD`, not the Release-Static default `/MT`

PJSIP's `pjproject-vs14.sln` `Release-Static` configuration hardcodes
`<RuntimeLibrary>MultiThreaded</RuntimeLibrary>` (= `/MT`, static CRT)
in every `.vcxproj`. Qt + our code link against `/MD` (dynamic CRT —
vcpkg's default, also MSVC's modern default). MSVC refuses to mix
the two — link fails with:

```
pjsua2-lib-...-Release-Static.lib(types.obj) : error LNK2038:
  mismatch detected for 'RuntimeLibrary': value 'MT_StaticRelease'
  doesn't match value 'MD_DynamicRelease' in test_register_udp.cpp.obj
```

`msbuild /p:RuntimeLibrary=MultiThreadedDLL` does NOT fix it because
PJSIP's vcxproj sets the property explicitly per ItemDefinitionGroup,
which overrides command-line globals. Fix: patch all the `.vcxproj`
files in-place before invoking msbuild:

```pwsh
Get-ChildItem pjproject-2.17 -Recurse -Filter *.vcxproj | ForEach-Object {
    (Get-Content $_.FullName -Raw) `
        -replace '<RuntimeLibrary>MultiThreaded</RuntimeLibrary>', `
                 '<RuntimeLibrary>MultiThreadedDLL</RuntimeLibrary>' `
        | Set-Content -NoNewline $_.FullName
}
```

See `.github/workflows/ci.yml`'s `Build PJSIP (at C:\p)` step.

Knock-on: bump the PJSIP cache key (e.g. `-v3-md`) whenever you
change build flags that affect symbol/CRT linkage — otherwise the
old cache shadows the fix.

### `actions/cache@v4` does NOT save on job failure by default

For expensive caches (Windows Qt cold build is ~2h), use
`save-always: true` so a failed compile downstream doesn't throw
away cache progress. Otherwise every iteration on a failing build
redoes the cold work.

Also cache `vcpkg_installed/`, not just `vcpkg/` root — see
`.github/workflows/ci.yml` Windows job for the path list.

### macOS Dock tooltip uses bundle directory name, not Info.plist

Setting `CFBundleName` / `CFBundleDisplayName` /
`QGuiApplication::setApplicationDisplayName` to "Compact Phone"
does NOT fix the Dock tooltip if the .app directory itself is
`compactphone.app` (lowercase). macOS' Dock reads the basename.

Fix in `src/CMakeLists.txt`:
```cmake
set_target_properties(compactphone PROPERTIES
    OUTPUT_NAME "Compact Phone"
    MACOSX_BUNDLE_BUNDLE_NAME "Compact Phone")
```

CMake then writes `build/macos/src/Compact Phone.app/Contents/MacOS/
Compact Phone` — binary name with a space is the Apple convention
(QuickTime Player, Photo Booth all do this). Nothing in the codebase
shells out to invoke the binary by path, so spaces are fine.

Knock-on: Makefile `run-macos`, packaging/macos/build-dmg.sh, and
release-macos.yml all hardcode the new path "Compact Phone.app".

### Qt QQC2 `Switch` has padding, insets, *and* contentItem all claiming space

To make `AppSwitch` sit flush right in a `RowLayout`, you need to
zero ALL of:
- `padding`, `leftPadding`, `rightPadding`, `topPadding`, `bottomPadding`
- `topInset`, `bottomInset`, `leftInset`, `rightInset`
- `contentItem`'s `implicitWidth` / `implicitHeight`

AND set `Layout.maximumWidth: 36` (the indicator's actual width) so
`RowLayout` can't give it slack. See `src/ui/qml/components/AppSwitch.qml`
— the comment block on top of the file enumerates everything that
took two iterations to figure out.

### `AppIcon` inside `RowLayout` — use `Layout.preferredWidth/Height`, not `width/height`

`AppIcon` is an `Image`. Inside a `RowLayout` the layout uses
`Layout.preferred*` for sizing; bare `width: 14; height: 14` get
ignored and the SVG's intrinsic size leaks through. Different icons
have different intrinsic bounds, so a row of "same-sized" icons
renders at very different sizes.

Use:
```qml
AppIcon {
    Layout.preferredWidth: 16
    Layout.preferredHeight: 16
    Layout.alignment: Qt.AlignVCenter
}
```

### Auto-update appcast lives at `/releases/latest/download/appcast-<os>.xml`

The in-tree `UpdateChecker` is a custom Qt-based fetcher that parses
Sparkle 2.x XML but **does not verify signatures**. The feed URL is
platform-specific in `src/core/UpdateChecker.cpp` via `#ifdef`:

- macOS: `.../releases/latest/download/appcast-macos.xml`
- Windows: `.../releases/latest/download/appcast-windows.xml`
- Linux: `""` (no signed release path; `check()` no-ops cleanly)

`tools/release/generate-appcast.py` builds the XML; the
`release-{macos,windows}.yml` workflows call it and upload the
appcast as a release asset alongside the DMG/MSI. Optional EdDSA
signing via the `SPARKLE_ED25519_PRIVATE_KEY` Actions secret —
forward-compat with the real Sparkle framework if we ever adopt it.

### Stable release filenames so the landing page can hardcode URLs

Release artifacts use **version-less filenames** —
`Compact-Phone-macOS.dmg` and `Compact-Phone-Windows.msi`. GitHub's
`/releases/latest/download/<filename>` only resolves when the filename
matches exactly, so version-stamped names would 404 the landing-page
buttons after every release. See `docs/download-urls.md` for the full
URL list and HTML snippets MyWebs uses.

### Main merges must not start expensive build workflows

`ci.yml`'s `on:` is `pull_request:` only. Earlier it also had
`push: branches: [main]` but that was removed in #24: squash-merge +
protected main + required PR CI means every merge commit's source is
identical to what PR CI already validated. Running CI again on every
main push just burns runner time we need free for tag-triggered release
builds — and macOS pool contention is real (see next note).

Do not add `push: branches: [main]` to workflows that build Docker
images, vcpkg dependencies, app binaries, or platform packages. Main
merges may run cheap checks such as secret scanning or docs dispatch,
but expensive builds should run only on release tags (`v*`) or explicit
`workflow_dispatch` when someone intentionally asks for them. The
`linux-builder-image.yml` workflow is manual for this reason.

Re-add the push trigger only if we ever take in non-squash merges
or run CI-bypassing admin merges.

### GitHub-hosted `macos-14` pool contention — bump to `macos-15`

Free-tier `macos-14` (Sonoma, Apple Silicon) runners are heavily
oversubscribed across all of public-repo GitHub Actions. Multi-hour
queue waits are normal during EU/US workday overlap; release jobs
have sat queued 3+ hours before pickup.

`macos-15` (Sequoia) is also arm64 / Apple Silicon, sits in a
separate, less-contended runner pool. Both CI and `release-macos.yml`
target `macos-15` since #21. Trade-off: vcpkg's content-addressed
binary cache may need a one-time cold rebuild if the macOS 15 SDK
changes any port's ABI hash.

Don't switch to `macos-13` (Intel) — we ship arm64 DMGs, and
building x86_64 there means a separate `lipo`-and-merge step to
produce a universal binary. Not worth it just to reduce queue time.

### Linux x86_64 unit test segfaults — OpenSSL 3.0.2 `EVP_PKEY_HKDF` bug + pjsua2 audio init

Two distinct root causes, both Linux-x86_64-only (arm64 dev container
on Apple Silicon never reproduces — different libcrypto patch level).

**Cause 1 — OpenSSL 3.0.2 provider registry bug** (FIXED in #23/#25).
`KeychainFile.*`, `KeychainFactoryTest.*`, `AccountsManagerUpdateTest.*`
all SIGSEGV inside `EVP_KEYMGMT_names_do_all` when called via the
legacy `EVP_PKEY_HKDF` → `EVP_PKEY_derive_init` path. Ubuntu 22.04's
`libcrypto.so.3` is OpenSSL 3.0.2 which has a known race/init bug in
that code path. Fix: use the modern `EVP_KDF_fetch("HKDF", ...)` /
`EVP_KDF_derive` API instead — same crypto output (HKDF-SHA256), but
doesn't traverse the buggy keymgmt walker. See
`src/core/platform/Keychain_file.cpp::deriveKey`.

Diagnostic that pinpointed it: add `gdb` to `tools/dev/Dockerfile`
(see `apt-get install -y ... gdb`), then run failing tests under
`gdb --batch -ex run -ex 'bt full' -ex 'info sharedlibrary'`. The
backtrace immediately showed `libcrypto.so.3` frames — Qt and our
code had nothing wrong.

**Cause 2 — pjsua2 audio init** (still open).
`SipEngineLifecycle.*` still SIGSEGVs because pjsua2's audio backend
init crashes even with the null-dev fallback in the headless
container. CI workaround: `ctest --exclude-regex 'SipEngineLifecycle'`
in `ci.yml`. Real fix probably needs either a gtest fixture that
initialises pjsua2 once per process, or moving those tests to the
integration suite that only runs on a real desktop.

**Why local arm64 dev container doesn't reproduce either**: the
Apple-Silicon Debian/Ubuntu base ships a newer libcrypto patch, and
the audio device enumeration on Docker-for-Mac apparently doesn't
trip the pjsua2 init path the same way as on the GitHub x86_64
runner. So "tests pass on my machine" is not signal for these.

## Branch + PR conventions

- **`main` is protected** — direct pushes blocked, force-push blocked,
  deletion blocked. PR is the only way in.
- **Squash-merge is the only merge method.** Repo-level setting +
  ruleset-level `pull_request.allowed_merge_methods: ["squash"]`
  enforce this redundantly.
- **`required_linear_history`** keeps `main` a straight line.
- **`delete_branch_on_merge: true`** auto-deletes feature branches.
- PR title becomes the squash commit subject; PR body becomes the
  commit body. Write both with that in mind.
- **No required reviews** today (solo project). If a second
  maintainer joins, bump `required_approving_review_count` to 1.

When chaining work: stack PRs by basing one feature branch on
another. After the base merges, GitHub rebases the stacked PR onto
main automatically (or you do `git rebase main` and force-push).

## Things I avoid

- **Self-hosted GitHub Actions runners on this public repo.** Fork
  PRs can execute arbitrary code on the runner host — known supply-
  chain risk. The `make remote-*` flow uses plain SSH from the
  maintainer's machine instead.
- **Force-pushing `main`** — the protection ruleset blocks it; don't
  try to work around with admin bypass.
- **`--no-verify` on commits** — gitleaks pre-commit hook is the
  last line of defence against accidentally pushing secrets to a
  public repo. Always run it.

## Where to find more

- `tools/dev/remote-builds.md` — daily workflow for VPS offload
- `tools/dev/triplets/README.md` — why the release-only triplet, what
  we lose and don't lose
- `tools/release/generate-appcast.py` — Sparkle 2.x appcast generator
  for release-time auto-update feeds
- `docs/download-urls.md` — stable public URLs the MyWebs landing page
  hardcodes
- `web/docs/pages/*.md` — user-facing docs (configuration,
  provisioning, troubleshooting, FAQ)
- `Makefile` — every target has a `## help` line; `make help` lists them
