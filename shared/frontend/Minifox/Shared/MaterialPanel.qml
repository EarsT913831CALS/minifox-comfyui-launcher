import QtQuick
import QtQuick.Controls

Frame {
    id: control

    property bool strong: false
    property bool accented: false
    property real cornerRadius: Theme.radius

    padding: Theme.spacingLg

    background: Rectangle {
        radius: control.cornerRadius
        color: control.strong ? Theme.materialFillStrong : Theme.materialFill
        border.width: 1
        border.color: control.accented
                      ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.52)
                      : Theme.materialStroke

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: control.cornerRadius
            anchors.rightMargin: control.cornerRadius
            height: 1
            color: control.accented
                   ? Qt.rgba(1, 1, 1, Theme.dark ? 0.30 : 0.64)
                   : Theme.materialEdge
            opacity: 0.9
        }
    }
}
