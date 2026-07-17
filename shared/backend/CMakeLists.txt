qt_add_library(minifox_shared STATIC
    PortablePaths.cpp
    PortablePaths.h
)

target_include_directories(minifox_shared
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(minifox_shared
    PUBLIC
        Qt6::Core
)
