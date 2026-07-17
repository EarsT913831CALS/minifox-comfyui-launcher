import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Control {
    id: root

    required property var appContext
    property int currentPage: 0
    readonly property bool compactNavigation: width < 1050

    padding: 0
    font.family: Theme.uiFontFamily
    font.pointSize: Theme.bodySize
    palette.window: Theme.surface
    palette.windowText: Theme.foreground
    palette.base: Theme.surfaceRaised
    palette.alternateBase: Theme.surfaceSubtle
    palette.text: Theme.foreground
    palette.button: Theme.surfaceRaised
    palette.buttonText: Theme.foreground
    palette.accent: Theme.accent
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentForeground
    palette.brightText: Theme.error
    palette.light: Theme.surfaceRaised
    palette.midlight: Theme.surfaceSubtle
    palette.mid: Theme.outline
    palette.dark: Theme.outline
    palette.shadow: Theme.shadow
    palette.toolTipBase: Theme.surfaceRaised
    palette.toolTipText: Theme.foreground
    palette.placeholderText: Theme.foregroundSecondary
    palette.link: Theme.info
    palette.linkVisited: Theme.info

    function pageSource(index) {
        switch (index) {
        case 0:
            return "pages/DashboardPage.qml";
        case 1:
            return "pages/ConfigurationPage.qml";
        case 2:
            return "pages/RuntimePage.qml";
        case 3:
            return "pages/SettingsPage.qml";
        default:
            return "pages/DashboardPage.qml";
        }
    }

    onCurrentPageChanged: pageLoader.setSource(pageSource(currentPage), {
        "appContext": appContext
    })
    Component.onCompleted: pageLoader.setSource(pageSource(currentPage), {
        "appContext": appContext
    })

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ToolBar {
            Layout.fillWidth: true
            Layout.preferredHeight: 56

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingMd
                anchors.rightMargin: Theme.spacingMd
                spacing: Theme.spacingMd

                IconLabel {
                    glyph: "\uE7F4"
                    iconPointSize: Theme.titleSize
                    color: palette.highlight
                }

                AppLabel {
                    text: qsTr("Minifox")
                    font.pointSize: Theme.subtitleSize
                    font.weight: Font.DemiBold
                }

                ToolSeparator {}

                AppComboBox {
                    id: profileSelector
                    Layout.preferredWidth: root.compactNavigation ? 180 : 240
                    model: root.appContext.configuration.profileNames
                    currentIndex: root.appContext.configuration.currentProfileIndex
                    Accessible.name: qsTr("当前启动配置")
                    onActivated: index => root.appContext.configuration.currentProfileIndex = index
                }

                Item {
                    Layout.fillWidth: true
                }

                StatusBadge {
                    text: root.appContext.runtime.statusText
                    icon: root.appContext.runtime.serviceReady ? "\uE73E" : "\uE711"
                    statusColor: root.appContext.runtime.serviceReady ? Theme.success : root.appContext.runtime.status === 4 ? Theme.error : Theme.foregroundSecondary
                }

                AppButton {
                    text: qsTr("启动")
                    icon.name: "media-playback-start"
                    accented: true
                    enabled: root.appContext.runtime.canStart && root.appContext.configuration.valid
                    onClicked: root.appContext.runtime.start()
                }

                AppButton {
                    text: qsTr("停止")
                    icon.name: "media-playback-stop"
                    destructive: true
                    enabled: root.appContext.runtime.canStop
                    onClicked: root.appContext.runtime.stop()
                }

                AppToolButton {
                    text: "\uE774"
                    font.family: Theme.iconFontFamily
                    font.pointSize: Theme.subtitleSize
                    enabled: root.appContext.runtime.serviceReady
                    Accessible.name: qsTr("打开 WebUI")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("打开 WebUI")
                    onClicked: root.appContext.runtime.openWebUi()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            NavigationRail {
                currentIndex: root.currentPage
                compact: root.compactNavigation
                Layout.fillHeight: true
                Layout.preferredWidth: root.compactNavigation ? 72 : 224
                onPageSelected: index => root.currentPage = index
            }

            ToolSeparator {
                orientation: Qt.Vertical
                Layout.fillHeight: true
            }

            Loader {
                id: pageLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                asynchronous: true

                onLoaded: {
                    pageFade.stop();
                    if (Theme.reducedMotion) {
                        opacity = 1;
                    } else {
                        opacity = 0;
                        pageFade.start();
                    }
                }

                OpacityAnimator {
                    id: pageFade
                    target: pageLoader
                    from: 0
                    to: 1
                    duration: Theme.motionDuration
                    easing.type: Easing.OutCubic
                }
            }
        }
    }
}
