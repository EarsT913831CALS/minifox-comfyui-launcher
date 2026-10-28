qt_add_executable(minifox_versioning_tests
    tst_Transaction.cpp
    ../Git.cpp
    ../Git.h
    ../Repository.cpp
    ../Repository.h
    ../Transaction.cpp
    ../Transaction.h
)

target_include_directories(minifox_versioning_tests PRIVATE ..)

target_link_libraries(minifox_versioning_tests
    PRIVATE
        Qt6::Test
        minifox_shared_backend
)

add_test(NAME minifox_versioning_tests
    COMMAND minifox_versioning_tests
        -o ${CMAKE_BINARY_DIR}/minifox_versioning_tests.txt,txt)
