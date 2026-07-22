import QtQuick
import QtQuick.Controls
import Minifox.Shared

ApplicationWindow {
    id: window

    required property var appContext
    property bool allowClose: false

    width: 1280
    height: 800
    minimumWidth: 900
    minimumHeight: 620
    visible: true
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
        if (appContext.runtime.active && !allowClose) {
            close.accepted = false;
            if (!closeConfirmation.visible)
                closeConfirmation.open();
        }
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
        value: window.appContext.settings.effectiveAccentColor
    }

    Binding {
        target: Theme
        property: "reducedMotion"
        value: window.appContext.settings.reducedMotion
    }

    AppShell {
        anchors.fill: parent
        appContext: window.appContext
    }

    AppDialog {
        id: closeConfirmation

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
}
