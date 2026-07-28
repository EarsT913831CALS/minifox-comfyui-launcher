pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Minifox.Shared

Item {
    id: root

    required property var appContext
    required property var itemData
    signal openConfiguration()
    signal openRuntime()
    signal launchRequested()

    readonly property var properties: itemData.properties || ({})

    Loader {
        anchors.fill: parent
        sourceComponent: root.itemData.type === "launch" ? launchComponent
                         : root.itemData.type === "image" ? imageComponent
                         : root.itemData.type === "folders" ? foldersComponent
                         : root.itemData.type === "text" ? textComponent
                         : root.itemData.type === "version" ? versionComponent
                         : panelComponent
    }

    Component {
        id: launchComponent

        LaunchStatusCard {
            appContext: root.appContext
            detailed: root.properties.detailed !== false
            onOpenConfiguration: root.openConfiguration()
            onOpenRuntime: root.openRuntime()
            onLaunchRequested: root.launchRequested()
        }
    }

    Component {
        id: imageComponent

        MaterialPanel {
            padding: 0
            strong: true
            clip: true

            SkinImage {
                id: skinImage

                anchors.fill: parent
                source: root.appContext.skins.assetUrl(root.properties.asset || "")
                fillMode: root.properties.fillMode || "cover"
                focusX: root.properties.focusX === undefined ? 0.5 : root.properties.focusX
                focusY: root.properties.focusY === undefined ? 0.5 : root.properties.focusY
                zoom: root.properties.zoom === undefined ? 1.0 : root.properties.zoom
            }

            Rectangle {
                anchors.fill: parent
                visible: skinImage.source.toString().length === 0
                         || skinImage.status === Image.Error
                color: Theme.surfaceSubtle

                Column {
                    anchors.centerIn: parent
                    spacing: Theme.spacingSm

                    IconLabel {
                        anchors.horizontalCenter: parent.horizontalCenter
                        glyph: "\uEB9F"
                        iconPointSize: 28
                        color: Theme.foregroundSecondary
                    }
                    AppLabel {
                        text: qsTr("在属性栏中选择图片")
                        color: Theme.foregroundSecondary
                    }
                }
            }

            Rectangle {
                visible: (root.properties.title || "").length > 0
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                implicitHeight: imageText.implicitHeight + Theme.spacingLg * 2
                color: "#99000000"

                ColumnLayout {
                    id: imageText
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: Theme.spacingLg
                    spacing: 2

                    AppLabel {
                        text: root.properties.title || ""
                        color: "#ffffff"
                        font.pointSize: Theme.titleSize
                        font.weight: Font.DemiBold
                    }
                    AppLabel {
                        text: root.properties.subtitle || ""
                        color: "#ddffffff"
                    }
                }
            }
        }
    }

    Component {
        id: foldersComponent

        MaterialPanel {
            id: foldersPanel
            strong: true
            padding: Theme.spacingLg

            function folderPath(kind) {
                const base = root.appContext.configuration.comfyRoot;
                if (kind === "root") return base;
                if (kind === "custom_nodes") return base + "/custom_nodes";
                if (kind === "input") return base + "/input";
                return base + "/output";
            }

            function openFolder(kind) {
                Qt.openUrlExternally("file:///" + folderPath(kind).replace(/\\/g, "/"));
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: Theme.spacingMd

                AppLabel {
                    text: qsTr("快捷文件夹")
                    font.pointSize: Theme.subtitleSize
                    font.weight: Font.DemiBold
                }

                GridLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    columns: width > 480 ? 2 : 1
                    columnSpacing: Theme.spacingSm
                    rowSpacing: Theme.spacingSm

                    Repeater {
                        model: [
                            { key: "root", title: qsTr("根目录"), icon: "\uE8B7" },
                            { key: "custom_nodes", title: "custom_nodes", icon: "\uE8F1" },
                            { key: "input", title: "input", icon: "\uE8B5" },
                            { key: "output", title: "output", icon: "\uE8B5" }
                        ]

                        AppButton {
                            required property var modelData
                            Layout.fillWidth: true
                            text: modelData.title
                            icon.name: ""
                            onClicked: foldersPanel.openFolder(modelData.key)
                        }
                    }
                }
            }
        }
    }

    Component {
        id: textComponent

        MaterialPanel {
            strong: true
            padding: Theme.spacingLg

            ColumnLayout {
                anchors.fill: parent
                spacing: Theme.spacingMd

                AppLabel {
                    text: root.properties.title || qsTr("公告")
                    font.pointSize: Theme.titleSize
                    font.weight: Font.DemiBold
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    AppLabel {
                        width: parent.width
                        text: root.properties.text || ""
                        textFormat: Text.MarkdownText
                        wrapMode: Text.WordWrap
                        color: Theme.foregroundSecondary
                        onLinkActivated: link => Qt.openUrlExternally(link)
                    }
                }
            }
        }
    }

    Component {
        id: versionComponent

        MaterialPanel {
            strong: true
            padding: Theme.spacingLg

            ColumnLayout {
                anchors.fill: parent
                spacing: Theme.spacingSm

                AppLabel {
                    text: qsTr("版本信息")
                    font.pointSize: Theme.titleSize
                    font.weight: Font.DemiBold
                }
                LaunchInformationRow {
                    Layout.fillWidth: true
                    label: qsTr("启动器")
                    value: root.appContext.versions.launcherVersion
                }
                LaunchInformationRow {
                    Layout.fillWidth: true
                    label: qsTr("ComfyUI")
                    value: root.appContext.versions.comfyVersion
                }
                LaunchInformationRow {
                    Layout.fillWidth: true
                    label: qsTr("分支")
                    value: root.appContext.versions.branch
                }
                LaunchInformationRow {
                    Layout.fillWidth: true
                    label: qsTr("Python")
                    value: root.appContext.configuration.pythonPath
                }
                Item { Layout.fillHeight: true }
            }
        }
    }

    Component {
        id: panelComponent

        MaterialPanel {
            strong: false
        }
    }
}
