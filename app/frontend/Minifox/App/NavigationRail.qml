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
        {
            title: qsTr("主页"),
            icon: "\uE80F"
        },
        {
            title: qsTr("启动配置"),
            icon: "\uE713"
        },
        {
            title: qsTr("运行与控制台"),
            icon: "\uE756"
        },
        {
            title: qsTr("应用设置"),
            icon: "\uE770"
        }
    ]

    padding: Theme.spacingSm

    Column {
        width: parent.width
        spacing: Theme.spacingXs

        Repeater {
            model: root.destinations

            delegate: AppItemDelegate {
                id: navigationDelegate

                required property int index
                required property var modelData

                width: parent.width
                height: 48
                highlighted: navigationDelegate.index === root.currentIndex
                activeFocusOnTab: true
                Accessible.name: navigationDelegate.modelData.title
                ToolTip.visible: root.compact && hovered
                ToolTip.text: navigationDelegate.modelData.title
                onClicked: root.pageSelected(navigationDelegate.index)

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingMd
                    anchors.rightMargin: Theme.spacingMd
                    spacing: Theme.spacingMd

                    IconLabel {
                        glyph: navigationDelegate.modelData.icon
                        iconPointSize: Theme.bodySize
                        color: Theme.foreground
                        Layout.alignment: Qt.AlignVCenter
                    }

                    AppLabel {
                        visible: !root.compact
                        text: navigationDelegate.modelData.title
                        color: Theme.foreground
                        font.pointSize: Theme.bodySize
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Item {
                        visible: root.compact
                        Layout.fillWidth: true
                    }
                }
            }
        }
    }
}
