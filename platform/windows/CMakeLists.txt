qt_add_library(minifox_platform STATIC
    ProcessJob.cpp
    ProcessJob.h
)

target_include_directories(minifox_platform
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(minifox_platform
    PUBLIC
        Qt6::Core
)

if(WIN32)
    target_compile_definitions(minifox_platform PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
