pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Frame {
    id: root

    required property var appContext
    required property var parameter
    readonly property string consoleFontFamily: appContext.settings.consoleFontFamily
    readonly property real consoleFontSize: appContext.settings.consoleFontSize
    readonly property bool consoleWordWrap: appContext.settings.consoleWordWrap
    readonly property var displayFlags: parameter.flag
        ? String(parameter.flag).split(" / ")
        : []

    Layout.fillWidth: true
    padding: Theme.spacingMd

    function optionIndex(value) {
        for (let index = 0; index < parameter.options.length; ++index) {
            if (parameter.options[index].value === value)
                return index;
        }
        return 0;
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingSm

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMd

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingXs

                AppLabel {
                    text: root.parameter.title
                    font.pointSize: Theme.bodySize
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }

                AppLabel {
                    text: root.parameter.description
                    color: Theme.foregroundSecondary
                    font.pointSize: Theme.captionSize
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }

            ColumnLayout {
                spacing: 0
                Layout.alignment: Qt.AlignRight | Qt.AlignTop

                Repeater {
                    model: root.displayFlags

                    delegate: AppLabel {
                        required property string modelData

                        text: modelData
                        color: palette.highlight
                        font.family: root.consoleFontFamily
                        font.pointSize: root.consoleFontSize
                        horizontalAlignment: Text.AlignRight
                        wrapMode: Text.NoWrap
                        Layout.alignment: Qt.AlignRight
                    }
                }
            }
        }

        Loader {
            Layout.fillWidth: true
            sourceComponent: {
                switch (root.parameter.control) {
                case "switch":
                    return switchEditor;
                case "choice":
                    return choiceEditor;
                case "integer":
                    return integerEditor;
                case "integerOptional":
                    return integerOptionalEditor;
                case "real":
                    return realEditor;
                case "realOptional":
                    return realOptionalEditor;
                case "folder":
                    return folderEditor;
                case "file":
                    return fileEditor;
                case "multiline":
                    return multilineEditor;
                default:
                    return textEditor;
                }
            }
        }
    }

    Component {
        id: switchEditor

        AppSwitch {
            text: checked ? qsTr("已启用") : qsTr("未启用")
            checked: Boolean(root.parameter.value)
            onToggled: root.appContext.configuration.setParameterValue(root.parameter.key, checked)
        }
    }

    Component {
        id: choiceEditor

        AppComboBox {
            model: root.parameter.options
            textRole: "label"
            valueRole: "value"
            currentIndex: root.optionIndex(root.parameter.value)
            onActivated: index => root.appContext.configuration.setParameterValue(root.parameter.key, root.parameter.options[index].value)
        }
    }

    Component {
        id: textEditor

        AppTextField {
            text: root.parameter.value
            font.family: root.consoleFontFamily
            font.pointSize: root.consoleFontSize
            selectByMouse: true
            onEditingFinished: root.appContext.configuration.setParameterValue(root.parameter.key, text)
        }
    }

    Component {
        id: integerEditor

        AppSpinBox {
            from: root.parameter.minimum === undefined ? -2147483647 : root.parameter.minimum
            to: root.parameter.maximum === undefined ? 2147483647 : root.parameter.maximum
            value: Number(root.parameter.value)
            font.family: root.consoleFontFamily
            font.pointSize: root.consoleFontSize
            editable: true
            onValueModified: root.appContext.configuration.setParameterValue(root.parameter.key, value)
        }
    }

    Component {
        id: integerOptionalEditor

        AppTextField {
            text: root.parameter.value
            placeholderText: qsTr("留空使用默认值")
            font.family: root.consoleFontFamily
            font.pointSize: root.consoleFontSize
            validator: IntValidator {
                bottom: root.parameter.minimum === undefined ? 0 : root.parameter.minimum
                top: root.parameter.maximum === undefined ? 2147483647 : root.parameter.maximum
            }
            onEditingFinished: root.appContext.configuration.setParameterValue(root.parameter.key, text)
        }
    }

    Component {
        id: realEditor

        AppTextField {
            text: String(root.parameter.value)
            font.family: root.consoleFontFamily
            font.pointSize: root.consoleFontSize
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            validator: DoubleValidator {
                bottom: root.parameter.minimum === undefined ? -1.0e12 : root.parameter.minimum
                top: root.parameter.maximum === undefined ? 1.0e12 : root.parameter.maximum
                notation: DoubleValidator.StandardNotation
            }
            onEditingFinished: root.appContext.configuration.setParameterValue(root.parameter.key, Number(text))
        }
    }

    Component {
        id: realOptionalEditor

        AppTextField {
            text: root.parameter.value
            placeholderText: qsTr("留空使用默认值")
            font.family: root.consoleFontFamily
            font.pointSize: root.consoleFontSize
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            validator: DoubleValidator {
                bottom: root.parameter.minimum === undefined ? 0 : root.parameter.minimum
                top: root.parameter.maximum === undefined ? 1.0e12 : root.parameter.maximum
                notation: DoubleValidator.StandardNotation
            }
            onEditingFinished: root.appContext.configuration.setParameterValue(root.parameter.key, text)
        }
    }

    Component {
        id: folderEditor

        PathField {
            appContext: root.appContext
            pathValue: root.parameter.value
            folderMode: true
            onPathEdited: value => root.appContext.configuration.setParameterValue(root.parameter.key, value)
        }
    }

    Component {
        id: fileEditor

        PathField {
            appContext: root.appContext
            pathValue: root.parameter.value
            onPathEdited: value => root.appContext.configuration.setParameterValue(root.parameter.key, value)
        }
    }

    Component {
        id: multilineEditor

        AppTextArea {
            implicitHeight: 92
            text: root.parameter.value
            placeholderText: qsTr("每行一项")
            font.family: root.consoleFontFamily
            font.pointSize: root.consoleFontSize
            selectByMouse: true
            wrapMode: root.consoleWordWrap ? TextEdit.WrapAnywhere : TextEdit.NoWrap
            onEditingFinished: root.appContext.configuration.setParameterValue(root.parameter.key, text)
        }
    }
}
