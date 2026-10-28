pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Minifox.Shared

Pane {
    id: root

    required property int currentIndex
    property bool compact: false
    // Notifications live behind a bell above Settings once there are any.
    property int noticeCount: 0
    property int unreadNotices: 0
    signal pageSelected(int index)
    signal noticesRequested(Item anchor)

    readonly property var destinations: [
        { title: qsTr("一键启动"), icon: "\uE768" },
        { title: qsTr("高级选项"), icon: "\uE713" },
        { title: qsTr("控制台"), icon: "\uE756" },
        { title: qsTr("版本管理"), icon: "\uE81C" }
    ]

    padding: Theme.spacingSm
    background: Rectangle {
        color: Theme.sidebar

        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 1
            color: Theme.materialStroke
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingXs

        Repeater {
            model: root.destinations

            delegate: NavigationDestination {
                required property int index
                required property var modelData

                Layout.fillWidth: true
                destinationIndex: index
                destinationTitle: modelData.title
                destinationIcon: modelData.icon
                compact: root.compact
                current: index === root.currentIndex
                onDestinationSelected: index => root.pageSelected(index)
            }
        }

        Item {
            Layout.fillHeight: true
        }

        NavigationDestination {
            id: bell

            visible: root.noticeCount > 0
            Layout.fillWidth: true
            destinationIndex: -1
            destinationTitle: qsTr("通知")
            destinationIcon: "\uEA8F"
            compact: root.compact
            onDestinationSelected: root.noticesRequested(bell)

            Rectangle {
                visible: root.unreadNotices > 0
                x: parent.width / 2 + 6
                y: root.compact ? 8 : 10
                width: Math.max(height, unreadLabel.implicitWidth + 8)
                height: 18
                radius: 9
                color: Theme.accent

                AppLabel {
                    id: unreadLabel
                    anchors.centerIn: parent
                    text: root.unreadNotices > 99 ? "99+" : root.unreadNotices
                    color: Theme.accentForeground
                    font.pointSize: Theme.captionSize - 1
                    font.weight: Font.DemiBold
                }
            }
        }

        NavigationDestination {
            Layout.fillWidth: true
            destinationIndex: 4
            destinationTitle: qsTr("设置")
            destinationIcon: "\uE770"
            compact: root.compact
            current: root.currentIndex === 4
            onDestinationSelected: index => root.pageSelected(index)
        }
    }
}
