qt_add_executable(minifox_window_chrome_tests
    tst_WindowChrome.cpp
)

set_target_properties(minifox_window_chrome_tests PROPERTIES WIN32_EXECUTABLE FALSE)

target_link_libraries(minifox_window_chrome_tests
    PRIVATE
        Qt6::Test
        minifox_platform
)

add_test(NAME minifox_window_chrome_tests COMMAND minifox_window_chrome_tests)
set_tests_properties(minifox_window_chrome_tests PROPERTIES
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)
