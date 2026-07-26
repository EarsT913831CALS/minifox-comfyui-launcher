qt_add_library(minifox_runtime_backend STATIC
    CommandPromptBuilder.cpp
    CommandPromptBuilder.h
    LaunchCommandBuilder.cpp
    LaunchCommandBuilder.h
    LogModel.cpp
    LogModel.h
    RuntimeManager.cpp
    RuntimeManager.h
    ZludaBootstrap.cpp
    ZludaBootstrap.h
)

set(minifox_zluda_package_files
    ${PROJECT_SOURCE_DIR}/assets/zluda/zluda.extpack
    ${PROJECT_SOURCE_DIR}/assets/zluda/tensile-gfx101x.extpack
    ${PROJECT_SOURCE_DIR}/assets/zluda/tensile-gfx1031.extpack
    ${PROJECT_SOURCE_DIR}/assets/zluda/tensile-gfx1032.extpack
    ${PROJECT_SOURCE_DIR}/assets/zluda/tensile-gfx103x.extpack
    ${PROJECT_SOURCE_DIR}/assets/zluda/tensile-gfx1103.extpack
    ${PROJECT_SOURCE_DIR}/assets/zluda/tensile-gfx8xx-9xx.extpack
    ${PROJECT_SOURCE_DIR}/assets/zluda/tensile-gfx906.extpack
    ${PROJECT_SOURCE_DIR}/assets/zluda/tensile-gfx94x.extpack
)

qt_add_resources(minifox_runtime_backend minifox_zluda_packages
    BIG_RESOURCES
    PREFIX "/minifox/zluda"
    BASE "${PROJECT_SOURCE_DIR}/assets/zluda"
    FILES ${minifox_zluda_package_files}
)

target_include_directories(minifox_runtime_backend
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(minifox_runtime_backend
    PUBLIC
        Qt6::Core
        Qt6::Quick
        minifox_platform
    PRIVATE
        Qt6::Gui
        Qt6::Network
        minifox_application_settings_backend
        minifox_configuration_backend
        minifox_shared_backend
)
