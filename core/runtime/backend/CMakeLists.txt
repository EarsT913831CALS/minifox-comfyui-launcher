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
    ${PROJECT_SOURCE_DIR}/assets/zluda/zluda-hip57.extpack
    ${PROJECT_SOURCE_DIR}/assets/zluda/zluda-hip71.extpack
)

file(READ "${PROJECT_SOURCE_DIR}/assets/zluda/manifest.json" minifox_zluda_manifest)
foreach(package IN LISTS minifox_zluda_package_files)
    get_filename_component(package_name "${package}" NAME)
    file(SHA256 "${package}" actual_hash)
    string(JSON expected_hash GET "${minifox_zluda_manifest}" "${package_name}" archiveSha256)
    if(NOT actual_hash STREQUAL expected_hash)
        message(FATAL_ERROR "ZLUDA archive does not match the reviewed manifest: ${package_name}")
    endif()
endforeach()
list(APPEND minifox_zluda_package_files "${PROJECT_SOURCE_DIR}/assets/zluda/manifest.json")

qt_add_resources(minifox_runtime_backend minifox_zluda_packages
    BIG_RESOURCES
    PREFIX "/minifox/zluda"
    BASE "${PROJECT_SOURCE_DIR}/assets/zluda"
    FILES ${minifox_zluda_package_files}
)

qt_add_resources(minifox_runtime_backend minifox_python_bridge
    PREFIX "/minifox/python"
    BASE "${PROJECT_SOURCE_DIR}/assets/python"
    FILES
        ${PROJECT_SOURCE_DIR}/assets/python/minifox_progress_bridge.py
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
        minifox_shared_backend
    PRIVATE
        Qt6::Gui
        Qt6::Network
        minifox_application_settings_backend
        minifox_configuration_backend
)
