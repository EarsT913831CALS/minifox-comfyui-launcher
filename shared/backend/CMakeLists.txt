qt_add_library(minifox_shared_backend STATIC
    PortablePaths.cpp
    PortablePaths.h
    ProcessTextDecoder.cpp
    ProcessTextDecoder.h
)

target_include_directories(minifox_shared_backend
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(minifox_shared_backend
    PUBLIC
        Qt6::Core
)
