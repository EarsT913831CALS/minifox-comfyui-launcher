pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    required property var appContext
    readonly property string consoleFontFamily: appContext.settings.consoleFontFamily
    readonly property real consoleFontSize: appContext.settings.consoleFontSize
    spacing: Theme.spacingMd

    AppLabel {
        text: qsTr("环境变量会在主命令之前写入子进程环境。常用的 UTF-8 与无缓冲输出变量已默认提供，也可添加任意自定义项。")
        color: Theme.foregroundSecondary
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }

    Repeater {
        model: root.appContext.configuration.environmentEntries

        delegate: Frame {
            id: environmentEntry

            required property int index
            required property var modelData

            Layout.fillWidth: true
            padding: Theme.spacingSm

            RowLayout {
                anchors.fill: parent
                spacing: Theme.spacingSm

                AppSwitch {
                    id: enabledSwitch
                    checked: environmentEntry.modelData.enabled
                    Accessible.name: qsTr("启用环境变量")
                    onToggled: root.appContext.configuration.updateEnvironmentEntry(environmentEntry.index, nameInput.text, valueInput.text, checked)
                }

                AppTextField {
                    id: nameInput
                    Layout.preferredWidth: 220
                    text: environmentEntry.modelData.name
                    placeholderText: qsTr("变量名")
                    font.family: root.consoleFontFamily
                    font.pointSize: root.consoleFontSize
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
                    onEditingFinished: root.appContext.configuration.updateEnvironmentEntry(environmentEntry.index, nameInput.text, text, enabledSwitch.checked)
                }

                AppToolButton {
                    text: "\uE74D"
                    destructive: true
                    font.family: Theme.iconFontFamily
                    Accessible.name: qsTr("删除环境变量")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("删除")
                    onClicked: root.appContext.configuration.removeEnvironmentEntry(environmentEntry.index)
                }
            }
        }
    }

    AppButton {
        text: qsTr("添加环境变量")
        onClicked: root.appContext.configuration.addEnvironmentEntry()
    }
}
