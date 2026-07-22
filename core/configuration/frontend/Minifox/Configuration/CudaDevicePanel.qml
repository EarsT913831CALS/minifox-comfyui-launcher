pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Minifox.Shared

Frame {
    id: root

    required property var hardware
    padding: Theme.spacingMd

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingSm

        RowLayout {
            Layout.fillWidth: true

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingXs

                AppLabel {
                    text: qsTr("CUDA 设备检测")
                    font.pointSize: Theme.subtitleSize
                    font.weight: Font.DemiBold
                }

                AppLabel {
                    text: root.hardware.summary
                    color: Theme.foregroundSecondary
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }

            AppButton {
                text: root.hardware.detecting ? qsTr("检测中…") : qsTr("重新检测")
                enabled: !root.hardware.detecting
                onClicked: root.hardware.detect()
            }

            AppButton {
                text: qsTr("使用全部设备")
                enabled: root.hardware.hasCuda && !root.hardware.detecting
                onClicked: root.hardware.applyAllDevices()
            }
        }

        Repeater {
            model: root.hardware.cudaDevices

            delegate: Frame {
                id: deviceDelegate
                required property int index
                required property var modelData
                Layout.fillWidth: true
                padding: Theme.spacingSm

                RowLayout {
                    anchors.fill: parent
                    spacing: Theme.spacingMd

                    AppLabel {
                        text: deviceDelegate.modelData.name || qsTr("设备 %1").arg(deviceDelegate.index)
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }

                    AppLabel {
                        text: deviceDelegate.modelData.memoryText || ""
                        color: Theme.foregroundSecondary
                    }

                    AppButton {
                        text: qsTr("使用此设备")
                        onClicked: root.hardware.useDevice(deviceDelegate.modelData.index)
                    }
                }
            }
        }

        AppLabel {
            visible: root.hardware.lastError.length > 0
            text: root.hardware.lastError
            color: Theme.error
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }
}
