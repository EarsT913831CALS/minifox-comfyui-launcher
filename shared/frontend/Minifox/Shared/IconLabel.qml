import QtQuick

AppLabel {
    required property string glyph
    property real iconPointSize: Theme.subtitleSize

    text: glyph
    font.family: Theme.iconFontFamily
    font.pointSize: iconPointSize
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
    Accessible.ignored: true
}
