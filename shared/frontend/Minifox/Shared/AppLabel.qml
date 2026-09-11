import QtQuick

Text {
    id: control
    textFormat: Text.PlainText

    color: control.enabled ? Theme.foreground : Theme.foregroundSecondary
    font.family: Theme.uiFontFamily
    Accessible.name: text
}
