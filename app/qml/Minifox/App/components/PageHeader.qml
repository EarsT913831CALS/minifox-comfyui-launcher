import QtQuick
import QtQuick.Layouts

RowLayout {
    id: root

    required property string title
    property string description
    property string icon

    spacing: Theme.spacingMd

    IconLabel {
        visible: root.icon.length > 0
        glyph: root.icon
        iconPointSize: Theme.displaySize
        color: palette.highlight
        Layout.alignment: Qt.AlignTop
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingXs

        AppLabel {
            text: root.title
            font.pointSize: Theme.titleSize
            font.weight: Font.DemiBold
            Layout.fillWidth: true
        }

        AppLabel {
            visible: root.description.length > 0
            text: root.description
            color: Theme.foregroundSecondary
            font.pointSize: Theme.bodySize
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }
}
