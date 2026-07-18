import QtQuick
import QtQuick.Controls
import Minifox.Shared

ApplicationWindow {
    id: window

    required property var appContext

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
}
