# Overlay triplet for macOS release/manual validation workflows. It matches
# vcpkg's built-in arm64-osx triplet, but skips Debug variants of dependencies.
# This avoids cold-building Qt's debug-only pieces in CI while leaving the app's
# own RelWithDebInfo symbols intact.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)
set(VCPKG_OSX_DEPLOYMENT_TARGET 12.0)
set(VCPKG_BUILD_TYPE release)
