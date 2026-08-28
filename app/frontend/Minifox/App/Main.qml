import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Minifox.Shared

ApplicationWindow {
    id: window

    required property var appContext
    property bool allowClose: false
    readonly property real availableAspectRatio: Screen.desktopAvailableHeight > 0
                                                 ? Screen.desktopAvailableWidth
                                                   / Screen.desktopAvailableHeight
                                                 : 1.6
    readonly property real effectiveAspectRatio: {
        switch (window.appContext.settings.windowAspectRatio) {
        case "16:9": return 16 / 9;
        case "3:2": return 3 / 2;
        case "4:3": return 4 / 3;
        case "16:10": return 16 / 10;
        default: return window.availableAspectRatio;
        }
    }
    width: 1440
    height: 900
    minimumWidth: Math.max(1024, Math.round(minimumHeight * effectiveAspectRatio))
    minimumHeight: 700
    visible: true
    flags: Qt.Window | Qt.FramelessWindowHint
    title: qsTr("Minifox ComfyUI 启动器")
    color: Theme.surface
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

    onClosing: close => {
        if (!appContext.configuration.savePendingChanges()) {
            close.accepted = false;
            if (!saveErrorDialog.visible)
                saveErrorDialog.open();
            return;
        }
        if (appContext.runtime.active && !allowClose) {
            close.accepted = false;
            if (!closeConfirmation.visible)
                closeConfirmation.open();
        }
    }

    Binding {
        target: Theme
        property: "skin"
        value: window.appContext.skins.effectiveAppearance
    }

    Binding {
        target: Theme
        property: "themeMode"
        value: window.appContext.settings.themeMode
    }

    Binding {
        target: Theme
        property: "baseFontSize"
        value: window.appContext.settings.fontPointSize
    }

    Binding {
        target: Theme
        property: "uiFontFamily"
        value: window.appContext.settings.effectiveFontFamily
    }

    Binding {
        target: Theme
        property: "systemDark"
        value: window.appContext.settings.effectiveDark
    }

    Binding {
        target: Theme
        property: "accent"
        value: window.appContext.skins.effectiveAppearance.accent
               || window.appContext.settings.effectiveAccentColor
    }

    Binding {
        target: Theme
        property: "reducedMotion"
        value: window.appContext.settings.reducedMotion
    }

    Binding {
        target: window.appContext.windowChrome
        property: "aspectRatio"
        value: window.effectiveAspectRatio
    }

    Component.onCompleted: {
        window.appContext.windowChrome.attach(window);
    }

    SkinImage {
        anchors.fill: parent
        source: Theme.backgroundSource
        visible: source.toString().length > 0
        opacity: Theme.backgroundOpacity
        fillMode: Theme.backgroundFillMode
        focusX: Theme.backgroundFocusX
        focusY: Theme.backgroundFocusY
        zoom: Theme.backgroundZoom
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.backgroundSource.toString().length > 0
               ? Theme.backgroundOverlay : Theme.surface
    }

    Item {
        id: designSurface

        anchors.fill: parent

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            AppTitleBar {
                window: window
                appContext: window.appContext
                Layout.fillWidth: true
                Layout.preferredHeight: 48
            }

            AppShell {
                appContext: window.appContext
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }

        AppDialog {
            id: closeConfirmation

            parent: designSurface
            anchors.centerIn: parent
            title: qsTr("ComfyUI 正在运行")
            modal: true
            standardButtons: Dialog.Yes | Dialog.Cancel
            acceptText: qsTr("停止并退出")
            rejectText: qsTr("取消")
            closePolicy: Popup.CloseOnEscape

            AppLabel {
                width: closeConfirmation.availableWidth
                text: qsTr("关闭启动器会停止正在运行的 ComfyUI，并释放其占用的浏览器端口。确定要退出吗？")
                wrapMode: Text.Wrap
            }

            onAccepted: {
                window.allowClose = true;
                window.appContext.runtime.shutdown();
                window.close();
            }
        }

        AppDialog {
            id: saveErrorDialog

            parent: designSurface
            anchors.centerIn: parent
            title: qsTr("无法保存启动配置")
            modal: true
            standardButtons: Dialog.Ok
            acceptText: qsTr("确定")
            closePolicy: Popup.CloseOnEscape

            AppLabel {
                width: saveErrorDialog.availableWidth
                text: window.appContext.configuration.lastError
                wrapMode: Text.Wrap
            }
        }

    }
}
