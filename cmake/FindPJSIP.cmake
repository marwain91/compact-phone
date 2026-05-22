# Locates PJSIP and exposes a single imported target `PJSIP::PJSIP` that
# links pjsua2 and every transitive PJSIP component (including bundled
# third_party libs) plus the system libraries PJSIP requires.
#
# Discovery strategy depends on platform:
#
# * Linux / macOS: pkg-config metadata installed by pjproject's autotools
#   `make install`. The PJSIP build lives outside vcpkg because pjproject is
#   not in the vcpkg registry. See tools/dev/Dockerfile and the macOS release
#   workflow.
#
# * Windows: PJSIP_ROOT environment variable pointing at the built pjproject
#   tree. PJSIP's Visual Studio build does not emit a .pc file, so we discover
#   per-component .lib files by glob and add include dirs explicitly. See
#   .github/workflows/platform-matrix.yml and release-windows.yml.

if(WIN32)
    set(_pjsip_root "$ENV{PJSIP_ROOT}")
    if(NOT _pjsip_root OR NOT IS_DIRECTORY "${_pjsip_root}")
        message(FATAL_ERROR
            "PJSIP_ROOT environment variable is not set or does not point at "
            "a directory. On Windows we expect a pre-built pjproject tree "
            "with .lib files under pjlib/lib, pjlib-util/lib, pjnath/lib, "
            "pjmedia/lib, pjsip/lib, and third_party/lib.")
    endif()

    set(_pjsip_includes
        "${_pjsip_root}/pjlib/include"
        "${_pjsip_root}/pjlib-util/include"
        "${_pjsip_root}/pjnath/include"
        "${_pjsip_root}/pjmedia/include"
        "${_pjsip_root}/pjsip/include"
    )
    foreach(_dir IN LISTS _pjsip_includes)
        if(NOT IS_DIRECTORY "${_dir}")
            message(FATAL_ERROR "Missing PJSIP include dir: ${_dir}")
        endif()
    endforeach()

    set(_pjsip_lib_dirs
        "${_pjsip_root}/pjlib/lib"
        "${_pjsip_root}/pjlib-util/lib"
        "${_pjsip_root}/pjnath/lib"
        "${_pjsip_root}/pjmedia/lib"
        "${_pjsip_root}/pjsip/lib"
        "${_pjsip_root}/third_party/lib"
    )
    set(_pjsip_libs "")
    foreach(_dir IN LISTS _pjsip_lib_dirs)
        if(NOT IS_DIRECTORY "${_dir}")
            message(FATAL_ERROR "Missing PJSIP lib dir: ${_dir}")
        endif()
        file(GLOB _libs "${_dir}/*.lib")
        list(APPEND _pjsip_libs ${_libs})
    endforeach()
    if(NOT _pjsip_libs)
        message(FATAL_ERROR
            "PJSIP_ROOT=${_pjsip_root} contains no .lib files; did the "
            "Windows PJSIP build step run?")
    endif()

    set(PJSIP_FOUND TRUE)
    add_library(PJSIP::PJSIP INTERFACE IMPORTED)
    target_include_directories(PJSIP::PJSIP INTERFACE ${_pjsip_includes})
    target_link_libraries(PJSIP::PJSIP INTERFACE
        ${_pjsip_libs}
        ws2_32 winmm ole32 dsound wsock32
    )
    # Qt defines UNICODE/_UNICODE on Windows. PJSIP then selects a wide-char
    # code path that requires helpers it does not provide, so force ANSI
    # internal strings for this target.
    target_compile_definitions(PJSIP::PJSIP INTERFACE
        PJ_NATIVE_STRING_IS_UNICODE=0
    )
else()
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

        # Static linking needs the full transitive lib list: LIBS + LIBS_PRIVATE
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
        endif()
    endif()
endif()
