qt_add_library(minifox_runtime_backend STATIC
    LaunchCommandBuilder.cpp
    LaunchCommandBuilder.h
    LogModel.cpp
    LogModel.h
    RuntimeManager.cpp
    RuntimeManager.h
)

target_include_directories(minifox_runtime_backend
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(minifox_runtime_backend
    PUBLIC
        Qt6::Core
        minifox_platform
    PRIVATE
        Qt6::Gui
        Qt6::Network
        minifox_application_settings_backend
        minifox_configuration_backend
)
