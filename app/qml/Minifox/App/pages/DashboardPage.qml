import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: root

    required property var appContext
    padding: Theme.spacingLg

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingMd

        PageHeader {
            title: qsTr("主页")
            description: qsTr("检查当前配置、启动 ComfyUI，并随时查看服务状态。")
            icon: "\uE80F"
            Layout.fillWidth: true
        }

        Frame {
            Layout.fillWidth: true
            padding: Theme.spacingLg

            RowLayout {
                anchors.fill: parent
                spacing: Theme.spacingXl

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    AppLabel {
                        text: root.appContext.runtime.statusText
                        font.pointSize: Theme.displaySize
                        font.weight: Font.DemiBold
                    }

                    RowLayout {
                        StatusBadge {
                            text: root.appContext.runtime.serviceReady ? qsTr("服务已就绪") : qsTr("服务未就绪")
                            icon: root.appContext.runtime.serviceReady ? "\uE73E" : "\uE711"
                            statusColor: root.appContext.runtime.serviceReady ? Theme.success : Theme.foregroundSecondary
                        }

                        AppLabel {
                            text: root.appContext.runtime.serviceUrl
                            color: Theme.foregroundSecondary
                            font.family: root.appContext.settings.consoleFontFamily
                            font.pointSize: root.appContext.settings.consoleFontSize
                        }
                    }
                }

                ColumnLayout {
                    spacing: Theme.spacingXs
                    AppLabel {
                        text: qsTr("进程 ID")
                        color: Theme.foregroundSecondary
                    }
                    AppLabel {
                        text: root.appContext.runtime.processId > 0 ? root.appContext.runtime.processId : "—"
                        font.family: root.appContext.settings.consoleFontFamily
                        font.pointSize: root.appContext.settings.consoleFontSize
                    }
                }

                ColumnLayout {
                    spacing: Theme.spacingXs
                    AppLabel {
                        text: qsTr("运行时间")
                        color: Theme.foregroundSecondary
                    }
                    AppLabel {
                        text: root.appContext.runtime.uptime
                        font.family: root.appContext.settings.consoleFontFamily
                        font.pointSize: root.appContext.settings.consoleFontSize
                    }
                }

                AppButton {
                    text: root.appContext.runtime.canStart ? qsTr("启动 ComfyUI") : qsTr("停止 ComfyUI")
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

                AppButton {
                    text: qsTr("打开 WebUI")
                    enabled: root.appContext.runtime.serviceReady
                    onClicked: root.appContext.runtime.openWebUi()
                }
            }
        }

        Frame {
            visible: !root.appContext.configuration.valid || root.appContext.runtime.lastError.length > 0
            Layout.fillWidth: true
            padding: Theme.spacingMd

            RowLayout {
                anchors.fill: parent
                spacing: Theme.spacingSm

                IconLabel {
                    glyph: "\uE7BA"
                    color: Theme.error
                }
                AppLabel {
                    Layout.fillWidth: true
                    text: root.appContext.runtime.lastError.length > 0 ? root.appContext.runtime.lastError : root.appContext.configuration.validationErrors.join(" · ")
                    color: Theme.error
                    wrapMode: Text.Wrap
                }
            }
        }

        Frame {
            Layout.fillWidth: true
            padding: Theme.spacingMd

            ColumnLayout {
                anchors.fill: parent
                spacing: Theme.spacingSm

                RowLayout {
                    AppLabel {
                        text: qsTr("命令预览")
                        font.pointSize: Theme.subtitleSize
                        font.weight: Font.DemiBold
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    AppLabel {
                        text: qsTr("敏感环境变量会自动隐藏")
                        color: Theme.foregroundSecondary
                        font.pointSize: Theme.captionSize
                    }
                }

                AppTextArea {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 104
                    text: root.appContext.runtime.commandPreview
                    readOnly: true
                    selectByMouse: true
                    wrapMode: root.appContext.settings.consoleWordWrap ? TextEdit.WrapAnywhere : TextEdit.NoWrap
                    font.family: root.appContext.settings.consoleFontFamily
                    font.pointSize: root.appContext.settings.consoleFontSize
                    Accessible.name: qsTr("ComfyUI 启动命令预览")
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingSm

            RowLayout {
                AppLabel {
                    text: qsTr("最近输出")
                    font.pointSize: Theme.subtitleSize
                    font.weight: Font.DemiBold
                }
                Item {
                    Layout.fillWidth: true
                }
                AppLabel {
                    text: qsTr("完整内容保留至下一次启动")
                    color: Theme.foregroundSecondary
                    font.pointSize: Theme.captionSize
                }
            }

            ConsoleView {
                appContext: root.appContext
                forceCompact: true
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 120
            }
        }
    }
}
