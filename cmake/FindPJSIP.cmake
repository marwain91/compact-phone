# Locates PJSIP via the pkg-config metadata installed by pjproject's autotools
# `make install`. The PJSIP build lives outside vcpkg (see tools/dev/Dockerfile
# for the dev-container build; on macOS host, see the build script under
# tools/macos/), because pjproject is not in the vcpkg registry and its
# build system does not cooperate cleanly with vcpkg's out-of-source pattern.
#
# Exposes a single imported target `PJSIP::PJSIP` linking pjsua2 and every
# transitive PJSIP component (including bundled third_party libs) plus the
# system libraries PJSIP requires (OpenSSL, ALSA on Linux, CoreAudio frame-
# works on macOS, winsock + dsound on Windows).

find_package(PkgConfig REQUIRED)

# Hint pkg-config at common install prefixes.
foreach(_hint "/opt/pjproject/lib/pkgconfig" "/usr/local/lib/pkgconfig")
    if(IS_DIRECTORY "${_hint}")
        set(ENV{PKG_CONFIG_PATH} "${_hint}:$ENV{PKG_CONFIG_PATH}")
    endif()
endforeach()

pkg_check_modules(_PJSIP QUIET libpjproject)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PJSIP
    REQUIRED_VARS _PJSIP_FOUND
    VERSION_VAR _PJSIP_VERSION
)

if(PJSIP_FOUND AND NOT TARGET PJSIP::PJSIP)
    add_library(PJSIP::PJSIP INTERFACE IMPORTED)
    target_include_directories(PJSIP::PJSIP INTERFACE ${_PJSIP_INCLUDE_DIRS})
    target_compile_options(PJSIP::PJSIP INTERFACE ${_PJSIP_CFLAGS_OTHER})

    set(_PJSIP_LINK_LIBRARIES ${_PJSIP_STATIC_LIBRARIES})
    if(TARGET OpenSSL::SSL)
        list(REMOVE_ITEM _PJSIP_LINK_LIBRARIES ssl)
    endif()
    if(TARGET OpenSSL::Crypto)
        list(REMOVE_ITEM _PJSIP_LINK_LIBRARIES crypto)
    endif()

    # Static linking needs the full transitive lib list — LIBS + LIBS_PRIVATE
    # from the .pc file. pkg_check_modules exposes that as `_STATIC_LIBRARIES`
    # and `_STATIC_LIBRARY_DIRS`.
    target_link_directories(PJSIP::PJSIP INTERFACE
        ${_PJSIP_STATIC_LIBRARY_DIRS}
    )
    target_link_libraries(PJSIP::PJSIP INTERFACE
        ${_PJSIP_LINK_LIBRARIES}
    )
    if(TARGET OpenSSL::SSL)
        target_link_libraries(PJSIP::PJSIP INTERFACE OpenSSL::SSL)
    endif()
    if(TARGET OpenSSL::Crypto)
        target_link_libraries(PJSIP::PJSIP INTERFACE OpenSSL::Crypto)
    endif()

    # Platform-specific link extras not advertised via pkg-config.
    if(APPLE)
        target_link_libraries(PJSIP::PJSIP INTERFACE
            "-framework CoreAudio"
            "-framework AudioToolbox"
            "-framework AudioUnit"
            "-framework CoreFoundation"
            "-framework CoreServices"
            "-framework Security"
        )
    elseif(WIN32)
        target_link_libraries(PJSIP::PJSIP INTERFACE
            ws2_32 winmm ole32 dsound wsock32
        )
    endif()
endif()
