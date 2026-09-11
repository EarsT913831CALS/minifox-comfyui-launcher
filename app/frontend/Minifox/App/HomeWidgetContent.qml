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

            readonly property var folderEntries: {
                const configured = root.properties.folders;
                return configured && configured.length > 0
                       ? configured.slice(0, 8)
                       : [
                           {"kind": "root", "path": "${COMFYUI}"},
                           {"kind": "custom_nodes", "path": "${COMFYUI}/custom_nodes"},
                           {"kind": "input", "path": "${COMFYUI}/input"},
                           {"kind": "output", "path": "${COMFYUI}/output"}
                       ];
            }

            function folderTitle(entry, index) {
                if (entry.title && entry.title.length > 0)
                    return entry.title;
                if (entry.kind === "root") return qsTr("根目录");
                if (entry.kind === "custom_nodes") return "custom_nodes";
                if (entry.kind === "input") return "input";
                if (entry.kind === "output") return "output";
                return qsTr("文件夹 %1").arg(index + 1);
            }

            function requiredHeight(columns) {
                const rows = Math.ceil(folderEntries.length / columns);
                return 92 + rows * Theme.controlHeight
                       + Math.max(0, rows - 1) * Theme.spacingSm;
            }

            function columnCount(pixelWidth, pixelHeight) {
                if (folderEntries.length <= 1 || pixelWidth < 360)
                    return 1;
                return pixelWidth > 480 || pixelHeight < requiredHeight(1)
                       ? 2 : 1;
            }

            function resolvedFolderPath(entry) {
                let path = entry.path || "";
                const comfyRoot = root.appContext.configuration.comfyRoot || "";
                path = path.replace(/^\$\{COMFYUI\}/, comfyRoot);
                if (path.length > 0
                        && !/^[A-Za-z]:[\\/]/.test(path)
                        && !path.startsWith("/")
                        && !path.startsWith("file:")) {
                    path = comfyRoot + "/" + path;
                }
                return path;
            }

            function openFolder(entry) {
                const path = resolvedFolderPath(entry);
                if (path.length === 0)
                    return;
                root.appContext.skins.openLocalFolder(path);
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
                    columns: foldersPanel.columnCount(foldersPanel.width,
                                                      foldersPanel.height)
                    columnSpacing: Theme.spacingSm
                    rowSpacing: Theme.spacingSm

                    Repeater {
                        model: foldersPanel.folderEntries

                        AppButton {
                            required property int index
                            required property var modelData
                            Layout.fillWidth: true
                            text: foldersPanel.folderTitle(modelData, index)
                            enabled: foldersPanel.resolvedFolderPath(modelData).length > 0
                            icon.name: ""
                            onClicked: foldersPanel.openFolder(modelData)
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
                        text: root.appContext.skins.safeMarkdown(root.properties.text || "")
                        textFormat: Text.RichText
                        wrapMode: Text.WordWrap
                        color: Theme.foregroundSecondary
                        onLinkActivated: link => root.appContext.skins.openExternalLink(link)
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
