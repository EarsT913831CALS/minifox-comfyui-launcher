qt_add_executable(minifox_version_update_tests
    tst_RepositoryUpdate.cpp
    ../GitProcessRunner.cpp
    ../GitProcessRunner.h
    ../RepositoryUpdateJob.cpp
    ../RepositoryUpdateJob.h
    ../SafeMergeEngine.cpp
    ../SafeMergeEngine.h
    ../VersionTransactionStore.cpp
    ../VersionTransactionStore.h
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
