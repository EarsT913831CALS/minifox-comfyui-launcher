import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root

    required property string text
    property string icon: "\uE711"
    property color statusColor: Theme.foregroundSecondary

    implicitWidth: content.implicitWidth + Theme.spacingLg
    implicitHeight: 28
    radius: implicitHeight / 2
    color: Qt.rgba(statusColor.r, statusColor.g, statusColor.b, 0.14)
    border.color: Qt.rgba(statusColor.r, statusColor.g, statusColor.b, 0.42)
    border.width: 1

    Accessible.role: Accessible.StaticText
    Accessible.name: text

    RowLayout {
        id: content
        anchors.centerIn: parent
        spacing: Theme.spacingSm

        IconLabel {
            glyph: root.icon
            iconPointSize: Theme.captionSize
            color: root.statusColor
        }

        AppLabel {
            text: root.text
            color: root.statusColor
            font.pointSize: Theme.captionSize
            font.weight: Font.DemiBold
        }
    }
}
