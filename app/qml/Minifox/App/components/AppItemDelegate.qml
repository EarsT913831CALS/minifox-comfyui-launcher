import QtQuick
import QtQuick.Controls as Controls

Controls.ItemDelegate {
    id: control

    font.family: Theme.uiFontFamily
    palette.window: Theme.surfaceRaised
    palette.windowText: Theme.foreground
    palette.text: Theme.foreground
    palette.button: Theme.surfaceRaised
    palette.buttonText: Theme.foreground
    palette.accent: Theme.accent
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentForeground
    palette.placeholderText: Theme.foregroundSecondary

    Binding {
        target: control.contentItem
        property: "color"
        value: control.enabled ? Theme.foreground : Theme.foregroundSecondary
        when: control.contentItem !== null
    }
}
