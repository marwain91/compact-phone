find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(test_unit
    unit/test_account_types.cpp
    unit/test_accounts_controller.cpp
    unit/test_sipengine_lifecycle.cpp
    unit/test_database.cpp
    unit/test_keychain.cpp
    unit/test_accounts_manager_update.cpp
    unit/test_call_session_tracker.cpp
    unit/test_contacts_manager.cpp
    unit/test_history_manager.cpp
    unit/test_lines_manager.cpp
    unit/test_messages_manager.cpp
    unit/test_models.cpp
    unit/test_settings_controller.cpp
    unit/test_settings_manager.cpp
    unit/test_sip_uri.cpp
    unit/test_bootconfig.cpp
    unit/test_contact_importer.cpp
    unit/test_update_checker.cpp
    unit/test_log_buffer.cpp
    unit/test_crash_reporting.cpp
    unit/test_keychain_factory.cpp
    unit/test_stream_stats.cpp
    unit/test_url_dispatcher.cpp
    unit/test_daktela_provider.cpp
)
target_link_libraries(test_unit
    PRIVATE GTest::gtest GTest::gtest_main Qt6::Test compactphone_core
)
target_include_directories(test_unit PRIVATE ${CMAKE_SOURCE_DIR}/src)
gtest_discover_tests(test_unit
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "unit"
)

add_executable(test_qml_layout
    unit/test_active_call_layout.cpp
    unit/test_daktela_branding_layout.cpp
)
target_link_libraries(test_qml_layout
    PRIVATE
        GTest::gtest
        GTest::gtest_main
        Qt6::Core
)
target_compile_definitions(test_qml_layout PRIVATE
    COMPACTPHONE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
gtest_discover_tests(test_qml_layout
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "unit"
)

add_executable(test_release_packaging
    unit/test_release_packaging.cpp
)
target_link_libraries(test_release_packaging
    PRIVATE
        GTest::gtest
        GTest::gtest_main
        Qt6::Core
)
target_compile_definitions(test_release_packaging PRIVATE
    COMPACTPHONE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
gtest_discover_tests(test_release_packaging
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "unit"
)

add_executable(test_integration
    integration/test_register_udp.cpp
    integration/test_outbound_call_echo.cpp
    integration/test_call_lifecycle.cpp
    integration/test_hold_unhold.cpp
    integration/test_dtmf.cpp
    integration/test_register_tls.cpp
    integration/test_inbound_call.cpp
    integration/test_blind_transfer.cpp
    integration/test_attended_transfer.cpp
    integration/test_forward_call.cpp
    integration/test_conference.cpp
    integration/test_call_recording.cpp
    integration/test_instant_message.cpp
    integration/test_call_policies.cpp
)
target_link_libraries(test_integration
    PRIVATE GTest::gtest GTest::gtest_main Qt6::Core compactphone_core
)
target_include_directories(test_integration PRIVATE ${CMAKE_SOURCE_DIR}/src)
gtest_discover_tests(test_integration
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "integration"
)
