# Overlay triplet: shadows vcpkg's built-in x64-linux. Same in every
# respect except VCPKG_BUILD_TYPE=release, which skips the Debug
# variant of every dependency. See tools/dev/triplets/README.md.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_BUILD_TYPE release)
