import QtQuick
import QtQuick.Controls.FluentWinUI3 as Fluent
import QtQuick.Layouts

RowLayout {
    id: root

    property real from: 0
    property real to: 100
    property real value: 0
    property real stepSize: 1
    property string suffix: ""
    signal valueModified(real value)

    Layout.preferredWidth: 480
    Layout.maximumWidth: 560
    spacing: Theme.spacingMd

    Fluent.Slider {
        id: slider

        Layout.fillWidth: true
        implicitHeight: 32
        from: root.from
        to: root.to
        value: root.value
        stepSize: root.stepSize
        snapMode: Fluent.Slider.SnapAlways
        live: true
        focusPolicy: Qt.StrongFocus
        leftPadding: 8
        rightPadding: 8
        Accessible.name: qsTr("数值滑块")
        Accessible.description: Math.round(value) + root.suffix
        onMoved: root.valueModified(value)

        background: Rectangle {
            x: slider.leftPadding
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth
            height: 4
            radius: 2
            color: Theme.dark ? "#5a5a60" : "#c7c7ce"

            Rectangle {
                width: parent.width * slider.visualPosition
                height: parent.height
                radius: parent.radius
                color: Theme.accent
            }
        }

        handle: Rectangle {
            x: slider.leftPadding
               + slider.visualPosition * (slider.availableWidth - width)
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: 16
            height: 16
            radius: 8
            color: slider.pressed ? Theme.accentPressed
                                  : slider.hovered ? Theme.accentHovered : Theme.accent
            border.width: slider.visualFocus ? 3 : 2
            border.color: Theme.dark ? "#333338" : "#ffffff"
        }
    }

    Rectangle {
        Layout.preferredWidth: 62
        Layout.preferredHeight: 28
        radius: Theme.controlRadius
        color: Theme.surfaceSubtle
        border.width: 1
        border.color: Theme.materialStroke

        AppLabel {
            anchors.centerIn: parent
            text: Math.round(slider.value) + root.suffix
            color: Theme.foregroundSecondary
        }
    }
}
