import QtQuick
import QtQuick.Controls as Controls

Controls.TextArea {
    id: control

    font.family: Theme.uiFontFamily
    color: control.enabled ? Theme.foreground : Theme.foregroundSecondary
    placeholderTextColor: Theme.foregroundSecondary
    palette.window: Theme.surfaceRaised
    palette.windowText: Theme.foreground
    palette.text: Theme.foreground
    palette.button: Theme.surfaceRaised
    palette.buttonText: Theme.foreground
    palette.accent: Theme.accent
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentForeground
    palette.placeholderText: Theme.foregroundSecondary
}
