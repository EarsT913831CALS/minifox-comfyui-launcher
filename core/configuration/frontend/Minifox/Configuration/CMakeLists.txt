qt_add_library(minifox_configuration_ui STATIC)

qt_add_qml_module(minifox_configuration_ui
    URI Minifox.Configuration
    NO_PLUGIN
    IMPORTS
        TARGET minifox_shared_ui
    QML_FILES
        ConfigurationPage.qml
        CudaDevicePanel.qml
        EnvironmentEditor.qml
        ParameterEditor.qml
)

target_link_libraries(minifox_configuration_ui
    PUBLIC
        Qt6::Quick
        Qt6::QuickControls2
        minifox_shared_ui
)
