qt_add_executable(minifox_runtime_tests
    tst_LaunchCommandBuilder.cpp
)

target_link_libraries(minifox_runtime_tests
    PRIVATE
        Qt6::Test
        Qt6::Network
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        minifox_application_settings_backend
        minifox_configuration_backend
        minifox_runtime_backend
)

add_test(NAME minifox_runtime_tests COMMAND minifox_runtime_tests)
set_tests_properties(minifox_runtime_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)
