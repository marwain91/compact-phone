add_library(compactphone_core STATIC
    core/Account.h
    core/Account.cpp
    core/AccountsController.h
    core/AccountsController.cpp
    core/TrayController.h
    core/TrayController.cpp
    core/NetworkMonitor.h
    core/NetworkMonitor.cpp
    core/PowerMonitor.h
    core/PowerMonitor.cpp
    core/PowerMonitor_windows.cpp
    core/UrlDispatcher.h
    core/UrlDispatcher.cpp
    core/CallEntry.h
    core/CallEntry.cpp
    core/CallSessionTracker.h
    core/CallSessionTracker.cpp
    core/CallsController.h
    core/CallsController.cpp
    core/SettingsController.h
    core/SettingsController.cpp
    core/SipLog.h
    core/SipLog.cpp
    core/SipEngine.h
    core/SipEngine.cpp
    core/SipUri.h
    core/SipUri.cpp
    core/AccountsManager.h
    core/AccountsManager.cpp
    core/BootConfig.h
    core/BootConfig.cpp
    core/provisioning/Provider.h
    core/provisioning/Registry.h
    core/provisioning/Registry.cpp
    core/provisioning/DaktelaProvider.h
    core/provisioning/DaktelaProvider.cpp
    core/UpdateChecker.h
    core/UpdateChecker.cpp
    core/LogBuffer.h
    core/LogBuffer.cpp
    core/CrashReporting.h
    core/CrashReporting.cpp
    core/ContactImporter.h
    core/ContactImporter.cpp
    core/CallManager.h
    core/CallManager.cpp
    core/RingtonePlayer.h
    core/RingtonePlayer.cpp
    core/platform/Keychain.h
    core/platform/Keychain_memory.h
    core/platform/Keychain_memory.cpp
    core/platform/Keychain_file.h
    core/platform/Keychain_file.cpp
    core/platform/Keychain_factory.h
    core/platform/Keychain_factory.cpp
    core/platform/Keychain_windows.h
    core/platform/Keychain_windows.cpp
    core/Contact.h
    core/ContactsManager.h
    core/ContactsManager.cpp
    core/HistoryEntry.h
    core/HistoryManager.h
    core/HistoryManager.cpp
    core/Message.h
    core/MessagesManager.h
    core/MessagesManager.cpp
    core/WatchedLine.h
    core/LinesManager.h
    core/LinesManager.cpp
    core/SettingsManager.h
    core/SettingsManager.cpp
    models/AccountsModel.h
    models/AccountsModel.cpp
    models/CallsModel.h
    models/CallsModel.cpp
    models/ContactsModel.h
    models/ContactsModel.cpp
    models/HistoryModel.h
    models/HistoryModel.cpp
    models/MessagesModel.h
    models/MessagesModel.cpp
    models/ConversationsModel.h
    models/ConversationsModel.cpp
    models/LinesModel.h
    models/LinesModel.cpp
    persistence/Database.h
    persistence/Database.cpp
)
target_link_libraries(compactphone_core
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        PJSIP::PJSIP
        OpenSSL::Crypto
        spdlog::spdlog
        unofficial::sqlite3::sqlite3
)
if(COMPACTPHONE_WITH_TRAY)
    target_link_libraries(compactphone_core PUBLIC Qt6::Widgets)
endif()

if(APPLE)
    # macOS-specific Obj-C++ backends (Keychain, PowerMonitor). Each .mm
    # needs -fobjc-arc and pulls in its Apple framework dependency.
    target_sources(compactphone_core PRIVATE
        core/platform/Keychain_macos.h
        core/platform/Keychain_macos.mm
        core/platform/TouchIdGate.h
        core/platform/TouchIdGate.mm
        core/PowerMonitor_macos.mm
    )
    set_source_files_properties(
        core/platform/Keychain_macos.mm
        core/platform/TouchIdGate.mm
        core/PowerMonitor_macos.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(compactphone_core PUBLIC
        "-framework AppKit"
        "-framework Security"
        "-framework LocalAuthentication"
        "-framework Foundation"
    )
endif()
target_compile_definitions(compactphone_core
    PUBLIC COMPACTPHONE_WITH_TRAY=$<BOOL:${COMPACTPHONE_WITH_TRAY}>
)
if(COMPACTPHONE_ENABLE_SENTRY)
    find_package(sentry CONFIG REQUIRED)
    target_link_libraries(compactphone_core PUBLIC sentry::sentry)
    target_compile_definitions(compactphone_core
        PUBLIC COMPACTPHONE_ENABLE_SENTRY=1)
endif()
target_compile_definitions(compactphone_core PRIVATE
    COMPACTPHONE_VERSION="${PROJECT_VERSION}"
)
target_include_directories(compactphone_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Generate ringtone.wav at build time so we don't commit a binary blob.
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/resources/ringtone.wav
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/resources/generate_ringtone.py
                    ${CMAKE_CURRENT_SOURCE_DIR}/resources/ringtone.wav
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/resources/generate_ringtone.py
    COMMENT "Generating ringtone.wav"
    VERBATIM
)
add_custom_target(ringtone_wav ALL
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/resources/ringtone.wav
)

set(_compactphone_platform_sources)
if(WIN32)
    list(APPEND _compactphone_platform_sources compactphone.rc)
    # VERSIONINFO needs the version baked in at compile time. PROJECT_VERSION
    # comes from the top-level project() call (currently 0.1.0); pad to four
    # parts (Win32 wants major,minor,patch,build).
    set(_cp_v "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
    set(_cp_vs "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.0")
    set_source_files_properties(compactphone.rc PROPERTIES
        COMPILE_DEFINITIONS
        "COMPACTPHONE_VERSION_RC=${_cp_v};COMPACTPHONE_VERSION_STR=\"${_cp_vs}\""
    )
endif()

qt_add_executable(compactphone
    main.cpp
    resources/ringtone.qrc
    resources/branding.qrc
    ${_compactphone_platform_sources}
)
add_dependencies(compactphone ringtone_wav)

add_executable(compactphone-headless
    headless_main.cpp
)
target_link_libraries(compactphone-headless
    PRIVATE
        Qt6::Core
        compactphone_core
)
target_compile_definitions(compactphone-headless PRIVATE
    COMPACTPHONE_VERSION="${PROJECT_VERSION}"
)

if(APPLE)
    set(_compactphone_icns "${CMAKE_SOURCE_DIR}/branding/compactphone.icns")
    if(EXISTS "${_compactphone_icns}")
        target_sources(compactphone PRIVATE "${_compactphone_icns}")
        set_source_files_properties("${_compactphone_icns}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources"
        )
        set_target_properties(compactphone PROPERTIES
            MACOSX_BUNDLE_ICON_FILE "compactphone.icns"
        )
    else()
        message(WARNING
            "branding/compactphone.icns not found — run tools/generate-icons.sh "
            "to regenerate it from the source SVG."
        )
    endif()
endif()

set_source_files_properties(ui/qml/Theme.qml ui/qml/Icons.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

if(COMMAND qt_policy)
    qt_policy(SET QTP0004 NEW)
endif()

qt_add_qml_module(compactphone
    URI CompactPhone
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qml/CompactPhone
    QML_FILES
        ui/qml/Main.qml
        ui/qml/Theme.qml
        ui/qml/Icons.qml
        ui/qml/DialerPane.qml
        ui/qml/AccountsPane.qml
        ui/qml/AccountEditDialog.qml
        ui/qml/ProviderSignInDialog.qml
        ui/qml/ActiveCallView.qml
        ui/qml/IncomingCallDialog.qml
        ui/qml/Snackbar.qml
        ui/qml/TransferDialog.qml
        ui/qml/ContactsPane.qml
        ui/qml/ContactEditDialog.qml
        ui/qml/HistoryPane.qml
        ui/qml/MessagesPane.qml
        ui/qml/LinesPane.qml
        ui/qml/SettingsPane.qml
        ui/qml/LogViewerDialog.qml
        ui/qml/components/AppIcon.qml
        ui/qml/components/AppButton.qml
        ui/qml/components/IconButton.qml
        ui/qml/components/AppCard.qml
        ui/qml/components/StatusBadge.qml
        ui/qml/components/NavItem.qml
        ui/qml/components/AppComboBox.qml
        ui/qml/components/AppCheckBox.qml
        ui/qml/components/AppSwitch.qml
        ui/qml/components/BrandMark.qml
        ui/qml/components/ThemeCard.qml
    SOURCES
        core/PhoneController.h
        core/PhoneController.cpp
)

target_include_directories(compactphone PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_compile_definitions(compactphone PRIVATE
    COMPACTPHONE_VERSION="${PROJECT_VERSION}"
)

# i18n: lupdate produces .ts from QML/C++ sources; lrelease compiles them
# to .qm at build time. The .qm files are embedded into a Qt resource so
# main.cpp can load them without extracting to disk. Skipped silently when
# Qt6 LinguistTools isn't available — main.cpp's QTranslator::load just
# becomes a no-op in that case.
if(COMPACTPHONE_WITH_LINGUIST)
    qt_add_translations(compactphone
        TS_FILES
            i18n/compactphone_cs.ts
        QM_FILES_OUTPUT_VARIABLE COMPACTPHONE_QM_FILES
        RESOURCE_PREFIX "/i18n"
    )
endif()

target_link_libraries(compactphone
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        compactphone_core
)

set_target_properties(compactphone PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

if(APPLE)
    # OUTPUT_NAME drives the bundle directory ("Compact Phone.app") AND the
    # binary inside Contents/MacOS/ ("Compact Phone"). Both with a space
    # follows Apple convention — QuickTime Player.app/Contents/MacOS/QuickTime
    # Player, Photo Booth.app/Contents/MacOS/Photo Booth, etc. The Dock and
    # Finder show the bundle directory name, so this is what fixes the
    # "compactphone" tooltip. CFBundleExecutable in Info.plist comes from
    # MACOSX_BUNDLE_EXECUTABLE_NAME which CMake defaults to OUTPUT_NAME, so
    # the plist entry will say "Compact Phone" — matches the on-disk binary.
    set_target_properties(compactphone PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "Compact Phone"
        OUTPUT_NAME "Compact Phone"
    )
endif()
