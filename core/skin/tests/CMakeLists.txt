qt_add_executable(minifox_skin_tests
    tst_SkinManager.cpp
)

set_target_properties(minifox_skin_tests PROPERTIES WIN32_EXECUTABLE FALSE)

target_link_libraries(minifox_skin_tests
    PRIVATE
        Qt6::Test
        Qt6::Gui
        minifox_application_settings_backend
        minifox_skin_backend
)

add_test(NAME minifox_skin_tests COMMAND minifox_skin_tests)
set_tests_properties(minifox_skin_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)
