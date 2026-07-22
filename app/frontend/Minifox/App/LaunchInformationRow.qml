import QtQuick
import QtQuick.Layouts
import Minifox.Shared

RowLayout {
    required property string label
    required property string value

    spacing: Theme.spacingLg

    AppLabel {
        text: parent.label
        color: Theme.foregroundSecondary
        Layout.preferredWidth: 118
    }

    AppLabel {
        text: parent.value.length > 0 ? parent.value : "—"
        color: Theme.foreground
        elide: Text.ElideMiddle
        horizontalAlignment: Text.AlignRight
        Layout.fillWidth: true
    }
}
