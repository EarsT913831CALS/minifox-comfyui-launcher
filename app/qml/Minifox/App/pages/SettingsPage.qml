pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: root

    required property var appContext
    readonly property var themeModeLabels: [qsTr("跟随系统"), qsTr("浅色"), qsTr("深色")]
    readonly property var themeModeValues: ["system", "light", "dark"]
    readonly property var languageModeLabels: [qsTr("跟随系统"), qsTr("简体中文"), qsTr("English")]
    readonly property var languageModeValues: ["system", "zh_CN", "en_US"]
    readonly property var accentModeLabels: [qsTr("跟随系统"), qsTr("自定义")]
    readonly property var accentModeValues: ["system", "custom"]
    readonly property var proxyModeLabels: [qsTr("使用系统代理"), qsTr("不使用代理"), qsTr("手动设置")]
    readonly property var proxyModeValues: ["system", "none", "manual"]
    readonly property var fontFamilies: Qt.fontFamilies()

    function labelForValue(labels, values, value) {
        const index = values.indexOf(value);
        return index >= 0 ? labels[index] : "";
    }

    padding: Theme.spacingLg

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingMd

        PageHeader {
            title: qsTr("应用设置")
            description: qsTr("调整启动器的主题、字体、控制台输出和网络代理。设置保存在程序旁的隐藏目录中。")
            icon: "\uE770"
            Layout.fillWidth: true
        }

        ScrollView {
            id: settingsScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth

            ColumnLayout {
                width: settingsScroll.availableWidth
                spacing: Theme.spacingMd

                Frame {
                    Layout.fillWidth: true
                    padding: Theme.spacingLg

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: Theme.spacingMd

                        AppLabel {
                            text: qsTr("外观")
                            font.pointSize: Theme.subtitleSize
                            font.weight: Font.DemiBold
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: Theme.spacingLg
                            rowSpacing: Theme.spacingMd

                            AppLabel {
                                text: qsTr("主题")
                            }
                            AppComboBox {
                                Layout.fillWidth: true
                                model: root.themeModeLabels
                                currentIndex: root.themeModeValues.indexOf(root.appContext.settings.themeMode)
                                displayText: root.labelForValue(root.themeModeLabels, root.themeModeValues, root.appContext.settings.themeMode)
                                onActivated: index => root.appContext.settings.themeMode = root.themeModeValues[index]
                            }

                            AppLabel {
                                text: qsTr("语言")
                            }
                            AppComboBox {
                                Layout.fillWidth: true
                                model: root.languageModeLabels
                                currentIndex: root.languageModeValues.indexOf(root.appContext.settings.language)
                                displayText: root.labelForValue(root.languageModeLabels, root.languageModeValues, root.appContext.settings.language)
                                onActivated: index => root.appContext.settings.language = root.languageModeValues[index]
                            }

                            AppLabel {
                                text: qsTr("界面字体")
                            }
                            AppComboBox {
                                Layout.fillWidth: true
                                model: root.fontFamilies
                                currentIndex: root.appContext.settings.fontFamily.length > 0 ? root.fontFamilies.indexOf(root.appContext.settings.fontFamily) : -1
                                displayText: currentIndex >= 0 ? currentText : root.appContext.settings.fontFamily.length > 0 ? root.appContext.settings.fontFamily : qsTr("系统默认")
                                onActivated: index => root.appContext.settings.fontFamily = root.fontFamilies[index]
                            }

                            AppLabel {
                                text: qsTr("界面字号")
                            }
                            AppSpinBox {
                                from: 8
                                to: 24
                                value: Math.round(root.appContext.settings.fontPointSize)
                                editable: true
                                onValueModified: root.appContext.settings.fontPointSize = value
                            }

                            AppLabel {
                                text: qsTr("强调色")
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingSm

                                AppComboBox {
                                    Layout.fillWidth: true
                                    model: root.accentModeLabels
                                    currentIndex: root.accentModeValues.indexOf(root.appContext.settings.accentMode)
                                    displayText: root.labelForValue(root.accentModeLabels, root.accentModeValues, root.appContext.settings.accentMode)
                                    onActivated: index => root.appContext.settings.accentMode = root.accentModeValues[index]
                                }

                                Rectangle {
                                    Layout.preferredWidth: 28
                                    Layout.preferredHeight: 28
                                    radius: 4
                                    color: root.appContext.settings.effectiveAccentColor
                                    border.width: 1
                                    border.color: Theme.outline
                                    Accessible.ignored: true
                                }

                                AppLabel {
                                    text: root.appContext.settings.effectiveAccentColor.toUpperCase()
                                    font.family: root.appContext.settings.consoleFontFamily
                                    font.pointSize: root.appContext.settings.consoleFontSize
                                }

                                AppButton {
                                    visible: root.appContext.settings.accentMode === "custom"
                                    text: qsTr("选择颜色…")
                                    onClicked: accentColorDialogLoader.active = true
                                }
                            }

                            AppLabel {
                                text: qsTr("减少动态效果")
                            }
                            AppSwitch {
                                text: checked ? qsTr("已启用") : qsTr("未启用")
                                checked: root.appContext.settings.reducedMotion
                                onToggled: root.appContext.settings.reducedMotion = checked
                            }
                        }

                        AppLabel {
                            Layout.fillWidth: true
                            text: root.appContext.settings.reducedMotion ? qsTr("页面切换动画已关闭。") : qsTr("页面切换时使用短暂淡入动画。")
                            color: Theme.foregroundSecondary
                            wrapMode: Text.WordWrap
                        }

                        AppLabel {
                            Layout.fillWidth: true
                            text: qsTr("字体预览：小狐狸正在检查中文字体 ABC 123")
                            color: Theme.foreground
                            font.family: Theme.uiFontFamily
                            font.pointSize: Theme.bodySize
                        }
                    }
                }

                Frame {
                    Layout.fillWidth: true
                    padding: Theme.spacingLg

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: Theme.spacingMd

                        AppLabel {
                            text: qsTr("控制台输出")
                            font.pointSize: Theme.subtitleSize
                            font.weight: Font.DemiBold
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: Theme.spacingLg
                            rowSpacing: Theme.spacingMd

                            AppLabel {
                                text: qsTr("控制台主题")
                            }
                            AppComboBox {
                                Layout.fillWidth: true
                                model: root.themeModeLabels
                                currentIndex: root.themeModeValues.indexOf(root.appContext.settings.consoleTheme)
                                displayText: root.labelForValue(root.themeModeLabels, root.themeModeValues, root.appContext.settings.consoleTheme)
                                onActivated: index => root.appContext.settings.consoleTheme = root.themeModeValues[index]
                            }

                            AppLabel {
                                text: qsTr("等宽字体")
                            }
                            AppComboBox {
                                Layout.fillWidth: true
                                model: root.fontFamilies
                                currentIndex: root.fontFamilies.indexOf(root.appContext.settings.consoleFontFamily)
                                displayText: currentIndex >= 0 ? currentText : root.appContext.settings.consoleFontFamily
                                onActivated: index => root.appContext.settings.consoleFontFamily = root.fontFamilies[index]
                            }

                            AppLabel {
                                text: qsTr("控制台字号")
                            }
                            AppSpinBox {
                                from: 7
                                to: 24
                                value: Math.round(root.appContext.settings.consoleFontSize)
                                editable: true
                                onValueModified: root.appContext.settings.consoleFontSize = value
                            }

                            AppLabel {
                                text: qsTr("自动换行")
                            }
                            AppSwitch {
                                text: checked ? qsTr("已启用") : qsTr("未启用")
                                checked: root.appContext.settings.consoleWordWrap
                                onToggled: root.appContext.settings.consoleWordWrap = checked
                            }

                            AppLabel {
                                text: qsTr("显示时间戳")
                            }
                            AppSwitch {
                                text: checked ? qsTr("已启用") : qsTr("未启用")
                                checked: root.appContext.settings.showTimestamps
                                onToggled: root.appContext.settings.showTimestamps = checked
                            }
                        }

                        ConsoleSettingsPreview {
                            Layout.fillWidth: true
                            appContext: root.appContext
                        }
                    }
                }

                Frame {
                    Layout.fillWidth: true
                    padding: Theme.spacingLg

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: Theme.spacingMd

                        AppLabel {
                            text: qsTr("网络代理")
                            font.pointSize: Theme.subtitleSize
                            font.weight: Font.DemiBold
                        }

                        AppLabel {
                            Layout.fillWidth: true
                            text: qsTr("代理会应用到启动器网络请求，并写入 ComfyUI 子进程的 HTTP_PROXY、HTTPS_PROXY 与 ALL_PROXY 环境变量。")
                            color: Theme.foregroundSecondary
                            wrapMode: Text.WordWrap
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: Theme.spacingLg
                            rowSpacing: Theme.spacingMd

                            AppLabel {
                                text: qsTr("代理模式")
                            }
                            AppComboBox {
                                Layout.fillWidth: true
                                model: root.proxyModeLabels
                                currentIndex: root.proxyModeValues.indexOf(root.appContext.settings.proxyMode)
                                displayText: root.labelForValue(root.proxyModeLabels, root.proxyModeValues, root.appContext.settings.proxyMode)
                                onActivated: index => root.appContext.settings.proxyMode = root.proxyModeValues[index]
                            }

                            AppLabel {
                                visible: root.appContext.settings.proxyMode === "manual"
                                text: qsTr("代理主机")
                            }
                            AppTextField {
                                visible: root.appContext.settings.proxyMode === "manual"
                                Layout.fillWidth: true
                                text: root.appContext.settings.proxyHost
                                placeholderText: "127.0.0.1"
                                font.family: root.appContext.settings.consoleFontFamily
                                font.pointSize: root.appContext.settings.consoleFontSize
                                onEditingFinished: root.appContext.settings.proxyHost = text
                            }

                            AppLabel {
                                visible: root.appContext.settings.proxyMode === "manual"
                                text: qsTr("代理端口")
                            }
                            AppSpinBox {
                                visible: root.appContext.settings.proxyMode === "manual"
                                from: 1
                                to: 65535
                                value: root.appContext.settings.proxyPort
                                font.family: root.appContext.settings.consoleFontFamily
                                font.pointSize: root.appContext.settings.consoleFontSize
                                editable: true
                                onValueModified: root.appContext.settings.proxyPort = value
                            }
                        }

                        AppLabel {
                            Layout.fillWidth: true
                            text: root.appContext.settings.proxyMode === "manual" ? (root.appContext.settings.proxyHost.length > 0 ? qsTr("ComfyUI 子进程将使用代理：%1:%2").arg(root.appContext.settings.proxyHost).arg(root.appContext.settings.proxyPort) : qsTr("请填写代理主机；主机为空时不会启用手动代理。")) : root.appContext.settings.proxyMode === "none" ? qsTr("ComfyUI 子进程中的代理环境变量将被移除。") : qsTr("ComfyUI 子进程将继承系统代理环境。")
                            color: root.appContext.settings.proxyMode === "manual" && root.appContext.settings.proxyHost.length === 0 ? Theme.warning : Theme.foregroundSecondary
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                AppLabel {
                    visible: root.appContext.settings.lastError.length > 0
                    Layout.fillWidth: true
                    text: root.appContext.settings.lastError
                    color: Theme.error
                    wrapMode: Text.WordWrap
                }

                Item {
                    Layout.minimumHeight: Theme.spacingLg
                }
            }
        }
    }

    Loader {
        id: accentColorDialogLoader
        active: false
        sourceComponent: accentColorDialogComponent
    }

    Component {
        id: accentColorDialogComponent

        AccentColorDialog {
            selectedColor: root.appContext.settings.accentColor
            Component.onCompleted: open()
            onColorSelected: selectedColor => root.appContext.settings.accentColor = selectedColor.toString()
            onClosed: accentColorDialogLoader.active = false
        }
    }
}
