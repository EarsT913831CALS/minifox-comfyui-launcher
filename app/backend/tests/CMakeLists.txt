qt_add_executable(minifox_configuration_package_tests
    tst_ConfigurationPackageManager.cpp
    ../ConfigurationPackageManager.h
)
target_link_libraries(minifox_configuration_package_tests PRIVATE
    Qt6::Test minifox_shared_backend minifox_configuration_backend)
add_test(NAME minifox_configuration_package_tests COMMAND minifox_configuration_package_tests
    -o "${CMAKE_BINARY_DIR}/minifox_configuration_package_tests.txt,txt")
set_tests_properties(minifox_configuration_package_tests PROPERTIES
    RESOURCE_LOCK minifox_version_transactions
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
