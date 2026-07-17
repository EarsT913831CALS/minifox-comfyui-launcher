qt_add_library(minifox_application_settings STATIC
    ApplicationSettings.cpp
    ApplicationSettings.h
)

target_include_directories(minifox_application_settings
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(minifox_application_settings
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Network
    PRIVATE
        minifox_shared
)

if(WIN32)
    target_link_libraries(minifox_application_settings PRIVATE dwmapi)
endif()
