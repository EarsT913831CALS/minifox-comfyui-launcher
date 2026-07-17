pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: root

    property color selectedColor: "#0067c0"
    property real hue: 0.58
    property real saturation: 0.85
    property real brightness: 0.75
    readonly property color previewColor: Qt.hsva(hue, saturation, brightness, 1.0)

    signal colorSelected(color selectedColor)

    function clamped(value) {
        return Math.max(0.0, Math.min(1.0, value));
    }

    function setFromColor(value) {
        const sourceHue = value.hsvHue;
        if (sourceHue >= 0.0)
            hue = sourceHue;
        saturation = value.hsvSaturation;
        brightness = value.hsvValue;
    }

    function updateSaturationAndBrightness(xPosition, yPosition, itemWidth, itemHeight) {
        saturation = clamped(xPosition / itemWidth);
        brightness = 1.0 - clamped(yPosition / itemHeight);
    }

    function updateHue(xPosition, itemWidth) {
        hue = clamped(xPosition / itemWidth);
    }

    title: qsTr("选择强调色")
    modal: true
    implicitWidth: 520
    standardButtons: Dialog.Ok | Dialog.Cancel
    acceptText: qsTr("确定")
    rejectText: qsTr("取消")
    closePolicy: Popup.CloseOnEscape
    onOpened: setFromColor(selectedColor)
    onAccepted: colorSelected(previewColor)

    ColumnLayout {
        id: contentLayout

        objectName: "accentColorContent"
        width: root.availableWidth
        spacing: Theme.spacingMd

        Rectangle {
            id: saturationArea

            objectName: "saturationArea"

            Layout.fillWidth: true
            Layout.preferredHeight: 260
            radius: Theme.controlRadius
            color: Qt.hsva(root.hue, 1.0, 1.0, 1.0)
            border.width: 1
            border.color: saturationMouse.activeFocus ? Theme.accent : Theme.outline
            clip: true

            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "#ffffff" }
                    GradientStop { position: 1.0; color: "#00ffffff" }
                }
            }

            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#00000000" }
                    GradientStop { position: 1.0; color: "#ff000000" }
                }
            }

            Rectangle {
                width: 16
                height: 16
                radius: 8
                x: Math.max(0, Math.min(parent.width - width, root.saturation * parent.width - width / 2))
                y: Math.max(0, Math.min(parent.height - height, (1.0 - root.brightness) * parent.height - height / 2))
                color: "transparent"
                border.width: 2
                border.color: "#ffffff"

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -3
                    radius: width / 2
                    color: "transparent"
                    border.width: 1
                    border.color: "#80000000"
                }
            }

            MouseArea {
                id: saturationMouse

                anchors.fill: parent
                hoverEnabled: true
                activeFocusOnTab: true
                cursorShape: Qt.CrossCursor
                Accessible.role: Accessible.Slider
                Accessible.name: qsTr("颜色区域")
                Accessible.description: root.previewColor.toString().toUpperCase()
                onPressed: mouse => root.updateSaturationAndBrightness(mouse.x, mouse.y, width, height)
                onPositionChanged: mouse => {
                    if (pressed)
                        root.updateSaturationAndBrightness(mouse.x, mouse.y, width, height);
                }
                Keys.onLeftPressed: root.saturation = root.clamped(root.saturation - 0.01)
                Keys.onRightPressed: root.saturation = root.clamped(root.saturation + 0.01)
                Keys.onUpPressed: root.brightness = root.clamped(root.brightness + 0.01)
                Keys.onDownPressed: root.brightness = root.clamped(root.brightness - 0.01)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMd

            AppLabel {
                text: qsTr("色相")
            }

            Rectangle {
                id: hueArea

                objectName: "hueArea"

                Layout.fillWidth: true
                Layout.preferredHeight: 28
                radius: Theme.controlRadius
                border.width: 1
                border.color: hueMouse.activeFocus ? Theme.accent : Theme.outline
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "#ff0000" }
                    GradientStop { position: 0.167; color: "#ffff00" }
                    GradientStop { position: 0.333; color: "#00ff00" }
                    GradientStop { position: 0.5; color: "#00ffff" }
                    GradientStop { position: 0.667; color: "#0000ff" }
                    GradientStop { position: 0.833; color: "#ff00ff" }
                    GradientStop { position: 1.0; color: "#ff0000" }
                }

                Rectangle {
                    width: 6
                    height: parent.height + 6
                    radius: 3
                    x: Math.max(0, Math.min(parent.width - width, root.hue * parent.width - width / 2))
                    y: -3
                    color: "transparent"
                    border.width: 2
                    border.color: Theme.foreground
                }

                MouseArea {
                    id: hueMouse

                    anchors.fill: parent
                    hoverEnabled: true
                    activeFocusOnTab: true
                    cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.Slider
                    Accessible.name: qsTr("色相")
                    Accessible.description: Math.round(root.hue * 360)
                    onPressed: mouse => root.updateHue(mouse.x, width)
                    onPositionChanged: mouse => {
                        if (pressed)
                            root.updateHue(mouse.x, width);
                    }
                    Keys.onLeftPressed: root.hue = root.clamped(root.hue - 0.01)
                    Keys.onRightPressed: root.hue = root.clamped(root.hue + 0.01)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMd

            Rectangle {
                Layout.preferredWidth: 40
                Layout.preferredHeight: 40
                radius: Theme.controlRadius
                color: root.previewColor
                border.width: 1
                border.color: Theme.outline
                Accessible.ignored: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingXs

                AppLabel {
                    text: qsTr("当前颜色")
                    color: Theme.foregroundSecondary
                }

                AppLabel {
                    text: root.previewColor.toString().toUpperCase()
                    font.family: "Cascadia Mono"
                }
            }
        }
    }
}
