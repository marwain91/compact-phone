# Overlay triplets — release-only Linux deps

This directory holds vcpkg overlay triplets that **shadow** the
built-in `x64-linux` and `arm64-linux` triplets. They are identical
to vcpkg's defaults except for one line:

```cmake
set(VCPKG_BUILD_TYPE release)
```

That single line skips the **Debug variant** of every vcpkg-built
dependency (Qt, OpenSSL, spdlog, etc.) on Linux.

## Why

A full Linux Qt build (Debug + Release variants of every Qt module)
needs ~22 GB of disk for `qtdeclarative` alone — and another ~14 GB
for `qtbase`. On constrained VPSes this is fatal: a 21 GB-free build
host will run out partway through and the whole install fails.

Release-only builds halve that footprint (~10 GB peak for
qtdeclarative). vcpkg's documentation explicitly recommends this for
CI / build farms / constrained environments. The Qt project's own CI
uses release-only deps.

## What we lose

Stepping *into* Qt or OpenSSL source with a debugger no longer shows
source-line info — you get stripped symbols at the Qt-API boundary.
Your own code (`PhoneController::dial()`, `Main.qml:42`, ...) is
unaffected because it's compiled with `-g` regardless of the deps'
variant.

In practice, 99% of debugging happens in your own code; the API
boundary is enough to diagnose at. If you ever genuinely need
source-level Qt internals, comment out `VCPKG_BUILD_TYPE release`
in the matching triplet file and rebuild — you'll get the standard
debug variants back.

## What we do **not** lose

- Release binaries shipped to users — those were already
  release-only (the `release-macos.yml` / `release-windows.yml`
  workflows use Release config).
- macOS app symbols — macOS uses `tools/release/triplets/arm64-osx.cmake`
  for release-only dependencies, but the app itself still builds with
  `RelWithDebInfo`.
- Windows release builds — same, `x64-windows` is unaffected.

This overlay only narrows Linux deps. macOS release/manual validation
uses `tools/release/triplets/arm64-osx.cmake`, wired through
`vcpkg-configuration.json`, to avoid building Qt debug variants after
Apple clang crashed in `qtshadertools` debug codegen on macos-15.
Windows is untouched.

## How vcpkg finds these files

`CMakePresets.json` sets `VCPKG_OVERLAY_TRIPLETS` on the `linux`
preset to point at this directory. vcpkg's documented overlay rule
is: if a triplet with the same name exists in an overlay dir, the
overlay wins. So `x64-linux` here completely replaces the built-in.

Triplet name stays the same, so the binary cache namespace remains
shared with other vcpkg users for the *port* identity — but the ABI
hash is different (release-only vs default), so packages won't
collide.
