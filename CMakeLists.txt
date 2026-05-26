cmake_minimum_required(VERSION 3.21)

project(CompactPhone
    VERSION 0.1.0
    DESCRIPTION "Free multiplatform SIP softphone (Sponsored by CompactPhone)"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

option(COMPACTPHONE_BUILD_TESTS "Build unit and integration tests" ON)
option(COMPACTPHONE_ENABLE_TRAY "Enable system tray integration when Qt Widgets is available" ON)
option(COMPACTPHONE_ENABLE_SENTRY "Link the Sentry crash reporter (requires sentry-native via vcpkg)" OFF)
set(COMPACTPHONE_SENTRY_DSN "" CACHE STRING "Sentry DSN embedded at build time; empty disables uploads")

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2 Test)
# LinguistTools is required only to regenerate translation files; without it
# the .qm fallback resources still ship, just without lupdate/lrelease at
# build time.
find_package(Qt6 6.5 QUIET COMPONENTS LinguistTools)
if(Qt6LinguistTools_FOUND)
    set(COMPACTPHONE_WITH_LINGUIST ON)
else()
    set(COMPACTPHONE_WITH_LINGUIST OFF)
    message(STATUS "Qt6 LinguistTools not found; skipping translation regeneration. Install qttools via vcpkg to enable.")
endif()
set(COMPACTPHONE_WITH_TRAY OFF)
if(COMPACTPHONE_ENABLE_TRAY)
    find_package(Qt6 6.5 QUIET COMPONENTS Widgets)
    if(Qt6Widgets_FOUND)
        set(COMPACTPHONE_WITH_TRAY ON)
    else()
        message(STATUS "Qt6 Widgets not found; building without system tray integration")
    endif()
endif()
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PJSIP REQUIRED)
find_package(spdlog CONFIG REQUIRED)

qt_standard_project_setup()

# ---------------------------------------------------------------------------
# Licence-compliance file plumbing
#
# GPL-3.0-or-later (§5/§6) and LGPLv3 §4 both require that the licence
# notices and the "where to get source" information travel with the binary.
# We do that two ways:
#
#   1. macOS: src/CMakeLists.txt attaches these files to the .app bundle under
#      Contents/Resources, so they end up inside the .dmg by construction.
#   2. install(): emit them to ${CMAKE_INSTALL_PREFIX} so packagers (the
#      Windows WiX harvest, Linux .deb / AppImage scripts) pick them up
#      automatically.
# ---------------------------------------------------------------------------
set(COMPACTPHONE_LICENCE_FILES
    "${CMAKE_SOURCE_DIR}/LICENSE"
    "${CMAKE_SOURCE_DIR}/THIRD_PARTY_LICENSES.md"
)

add_subdirectory(src)

if(COMPACTPHONE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(GNUInstallDirs)
install(FILES ${COMPACTPHONE_LICENCE_FILES}
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
    COMPONENT licences
)
