pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Pane {
    id: root

    required property var appContext
    property bool exportRequested: false

    padding: Theme.spacingLg

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingMd

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMd

            PageHeader {
                title: qsTr("运行与控制台")
                description: qsTr("启动、停止并监控当前 ComfyUI 实例；进度在控制台底部单独显示。")
                icon: "\uE756"
                Layout.fillWidth: true
            }

            AppButton {
                text: qsTr("导出日志")
                enabled: root.appContext.runtime.logModel.count > 0
                onClicked: root.exportRequested = true
            }

            AppButton {
                text: qsTr("打开 WebUI")
                enabled: root.appContext.runtime.serviceReady
                onClicked: root.appContext.runtime.openWebUi()
            }

            AppButton {
                text: root.appContext.runtime.canStart ? qsTr("启动") : qsTr("停止")
                accented: root.appContext.runtime.canStart
                destructive: !root.appContext.runtime.canStart
                enabled: root.appContext.runtime.canStart ? root.appContext.configuration.valid : root.appContext.runtime.canStop
                onClicked: {
                    if (root.appContext.runtime.canStart)
                        root.appContext.runtime.start();
                    else
                        root.appContext.runtime.stop();
                }
            }

            AppToolButton {
                visible: root.appContext.runtime.status === 3
                text: "\uE7E8"
                destructive: true
                font.family: Theme.iconFontFamily
                Accessible.name: qsTr("强制终止进程树")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("强制终止进程树")
                onClicked: root.appContext.runtime.forceStop()
            }
        }

        Frame {
            Layout.fillWidth: true
            padding: Theme.spacingMd

            RowLayout {
                anchors.fill: parent
                spacing: Theme.spacingXl

                StatusBadge {
                    text: root.appContext.runtime.statusText
                    icon: root.appContext.runtime.serviceReady ? "\uE73E" : "\uE711"
                    statusColor: root.appContext.runtime.serviceReady ? Theme.success : root.appContext.runtime.status === 4 ? Theme.error : Theme.foregroundSecondary
                }

                AppLabel {
                    text: qsTr("PID %1").arg(root.appContext.runtime.processId > 0 ? root.appContext.runtime.processId : "—")
                    color: Theme.foregroundSecondary
                    font.family: root.appContext.settings.consoleFontFamily
                    font.pointSize: root.appContext.settings.consoleFontSize
                }

                AppLabel {
                    text: qsTr("运行 %1").arg(root.appContext.runtime.uptime)
                    color: Theme.foregroundSecondary
                    font.family: root.appContext.settings.consoleFontFamily
                    font.pointSize: root.appContext.settings.consoleFontSize
                }

                AppLabel {
                    text: root.appContext.runtime.serviceUrl
                    color: palette.highlight
                    font.family: root.appContext.settings.consoleFontFamily
                    font.pointSize: root.appContext.settings.consoleFontSize
                }

                Item {
                    Layout.fillWidth: true
                }

                AppLabel {
                    text: qsTr("%1 行").arg(root.appContext.runtime.logModel.count)
                    color: Theme.foregroundSecondary
                }
            }
        }

        Frame {
            visible: root.appContext.runtime.lastError.length > 0
            Layout.fillWidth: true
            padding: Theme.spacingSm

            RowLayout {
                anchors.fill: parent
                IconLabel {
                    glyph: "\uE783"
                    color: Theme.error
                }
                AppLabel {
                    text: root.appContext.runtime.lastError
                    color: Theme.error
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
        }

        ConsoleView {
            appContext: root.appContext
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }

    Loader {
        active: root.exportRequested
        sourceComponent: exportDialogComponent
    }

    Component {
        id: exportDialogComponent

        FileDialog {
            title: qsTr("导出控制台日志")
            fileMode: FileDialog.SaveFile
            defaultSuffix: "log"
            nameFilters: [qsTr("日志文件 (*.log)"), qsTr("文本文件 (*.txt)")]
            Component.onCompleted: open()
            onAccepted: {
                root.appContext.runtime.exportLog(selectedFile);
                root.exportRequested = false;
            }
            onRejected: root.exportRequested = false
        }
    }
}
