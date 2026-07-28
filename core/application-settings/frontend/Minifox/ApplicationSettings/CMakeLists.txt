qt_add_library(minifox_settings_ui STATIC)

qt_add_qml_module(minifox_settings_ui
    URI Minifox.ApplicationSettings
    NO_PLUGIN
    IMPORTS
        TARGET minifox_shared_ui
    QML_FILES
        AccentColorDialog.qml
        FontFamilyComboBox.qml
        SettingsPage.qml
        SkinSettingsPanel.qml
        ConsoleSettingsPreview.qml
)

target_link_libraries(minifox_settings_ui
    PUBLIC
        Qt6::Quick
        Qt6::QuickControls2
        minifox_shared_ui
)
