pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Minifox.Shared

MaterialPanel {
    id: root

    required property var appContext
    readonly property var hardware: appContext.hardware

    Layout.fillWidth: true
    strong: true
    accented: hardware.hasCuda
    padding: Theme.spacingMd

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingMd

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMd

            IconLabel {
                glyph: "\uE950"
                color: root.hardware.hasCuda ? Theme.accent : Theme.foregroundSecondary
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingXs

                AppLabel {
                    text: qsTr("CUDA 设备自动检测")
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }

                AppLabel {
                    text: root.hardware.summary
                    color: Theme.foregroundSecondary
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }

            StatusBadge {
                text: root.hardware.detecting
                      ? qsTr("检测中")
                      : root.hardware.hasCuda
                        ? qsTr("已检测")
                        : qsTr("未发现")
                icon: root.hardware.hasCuda ? "\uE73E" : "\uE711"
                statusColor: root.hardware.hasCuda ? Theme.success : Theme.foregroundSecondary
            }
        }

        AppLabel {
            visible: root.hardware.lastError.length > 0
            text: root.hardware.lastError
            color: Theme.error
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        Repeater {
            model: root.hardware.cudaDevices

            delegate: Rectangle {
                id: deviceDelegate

                required property var modelData

                Layout.fillWidth: true
                implicitHeight: deviceRow.implicitHeight + Theme.spacingMd * 2
                radius: Theme.controlRadius
                color: Theme.surfaceSubtle
                border.width: 1
                border.color: Theme.materialStroke

                RowLayout {
                    id: deviceRow
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMd
                    spacing: Theme.spacingMd

                    Rectangle {
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        radius: 10
                        color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b,
                                       Theme.dark ? 0.20 : 0.10)

                        AppLabel {
                            anchors.centerIn: parent
                            text: String(deviceDelegate.modelData.index)
                            color: Theme.accent
                            font.weight: Font.DemiBold
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        AppLabel {
                            text: deviceDelegate.modelData.name
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        AppLabel {
                            text: {
                                const details = [deviceDelegate.modelData.memoryText];
                                if (deviceDelegate.modelData.capability)
                                    details.push(qsTr("计算能力 %1").arg(deviceDelegate.modelData.capability));
                                return details.join(" · ");
                            }
                            color: Theme.foregroundSecondary
                            Layout.fillWidth: true
                        }
                    }

                    AppButton {
                        text: qsTr("使用此设备")
                        onClicked: root.hardware.useDevice(deviceDelegate.modelData.index)
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            AppLabel {
                text: {
                    const details = [];
                    if (root.hardware.detectionSource)
                        details.push(qsTr("来源：%1").arg(root.hardware.detectionSource));
                    if (root.hardware.torchVersion)
                        details.push(qsTr("PyTorch %1").arg(root.hardware.torchVersion));
                    if (root.hardware.cudaRuntimeVersion)
                        details.push(qsTr("CUDA %1").arg(root.hardware.cudaRuntimeVersion));
                    if (root.hardware.driverVersion)
                        details.push(qsTr("驱动 %1").arg(root.hardware.driverVersion));
                    return details.join(" · ");
                }
                color: Theme.foregroundSecondary
                font.pointSize: Theme.captionSize
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            AppButton {
                text: qsTr("重新检测")
                enabled: !root.hardware.detecting
                onClicked: root.hardware.detect()
            }

            AppButton {
                visible: root.hardware.cudaDevices.length > 1
                text: qsTr("使用全部设备")
                accented: true
                onClicked: root.hardware.applyAllDevices()
            }
        }
    }
}
