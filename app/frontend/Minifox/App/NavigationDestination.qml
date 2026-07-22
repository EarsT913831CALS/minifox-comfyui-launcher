import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Minifox.Shared

AppItemDelegate {
    id: control

    required property int destinationIndex
    required property string destinationTitle
    required property string destinationIcon
    property bool compact: false
    property bool current: false
    signal destinationSelected(int index)

    implicitHeight: compact ? 56 : 76
    highlighted: current
    activeFocusOnTab: true
    Accessible.name: destinationTitle
    ToolTip.visible: compact && hovered
    ToolTip.text: destinationTitle
    onClicked: destinationSelected(destinationIndex)

    background: Rectangle {
        radius: Theme.radius
        color: control.current
               ? Theme.navigationSelected
               : control.hovered
                 ? Theme.navigationHovered
                 : "transparent"
        border.width: control.visualFocus ? 2 : 0
        border.color: Theme.accent

        Rectangle {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 3
            height: control.current ? 26 : 0
            radius: 2
            color: Theme.accent

        }
    }

    contentItem: ColumnLayout {
        spacing: 5

        IconLabel {
            glyph: control.destinationIcon
            iconPointSize: control.compact ? Theme.subtitleSize : Theme.titleSize
            color: control.current ? Theme.accent : Theme.foreground
            Layout.alignment: Qt.AlignHCenter
        }

        AppLabel {
            visible: !control.compact
            text: control.destinationTitle
            color: Theme.foreground
            font.pointSize: Theme.captionSize
            font.weight: control.current ? Font.DemiBold : Font.Normal
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }
    }
}
