import QtQuick

Rectangle {
    id: surface

    required property bool controlEnabled
    required property bool controlHovered
    required property bool controlPressed
    required property bool controlFocused
    property bool accented: false
    property bool destructive: false

    readonly property color normalFill: {
        if (!controlEnabled)
            return Theme.buttonFillDisabled;
        if (accented) {
            if (controlPressed)
                return Theme.accentPressed;
            return controlHovered ? Theme.accentHovered : Theme.accent;
        }
        if (destructive) {
            if (controlPressed)
                return Theme.dangerFillPressed;
            return controlHovered ? Theme.dangerFillHovered : Theme.dangerFill;
        }
        if (controlPressed)
            return Theme.buttonFillPressed;
        return controlHovered ? Theme.buttonFillHovered : Theme.buttonFill;
    }
    readonly property color normalStroke: {
        if (!controlEnabled)
            return Theme.outline;
        if (accented)
            return controlPressed ? Theme.accentPressed : Theme.accentHovered;
        if (destructive)
            return controlPressed ? Theme.dangerFillPressed : Theme.dangerFillHovered;
        if (controlPressed)
            return Theme.buttonStrokePressed;
        return controlHovered ? Theme.buttonStrokeHovered : Theme.buttonStroke;
    }

    implicitWidth: 96
    implicitHeight: Theme.controlHeight
    radius: Theme.controlRadius
    color: normalFill
    border.width: 1
    border.color: normalStroke

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 2
        anchors.rightMargin: 2
        anchors.bottomMargin: 1
        height: 1
        radius: 1
        color: Theme.buttonBottomStroke
        opacity: surface.controlEnabled && !surface.controlPressed && !surface.accented && !surface.destructive ? 0.72 : 0
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        radius: surface.radius + 3
        color: "transparent"
        border.width: 2
        border.color: Theme.accent
        visible: surface.controlFocused
    }
}
