qt_add_library(minifox_configuration STATIC
    ConfigurationManager.cpp
    ConfigurationManager.h
    LaunchParameterCatalog.cpp
    LaunchParameterCatalog.h
)

target_include_directories(minifox_configuration
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(minifox_configuration
    PUBLIC
        Qt6::Core
    PRIVATE
        minifox_shared
)
