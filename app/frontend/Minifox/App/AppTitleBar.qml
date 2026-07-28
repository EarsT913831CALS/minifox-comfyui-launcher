import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Minifox.Shared

Rectangle {
    id: root

    required property ApplicationWindow window
    required property var appContext

    implicitHeight: 48
    color: Theme.titleBarFill

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.materialStroke
    }

    RowLayout {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.leftMargin: 14
        anchors.rightMargin: 150
        spacing: Theme.spacingSm

        Image {
            Layout.preferredWidth: 30
            Layout.preferredHeight: 30
            source: root.appContext.appIcon.activeIconSource
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
        }

        AppLabel {
            text: qsTr("Minifox ComfyUI 启动器")
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            Layout.maximumWidth: 420
        }

    }

    Row {
        anchors.right: parent.right
        anchors.top: parent.top
        height: parent.height

        TitleBarButton {
            symbol: "\uE921"
            accessibleName: qsTr("最小化")
            onActivated: root.window.showMinimized()
        }

        TitleBarButton {
            symbol: root.appContext.windowChrome.maximized ? "\uE923" : "\uE922"
            accessibleName: root.appContext.windowChrome.maximized
                            ? qsTr("还原") : qsTr("最大化")
            onActivated: root.appContext.windowChrome.toggleMaximized()
        }

        TitleBarButton {
            symbol: "\uE8BB"
            accessibleName: qsTr("关闭")
            closeButton: true
            onActivated: root.window.close()
        }
    }

    component TitleBarButton: Rectangle {
        id: button

        required property string symbol
        required property string accessibleName
        property bool closeButton: false
        signal activated()

        width: 46
        height: 48
        color: mouse.pressed
               ? (closeButton ? "#a4262c" : Theme.navigationSelected)
               : mouse.containsMouse
                 ? (closeButton ? "#c42b1c" : Theme.navigationHovered)
                 : "transparent"
        Accessible.role: Accessible.Button
        Accessible.name: accessibleName

        Behavior on color {
            enabled: Theme.controlMotionDuration > 0
            ColorAnimation {
                duration: Theme.controlMotionDuration
                easing.type: Easing.OutCubic
            }
        }

        IconLabel {
            anchors.centerIn: parent
            glyph: button.symbol
            iconPointSize: 10
            color: button.closeButton && mouse.containsMouse ? "#ffffff" : Theme.foreground
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: button.activated()
        }
    }
}
