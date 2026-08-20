pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Minifox.Shared

MaterialPanel {
    id: root

    required property var appContext
    readonly property var devices: appContext.runtime.acceleratorDevices

    function useAllCudaDevices() {
        const indexes = [];
        for (let index = 0; index < devices.length; ++index) {
            const device = devices[index];
            if (device.type.toLowerCase() === "cuda" && device.index >= 0)
                indexes.push(device.index);
        }
        if (indexes.length > 0)
            appContext.configuration.setParameterValue("cudaDevice", indexes.join(","));
    }

    Layout.fillWidth: true
    strong: true
    accented: true
    padding: Theme.spacingMd

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingMd

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMd

            IconLabel {
                glyph: "\uE950"
                color: Theme.accent
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingXs

                AppLabel {
                    text: qsTr("本次 ComfyUI 检测到的设备")
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }

                AppLabel {
                    text: qsTr("每次 ComfyUI 启动完成后自动刷新。")
                    color: Theme.foregroundSecondary
                    Layout.fillWidth: true
                }
            }

            StatusBadge {
                text: qsTr("已刷新")
                icon: "\uE73E"
                statusColor: Theme.success
            }
        }

        Repeater {
            model: root.devices

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
                            text: deviceDelegate.modelData.index >= 0
                                  ? String(deviceDelegate.modelData.index) : "–"
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
                            visible: text.length > 0
                            text: deviceDelegate.modelData.memoryText
                            color: Theme.foregroundSecondary
                            Layout.fillWidth: true
                        }
                    }

                    AppButton {
                        visible: deviceDelegate.modelData.type.toLowerCase() === "cuda"
                                 && deviceDelegate.modelData.index >= 0
                        text: qsTr("使用此设备")
                        compact: true
                        onClicked: root.appContext.configuration.setParameterValue(
                                       "cudaDevice", String(deviceDelegate.modelData.index))
                    }
                }
            }
        }

        RowLayout {
            visible: root.devices.length > 1
            Layout.fillWidth: true

            Item { Layout.fillWidth: true }

            AppButton {
                text: qsTr("使用全部设备")
                accented: true
                compact: true
                onClicked: root.useAllCudaDevices()
            }
        }
    }
}
