set_source_files_properties(Theme.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

qt_add_library(minifox_shared_ui STATIC)

qt_add_qml_module(minifox_shared_ui
    URI Minifox.Shared
    NO_PLUGIN
    QML_FILES
        Theme.qml
        AppButton.qml
        AppComboBox.qml
        AppDialog.qml
        AppItemDelegate.qml
        AppLabel.qml
        AppSpinBox.qml
        AppSwitch.qml
        AppTextArea.qml
        AppTextField.qml
        AppToolButton.qml
        ButtonSurface.qml
        IconLabel.qml
        PageHeader.qml
        PathField.qml
        StatusBadge.qml
)

target_link_libraries(minifox_shared_ui
    PUBLIC
        Qt6::Quick
        Qt6::QuickControls2
)
