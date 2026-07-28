qt_add_library(minifox_skin_backend STATIC
    SkinManager.cpp
    SkinManager.h
)

target_include_directories(minifox_skin_backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(minifox_skin_backend
    PUBLIC
        Qt6::Core
        Qt6::Gui
        minifox_application_settings_backend
        minifox_shared_backend
)
