# Overlay triplet: shadows vcpkg's built-in arm64-linux. Same in
# every respect except VCPKG_BUILD_TYPE=release. See README.md.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_BUILD_TYPE release)
