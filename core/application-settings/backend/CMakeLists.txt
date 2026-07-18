qt_add_library(minifox_application_settings_backend STATIC
    ApplicationSettings.cpp
    ApplicationSettings.h
)

target_include_directories(minifox_application_settings_backend
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(minifox_application_settings_backend
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Network
    PRIVATE
        minifox_shared_backend
)

if(WIN32)
    target_link_libraries(minifox_application_settings_backend PRIVATE dwmapi)
endif()
