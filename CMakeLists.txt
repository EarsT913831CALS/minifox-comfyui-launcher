cmake_minimum_required(VERSION 3.22)

project(MinifoxLauncher VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MINIFOX_BUILD_TESTS "Build Minifox unit tests" OFF)
option(MINIFOX_STATIC_RUNTIME "Link Qt and third-party runtime libraries statically" OFF)

if(MINIFOX_STATIC_RUNTIME)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    set(CMAKE_IMPORT_LIBRARY_SUFFIX ".a")
    set(PKG_CONFIG_ARGN "--static")
endif()

find_package(
    Qt6 6.8
    REQUIRED COMPONENTS
        Core
        Gui
        LinguistTools
        Network
        Qml
        Quick
        QuickControls2
)

qt_standard_project_setup(REQUIRES 6.8)

if(NOT WIN32)
    message(FATAL_ERROR "Minifox Launcher currently supports Windows only.")
endif()

if(MINIFOX_STATIC_RUNTIME AND NOT MINGW)
    message(FATAL_ERROR "MINIFOX_STATIC_RUNTIME requires the MSYS2 UCRT64 MinGW toolchain.")
endif()

if(MINIFOX_STATIC_RUNTIME AND QT6_IS_SHARED_LIBS_BUILD)
    message(FATAL_ERROR
        "MINIFOX_STATIC_RUNTIME requires a static Qt build. "
        "Configure with the qt-cmake from <MSYS2>/ucrt64/qt6-static/bin."
    )
endif()

if(MINIFOX_STATIC_RUNTIME AND TARGET OpenSSL::Crypto)
    set_property(
        TARGET OpenSSL::Crypto
        APPEND PROPERTY INTERFACE_LINK_LIBRARIES crypt32
    )
endif()

add_subdirectory(shared/backend)
add_subdirectory(shared/frontend/Minifox/Shared)
add_subdirectory(core/configuration/backend)
add_subdirectory(core/configuration/frontend/Minifox/Configuration)
add_subdirectory(core/application-settings/backend)
add_subdirectory(core/application-settings/frontend/Minifox/ApplicationSettings)
add_subdirectory(platform/windows)
add_subdirectory(core/runtime/backend)
add_subdirectory(core/runtime/frontend/Minifox/Runtime)
add_subdirectory(app)

if(MINIFOX_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
    add_subdirectory(core/runtime/tests)
endif()
