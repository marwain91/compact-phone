# Overlay triplet for Windows release/manual validation workflows. It matches
# vcpkg's built-in x64-windows triplet, but skips Debug variants of dependencies.
# This keeps cold Qt builds from spending CI time on libraries we do not ship.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_BUILD_TYPE release)
