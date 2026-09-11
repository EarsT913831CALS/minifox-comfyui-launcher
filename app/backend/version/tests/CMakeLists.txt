qt_add_executable(minifox_version_update_tests
    tst_RepositoryUpdate.cpp
    ../GitProcessRunner.cpp
    ../GitProcessRunner.h
    ../RepositoryUpdateJob.cpp
    ../RepositoryUpdateJob.h
    ../RepositoryUpdateCoordinator.cpp
    ../RepositoryUpdateCoordinator.h
    ../SafeMergeEngine.cpp
    ../SafeMergeEngine.h
    ../VersionTransactionStore.cpp
    ../VersionTransactionStore.h
    ../RepositoryRecovery.cpp
    ../RepositoryRecovery.h
)

target_include_directories(minifox_version_update_tests PRIVATE ..)

target_link_libraries(minifox_version_update_tests
    PRIVATE
        Qt6::Concurrent
        Qt6::Test
        minifox_shared_backend
)

add_test(NAME minifox_version_update_tests
    COMMAND minifox_version_update_tests
        -o ${CMAKE_BINARY_DIR}/minifox_version_update_tests.txt,txt)
set_tests_properties(minifox_version_update_tests PROPERTIES RESOURCE_LOCK minifox_version_transactions)

if(TARGET minifox_application_settings_backend AND TARGET minifox_configuration_backend AND TARGET minifox_platform)
    qt_add_executable(minifox_version_manager_tests
        tst_VersionManager.cpp
        ../../VersionManager.cpp ../../VersionManager.h
        ../GitProcessRunner.cpp ../RepositoryUpdateJob.cpp ../RepositoryUpdateCoordinator.cpp
        ../SafeMergeEngine.cpp ../VersionTransactionStore.cpp ../RepositoryRecovery.cpp
    )
    target_include_directories(minifox_version_manager_tests PRIVATE ../.. ..)
    target_link_libraries(minifox_version_manager_tests PRIVATE
        Qt6::Test Qt6::Concurrent Qt6::Network
        minifox_application_settings_backend minifox_configuration_backend
        minifox_platform minifox_shared_backend)
    add_test(NAME minifox_version_manager_tests COMMAND minifox_version_manager_tests
        -o ${CMAKE_BINARY_DIR}/minifox_version_manager_tests.txt,txt)
    set_tests_properties(minifox_version_manager_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" RESOURCE_LOCK minifox_version_transactions)
endif()
