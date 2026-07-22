pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Minifox.Shared

Pane {
    id: root

    required property int currentIndex
    property bool compact: false
    signal pageSelected(int index)

    readonly property var destinations: [
        { title: qsTr("一键启动"), icon: "\uE768" },
        { title: qsTr("高级选项"), icon: "\uE713" },
        { title: qsTr("控制台"), icon: "\uE756" },
        { title: qsTr("版本管理"), icon: "\uE81C" }
    ]

    padding: Theme.spacingSm
    background: Rectangle {
        color: Theme.sidebar
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
