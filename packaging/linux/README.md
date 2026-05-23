# Linux packaging

Compact Phone ships on Linux as a single-file `.AppImage`:

- `Compact-Phone-Linux-x86_64.AppImage` — currently targets glibc 2.39+
  distros (Ubuntu 24.04+, Debian 13+, Fedora 40+, Arch, etc.) without
  installation
- Statically links Qt 6, PJSIP, OpenSSL, libsrtp2, and vcpkg libraries;
  `linuxdeploy` gathers the remaining dynamic system dependencies
- No package-manager integration: users `chmod +x` and double-click

## Files

- `compactphone.desktop` — XDG desktop entry. linuxdeployqt picks this up
  to wire `Exec=`, `Icon=`, `Categories=`, etc. inside the AppImage
- (Icon comes from `branding/generated/icon_256x256.png` at build time)

## Build / publish

Done by `.github/workflows/release-linux.yml` on every `v*` tag push:

1. Spin up the dev container (`tools/dev/Dockerfile`) — same image used
   for CI Linux builds, so the binary is built against the same toolchain
2. `cmake --preset linux && cmake --build --preset linux`
3. Stage runtime into `AppDir/` (binary + desktop file + icon)
4. Invoke `linuxdeploy` to gather remaining dynamic `.so` deps and
   produce `Compact-Phone-Linux-x86_64.AppImage`
5. Upload to the GitHub Release matching the tag

The filename is **version-less** so the landing page can hardcode
`/releases/latest/download/Compact-Phone-Linux-x86_64.AppImage` —
matches the macOS DMG and Windows MSI pattern.

## glibc compatibility note

AppImages cannot bundle glibc, so binaries inherit the build host's glibc
symbol floor. Our release currently builds in the dev container, which is
Ubuntu 24.04 (glibc 2.39), so the AppImage requires glibc 2.39+ on the user
machine. That covers Ubuntu 24.04+, Debian 13+, Fedora 40+, Arch, and most
recent rolling distros, but excludes Ubuntu 22.04 / Debian 12 / older.

If broader compatibility is needed later, the build container can be
pinned to ubuntu:22.04 specifically for releases. See CLAUDE.md for
the Ubuntu version choice rationale.
