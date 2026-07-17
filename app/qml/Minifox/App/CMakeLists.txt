set_source_files_properties(Theme.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

qt_add_executable(minifox_launcher WIN32
    ../../../AppContext.cpp
    ../../../AppContext.h
    ../../../main.cpp
)

qt_add_qml_module(minifox_launcher
    URI Minifox.App
    QML_FILES
        Main.qml
        Theme.qml
        AppShell.qml
        components/ConsoleView.qml
        components/ConsoleSettingsPreview.qml
        components/AccentColorDialog.qml
        components/AppButton.qml
        components/AppComboBox.qml
        components/AppDialog.qml
        components/AppItemDelegate.qml
        components/AppLabel.qml
        components/AppSpinBox.qml
        components/AppSwitch.qml
        components/AppTextArea.qml
        components/AppTextField.qml
        components/AppToolButton.qml
        components/ButtonSurface.qml
        components/EnvironmentEditor.qml
        components/IconLabel.qml
        components/NavigationRail.qml
        components/PageHeader.qml
        components/ParameterEditor.qml
        components/PathField.qml
        components/StatusBadge.qml
        pages/ConfigurationPage.qml
        pages/DashboardPage.qml
        pages/RuntimePage.qml
        pages/SettingsPage.qml
)

target_link_libraries(minifox_launcher
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        minifox_application_settings
        minifox_configuration
        minifox_runtime
        minifox_shared
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
        ../../../translations/qml_en_US.ts
    RESOURCE_PREFIX "/i18n"
)

if(MINIFOX_STATIC_RUNTIME)
    qt_import_plugins(minifox_launcher
        EXCLUDE_BY_TYPE iconengines
        EXCLUDE_BY_TYPE imageformats
        EXCLUDE_BY_TYPE qmltooling
        EXCLUDE_BY_TYPE vectorimageformats
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
