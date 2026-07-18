qt_add_library(minifox_runtime_ui STATIC)

qt_add_qml_module(minifox_runtime_ui
    URI Minifox.Runtime
    NO_PLUGIN
    IMPORTS
        TARGET minifox_shared_ui
    QML_FILES
        ConsoleProgressPanel.qml
        ConsoleView.qml
        RuntimePage.qml
)

target_link_libraries(minifox_runtime_ui
    PUBLIC
        Qt6::Quick
        Qt6::QuickControls2
        minifox_shared_ui
)
