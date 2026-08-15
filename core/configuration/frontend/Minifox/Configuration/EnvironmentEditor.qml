pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Minifox.Shared

ColumnLayout {
    id: root

    required property var appContext
    readonly property string consoleFontFamily: appContext.settings.consoleFontFamily
    readonly property real consoleFontSize: appContext.settings.consoleFontSize
    readonly property var environmentEntries: appContext.configuration.environmentEntries
    spacing: Theme.spacingMd

    AppLabel {
        text: qsTr("在启动 ComfyUI 时覆盖子进程环境。变量名只能包含英文字母、数字和下划线，值可以留空。")
        color: Theme.foregroundSecondary
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }

    AppLabel {
        visible: root.environmentEntries.length === 0
        text: qsTr("当前配置没有自定义环境变量。")
        color: Theme.foregroundSecondary
        Layout.fillWidth: true
    }

    Repeater {
        id: environmentRepeater

        model: root.environmentEntries

        delegate: Frame {
            id: environmentEntry

            required property int index
            required property var modelData
            readonly property string errorText: modelData.error

            function focusName() {
                nameInput.forceActiveFocus();
                nameInput.selectAll();
            }

            Layout.fillWidth: true
            padding: Theme.spacingSm

            ColumnLayout {
                anchors.fill: parent
                spacing: Theme.spacingSm

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    AppSwitch {
                        id: enabledSwitch
                        checked: environmentEntry.modelData.enabled
                        Accessible.name: qsTr("启用环境变量")
                        KeyNavigation.right: nameInput
                        onToggled: root.appContext.configuration.updateEnvironmentEntry(environmentEntry.index, nameInput.text, valueInput.text, checked)
                    }

                    AppTextField {
                        id: nameInput
                        Layout.preferredWidth: 220
                        text: environmentEntry.modelData.name
                        placeholderText: qsTr("变量名")
                        font.family: root.consoleFontFamily
                        font.pointSize: root.consoleFontSize
                        validator: RegularExpressionValidator {
                            regularExpression: /[A-Za-z_][A-Za-z0-9_]*/
                        }
                        Accessible.description: environmentEntry.errorText
                        KeyNavigation.left: enabledSwitch
                        KeyNavigation.right: valueInput
                        onEditingFinished: root.appContext.configuration.updateEnvironmentEntry(environmentEntry.index, text, valueInput.text, enabledSwitch.checked)
                    }

                    AppTextField {
                        id: valueInput
                        Layout.fillWidth: true
                        text: environmentEntry.modelData.value
                        placeholderText: qsTr("值")
                        font.family: root.consoleFontFamily
                        font.pointSize: root.consoleFontSize
                        selectByMouse: true
                        KeyNavigation.left: nameInput
                        KeyNavigation.right: removeButton
                        onEditingFinished: root.appContext.configuration.updateEnvironmentEntry(environmentEntry.index, nameInput.text, text, enabledSwitch.checked)
                    }

                    AppToolButton {
                        id: removeButton
                        text: "\uE74D"
                        destructive: true
                        compact: true
                        font.family: Theme.iconFontFamily
                        Accessible.name: qsTr("删除环境变量")
                        KeyNavigation.left: valueInput
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("删除")
                        onClicked: root.appContext.configuration.removeEnvironmentEntry(environmentEntry.index)
                    }
                }

                AppLabel {
                    visible: environmentEntry.errorText.length > 0
                    text: environmentEntry.errorText
                    color: Theme.error
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
        }
    }

    AppButton {
        text: qsTr("添加环境变量")
        compact: true
        Layout.alignment: Qt.AlignLeft
        onClicked: {
            const index = root.appContext.configuration.addEnvironmentEntry();
            Qt.callLater(function() {
                const item = environmentRepeater.itemAt(index);
                if (item)
                    item.focusName();
            });
        }
    }
}
