pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Minifox.Shared

Pane {
    id: root

    required property var appContext
    property int currentSection: 0
    readonly property var themeModeLabels: [qsTr("跟随系统"), qsTr("浅色"), qsTr("深色")]
    readonly property var themeModeValues: ["system", "light", "dark"]
    readonly property var iconModeLabels: appContext.appIcon.customIconAvailable
                                                  ? [
                                                        qsTr("跟随主题"),
                                                        qsTr("固定亮色"),
                                                        qsTr("固定暗色"),
                                                        qsTr("自定义")
                                                    ]
                                                  : [
                                                        qsTr("跟随主题"),
                                                        qsTr("固定亮色"),
                                                        qsTr("固定暗色")
                                                    ]
    readonly property var iconModeValues: appContext.appIcon.customIconAvailable
                                                  ? ["theme", "light", "dark", "custom"]
                                                  : ["theme", "light", "dark"]
    readonly property var languageModeLabels: [qsTr("跟随系统"), qsTr("简体中文"), qsTr("English")]
    readonly property var languageModeValues: ["system", "zh_CN", "en_US"]
    readonly property var accentModeLabels: [qsTr("跟随系统"), qsTr("自定义")]
    readonly property var accentModeValues: ["system", "custom"]
    readonly property var windowRatioLabels: [
        qsTr("跟随屏幕可用区域（无黑边）"),
        qsTr("16:10"),
        qsTr("16:9"),
        qsTr("3:2"),
        qsTr("4:3")
    ]
    readonly property var windowRatioValues: ["screen", "16:10", "16:9", "3:2", "4:3"]
    readonly property var proxyModeLabels: [qsTr("使用系统代理"), qsTr("不使用代理"), qsTr("手动设置")]
    readonly property var proxyModeValues: ["system", "none", "manual"]
    readonly property var fontFamilies: Qt.fontFamilies()

    function labelForValue(labels, values, value) {
        const index = values.indexOf(value);
        return index >= 0 ? labels[index] : "";
    }

    // Changing the window ratio changes which part of the background image
    // stays visible. Re-center the cover crop automatically and point the
    // user at the skin page for fine-tuning.
    function recenterBackgroundCrop() {
        const appearance = appContext.skins.effectiveAppearance;
        const background = appearance.background || ({});
        if ((appearance.backgroundSource || "").length === 0)
            return;
        if ((background.fillMode || "cover") !== "cover")
            return;
        appContext.skins.setAppearanceValue("background.focusX", 0.5);
        appContext.skins.setAppearanceValue("background.focusY", 0.5);
        appContext.skins.setAppearanceValue("background.zoom", 1.0);
        cropHint.visible = true;
        cropHintTimer.restart();
    }

    padding: Theme.spacingLg

    Timer {
        id: cropHintTimer

        interval: 8000
        onTriggered: cropHint.visible = false
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingMd

        PageHeader {
            title: qsTr("应用设置")
            description: qsTr("调整启动器的主题、字体、控制台输出和网络代理。设置保存在程序旁的隐藏目录中。")
            icon: "\uE770"
            Layout.fillWidth: true
        }

        TabBar {
            Layout.fillWidth: true
            currentIndex: root.currentSection
            onCurrentIndexChanged: root.currentSection = currentIndex

            TabButton { text: qsTr("外观") }
            TabButton { text: qsTr("皮肤") }
            TabButton { text: qsTr("控制台") }
            TabButton { text: qsTr("网络") }
        }

        ScrollView {
            id: settingsScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth

            ColumnLayout {
                width: settingsScroll.availableWidth
                spacing: Theme.spacingMd

                MaterialPanel {
                    visible: root.currentSection === 0
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
                                text: qsTr("应用图标")
                                Layout.alignment: Qt.AlignTop
                                Layout.topMargin: Theme.spacingSm
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingSm

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.spacingMd

                                    ColumnLayout {
                                        spacing: Theme.spacingXs

                                        AppLabel {
                                            text: qsTr("当前运行")
                                            color: Theme.foregroundSecondary
                                            font.pointSize: Theme.captionSize
                                        }
                                        Rectangle {
                                            Layout.preferredWidth: 56
                                            Layout.preferredHeight: 56
                                            radius: Theme.controlRadius
                                            color: Theme.surfaceSubtle
                                            border.width: 1
                                            border.color: Theme.outline

                                            Image {
                                                anchors.fill: parent
                                                anchors.margins: 4
                                                source: root.appContext.appIcon.activeIconSource
                                                fillMode: Image.PreserveAspectFit
                                                smooth: true
                                                mipmap: true
                                            }
                                        }
                                    }

                                    ColumnLayout {
                                        spacing: Theme.spacingXs

                                        AppLabel {
                                            text: qsTr("下次启动")
                                            color: Theme.foregroundSecondary
                                            font.pointSize: Theme.captionSize
                                        }
                                        Rectangle {
                                            Layout.preferredWidth: 56
                                            Layout.preferredHeight: 56
                                            radius: Theme.controlRadius
                                            color: Theme.surfaceSubtle
                                            border.width: 1
                                            border.color: root.appContext.appIcon.restartRequired
                                                          ? Theme.accent : Theme.outline

                                            Image {
                                                anchors.fill: parent
                                                anchors.margins: 4
                                                source: root.appContext.appIcon.pendingIconSource
                                                fillMode: Image.PreserveAspectFit
                                                smooth: true
                                                mipmap: true
                                            }
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: Theme.spacingSm

                                        AppComboBox {
                                            Layout.fillWidth: true
                                            model: root.iconModeLabels
                                            currentIndex: root.iconModeValues.indexOf(
                                                              root.appContext.appIcon.mode)
                                            displayText: root.labelForValue(
                                                             root.iconModeLabels,
                                                             root.iconModeValues,
                                                             root.appContext.appIcon.mode)
                                            onActivated: index => root.appContext.appIcon.setMode(
                                                             root.iconModeValues[index])
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: Theme.spacingSm

                                            AppButton {
                                                Layout.preferredWidth: 148
                                                text: root.appContext.appIcon.customIconAvailable
                                                      ? qsTr("更换图片…")
                                                      : qsTr("选择图片…")
                                                onClicked: applicationIconDialog.open()
                                            }
                                            AppButton {
                                                text: qsTr("重置")
                                                enabled: root.appContext.appIcon.mode !== "theme"
                                                         || root.appContext.appIcon.customIconAvailable
                                                onClicked: root.appContext.appIcon.reset()
                                            }
                                        }
                                    }
                                }

                                AppLabel {
                                    Layout.fillWidth: true
                                    visible: root.appContext.appIcon.restartRequired
                                    text: qsTr("应用图标将在下次启动时更改。")
                                    color: Theme.info
                                    wrapMode: Text.WordWrap
                                }

                                AppLabel {
                                    Layout.fillWidth: true
                                    visible: root.appContext.appIcon.lastError.length > 0
                                    text: root.appContext.appIcon.lastError
                                    color: Theme.error
                                    wrapMode: Text.WordWrap
                                }
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
                            FontFamilyComboBox {
                                Layout.fillWidth: true
                                selectedFamily: root.appContext.settings.fontFamily
                                onFamilySelected: family =>
                                                  root.appContext.settings.fontFamily = family
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

                            AppLabel {
                                text: qsTr("窗口画面比例")
                            }
                            AppComboBox {
                                Layout.fillWidth: true
                                model: root.windowRatioLabels
                                currentIndex: root.windowRatioValues.indexOf(
                                                  root.appContext.settings.windowAspectRatio)
                                displayText: root.labelForValue(
                                                 root.windowRatioLabels,
                                                 root.windowRatioValues,
                                                 root.appContext.settings.windowAspectRatio)
                                onActivated: index => {
                                    const value = root.windowRatioValues[index];
                                    if (value === root.appContext.settings.windowAspectRatio)
                                        return;
                                    root.appContext.settings.windowAspectRatio = value;
                                    root.recenterBackgroundCrop();
                                }
                            }
                        }

                        AppLabel {
                            Layout.fillWidth: true
                            text: root.appContext.settings.windowAspectRatio === "screen"
                                  ? qsTr("窗口会采用当前屏幕的可用区域比例，最大化时不会出现用于保持比例的黑边。")
                                  : qsTr("窗口始终保持所选比例；最大化时会在屏幕工作区内居中扩展到最大尺寸，窗口内部不留黑边。")
                            color: Theme.foregroundSecondary
                            wrapMode: Text.WordWrap
                        }

                        AppLabel {
                            id: cropHint

                            Layout.fillWidth: true
                            visible: false
                            text: qsTr("窗口比例已变更，全局背景已自动居中裁剪。如需微调，请到「皮肤」页重新调整画面裁剪位置。")
                            color: Theme.info
                            wrapMode: Text.WordWrap
                        }

                        AppLabel {
                            Layout.fillWidth: true
                            text: qsTr("字体预览：小狐狸正在检查中文字体 ABC 123")
                            color: Theme.foreground
                            font.family: root.appContext.settings.effectiveFontFamily
                            font.pointSize: root.appContext.settings.fontPointSize
                        }
                    }
                }

                SkinSettingsPanel {
                    visible: root.currentSection === 1
                    Layout.fillWidth: true
                    appContext: root.appContext
                }

                MaterialPanel {
                    visible: root.currentSection === 2
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
                                text: qsTr("换行方式")
                            }
                            AppSwitch {
                                text: checked ? qsTr("任意位置换行") : qsTr("优先按单词换行")
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

                MaterialPanel {
                    visible: root.currentSection === 3
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

    FileDialog {
        id: applicationIconDialog

        title: qsTr("选择自定义应用图标")
        fileMode: FileDialog.OpenFile
        nameFilters: [
            qsTr("支持的图片 (*.png *.jpg *.jpeg *.webp *.ico)"),
            qsTr("所有文件 (*)")
        ]
        onAccepted: root.appContext.appIcon.importCustomIcon(selectedFile)
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
