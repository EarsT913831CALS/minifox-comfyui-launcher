qt_add_executable(minifox_versioning_tests
    tst_Transaction.cpp
)

target_link_libraries(minifox_versioning_tests
    PRIVATE
        Qt6::Test
        minifox_versioning_backend
)

add_test(NAME minifox_versioning_tests
    COMMAND minifox_versioning_tests
        -o ${CMAKE_BINARY_DIR}/minifox_versioning_tests.txt,txt)

qt_add_executable(minifox_version_service_tests
    tst_VersionService.cpp
)

target_link_libraries(minifox_version_service_tests
    PRIVATE
        Qt6::Test
        minifox_configuration_backend
        minifox_versioning_backend
)

add_test(NAME minifox_version_service_tests
    COMMAND minifox_version_service_tests
        -o ${CMAKE_BINARY_DIR}/minifox_version_service_tests.txt,txt)
set_tests_properties(minifox_version_service_tests PROPERTIES
    RESOURCE_LOCK minifox_version_transactions
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
