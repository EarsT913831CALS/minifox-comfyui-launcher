qt_add_executable(minifox_launcher WIN32
    ../../../backend/ApplicationIconManager.cpp
    ../../../backend/ApplicationIconManager.h
    ../../../backend/AppContext.cpp
    ../../../backend/AppContext.h
    ../../../backend/ConfigurationPackageManager.cpp
    ../../../backend/ConfigurationPackageManager.h
    ../../../backend/main.cpp
    ../../../backend/VersionManager.cpp
    ../../../backend/VersionManager.h
    ../../../backend/version/GitProcessRunner.cpp
    ../../../backend/version/GitProcessRunner.h
    ../../../backend/version/RepositoryUpdateCoordinator.cpp
    ../../../backend/version/RepositoryUpdateCoordinator.h
    ../../../backend/version/RepositoryUpdateJob.cpp
    ../../../backend/version/RepositoryUpdateJob.h
    ../../../backend/version/SafeMergeEngine.cpp
    ../../../backend/version/SafeMergeEngine.h
    ../../../backend/version/VersionTransactionStore.cpp
    ../../../backend/version/VersionTransactionStore.h
    ../../../backend/version/RepositoryRecovery.cpp
    ../../../backend/version/RepositoryRecovery.h
    resources/app-icon.rc
)

qt_add_resources(minifox_launcher minifox_app_icon_resources
    PREFIX "/minifox/icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    FILES
        resources/app-icon-light.png
        resources/app-icon-dark.png
)

qt_add_qml_module(minifox_launcher
    URI Minifox.App
    IMPORTS
        TARGET minifox_settings_ui
        TARGET minifox_configuration_ui
        TARGET minifox_runtime_ui
        TARGET minifox_shared_ui
    QML_FILES
        Main.qml
        AppTitleBar.qml
        AppShell.qml
        DashboardPage.qml
        LaunchStatusCard.qml
        HomeWidgetContent.qml
        HomeWidgetFrame.qml
        LaunchInformationRow.qml
        NavigationDestination.qml
        NavigationRail.qml
        VersionPage.qml
)

target_link_libraries(minifox_launcher
    PRIVATE
        Qt6::Core
        Qt6::Concurrent
        Qt6::Gui
        Qt6::Qml
        Qt6::Network
        Qt6::Quick
        Qt6::QuickControls2
        minifox_application_settings_backend
        minifox_settings_ui
        minifox_configuration_backend
        minifox_configuration_ui
        minifox_runtime_backend
        minifox_runtime_ui
        minifox_skin_backend
        minifox_platform
        minifox_shared_backend
        minifox_shared_ui
)

if(MINGW)
    target_compile_options(minifox_launcher PRIVATE
        $<$<CONFIG:Release>:-ffunction-sections>
        $<$<CONFIG:Release>:-fdata-sections>
    )
    target_link_options(minifox_launcher PRIVATE
        $<$<CONFIG:Release>:-Wl,--gc-sections>
        $<$<CONFIG:Release>:-s>
    )

    if(NOT QT6_IS_SHARED_LIBS_BUILD)
        target_link_options(minifox_launcher PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()
endif()

set_target_properties(minifox_launcher PROPERTIES
    OUTPUT_NAME "Minifox ComfyUI Launcher"
)

qt_add_translations(minifox_launcher
    TS_FILES
        ../../translations/qml_en_US.ts
    LUPDATE_OPTIONS
        -no-obsolete
    RESOURCE_PREFIX "/i18n"
)

if(MINIFOX_STATIC_RUNTIME)
    qt_import_plugins(minifox_launcher
        EXCLUDE_BY_TYPE iconengines
        EXCLUDE_BY_TYPE imageformats
        EXCLUDE_BY_TYPE qmltooling
        EXCLUDE_BY_TYPE vectorimageformats
        INCLUDE
            Qt6::QICOPlugin
            Qt6::QJpegPlugin
            Qt6::QWebpPlugin
        INCLUDE_BY_TYPE tls Qt6::QSchannelBackendPlugin
    )
endif()

qt_import_qml_plugins(minifox_launcher)

if(MINIFOX_STATIC_RUNTIME)
    # MSYS2's static Qt package uses system archives whose CMake targets omit
    # their private dependencies. Keep them in a rescan group to handle the
    # FreeType/Harfbuzz cycle and archive ordering deterministically.
    target_link_libraries(minifox_launcher PRIVATE
        "-Wl,--start-group"
        freetype
        bz2
        png16
        z
        harfbuzz
        glib-2.0
        intl
        iconv
        pcre2-8
        graphite2
        brotlidec
        brotlicommon
        "-Wl,--end-group"
        atomic
        crypt32
        dwrite
        gdi32
        ole32
        rpcrt4
        shlwapi
        usp10
        user32
        uuid
        winmm
        ws2_32
    )
endif()

include(GNUInstallDirs)
install(
    TARGETS minifox_launcher
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
