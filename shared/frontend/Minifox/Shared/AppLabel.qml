import QtQuick

Text {
    id: control

    color: control.enabled ? Theme.foreground : Theme.foregroundSecondary
    font.family: Theme.uiFontFamily
    Accessible.name: text
}
