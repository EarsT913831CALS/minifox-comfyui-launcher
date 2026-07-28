import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import Minifox.Shared

MaterialPanel {
    id: root

    required property var appContext
    readonly property var appearance: appContext.skins.effectiveAppearance
    readonly property var materials: appearance.materials || ({})
    readonly property var backgroundSettings: appearance.background || ({})
    readonly property url backgroundSource: appearance.backgroundSource || ""
    readonly property string backgroundFillMode: backgroundSettings.fillMode || "cover"
    readonly property real backgroundFocusX: backgroundSettings.focusX ?? 0.5
    readonly property real backgroundFocusY: backgroundSettings.focusY ?? 0.5
    readonly property real backgroundZoom: backgroundSettings.zoom ?? 1.0
    readonly property bool hasBackground: backgroundSource.toString().length > 0
    // Mirrors Main.qml's effectiveAspectRatio so the crop dialog previews
    // the exact frame the launcher window will show.
    readonly property real windowAspectRatio: {
        switch (appContext.settings.windowAspectRatio) {
        case "16:9": return 16 / 9;
        case "3:2": return 3 / 2;
        case "4:3": return 4 / 3;
        case "16:10": return 16 / 10;
        default: return Screen.desktopAvailableHeight > 0
                 ? Screen.desktopAvailableWidth / Screen.desktopAvailableHeight
                 : 1.6;
        }
    }
    readonly property var skinEntries: appContext.skins.skins
    readonly property var skinNames: {
        let names = [];
        for (let index = 0; index < skinEntries.length; ++index)
            names.push(skinEntries[index].name);
        return names;
    }
    readonly property int activeIndex: {
        for (let index = 0; index < skinEntries.length; ++index) {
            if (skinEntries[index].id === appContext.skins.activeSkinId)
                return index;
        }
        return 0;
    }
    readonly property string variantPrefix: appContext.settings.effectiveDark
                                            ? "variants.dark." : "variants.light."

    padding: Theme.spacingLg

    ColumnLayout {
        id: contentColumn

        anchors.fill: parent
        spacing: Theme.spacingLg

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMd

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                AppLabel {
                    text: qsTr("皮肤")
                    font.pointSize: Theme.subtitleSize
                    font.weight: Font.DemiBold
                }
                AppLabel {
                    text: qsTr("皮肤控制全局背景、材质和首页布局。图片会复制到 .minifox/skins。")
                    color: Theme.foregroundSecondary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            StatusBadge {
                text: root.appContext.skins.activeSkinBuiltin
                      ? qsTr("内置") : qsTr("自定义")
                icon: "\uE790"
                statusColor: Theme.accent
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            AppComboBox {
                Layout.fillWidth: true
                model: root.skinNames
                currentIndex: root.activeIndex
                onActivated: index => {
                    if (index >= 0 && index < root.skinEntries.length)
                        root.appContext.skins.selectSkin(root.skinEntries[index].id);
                }
            }
            AppButton {
                text: qsTr("新建")
                onClicked: root.appContext.skins.createSkin()
            }
            AppButton {
                text: qsTr("复制")
                onClicked: root.appContext.skins.duplicateActiveSkin()
            }
            AppButton {
                text: qsTr("导入")
                onClicked: importDialog.open()
            }
            AppButton {
                text: qsTr("导出")
                onClicked: exportDialog.open()
            }
        }

        ColumnLayout {
            visible: !root.appContext.skins.activeSkinBuiltin
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            AppLabel {
                Layout.fillWidth: true
                text: qsTr("在此处重命名皮肤项目名称")
                color: Theme.foregroundSecondary
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                AppTextField {
                    id: skinNameField
                    Layout.fillWidth: true
                    text: root.appContext.skins.activeSkinName
                    placeholderText: qsTr("皮肤名称")
                    onEditingFinished: root.appContext.skins.renameActiveSkin(text)
                }
                AppButton {
                    text: qsTr("删除皮肤")
                    destructive: true
                    onClicked: deleteDialog.open()
                }
            }
        }

        Rectangle {
            id: backgroundPreview

            // Preview the exact frame the launcher window shows: the
            // window's own aspect ratio with the same crop settings, so
            // what you see here is what the window looks like.
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Math.min(contentColumn.width,
                                            560 * root.windowAspectRatio)
            Layout.preferredHeight: width / root.windowAspectRatio
            radius: Theme.radius
            clip: true
            color: Theme.surfaceSubtle
            border.width: 1
            border.color: Theme.materialStroke

            SkinImage {
                id: previewImage

                anchors.fill: parent
                source: root.backgroundSource
                visible: root.hasBackground
                opacity: 1
                fillMode: root.backgroundFillMode
                focusX: root.backgroundFocusX
                focusY: root.backgroundFocusY
                zoom: root.backgroundZoom
            }
            AppLabel {
                anchors.centerIn: parent
                visible: !root.hasBackground
                text: qsTr("未设置全局背景图片")
                color: Theme.foregroundSecondary
            }
        }

        AppLabel {
            Layout.alignment: Qt.AlignHCenter
            visible: root.hasBackground
            text: qsTr("预览按当前窗口画面比例显示")
            color: Theme.foregroundSecondary
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.spacingLg
            rowSpacing: Theme.spacingMd

            AppLabel { text: qsTr("全局背景") }
            RowLayout {
                Layout.fillWidth: true
                AppButton {
                    text: qsTr("选择图片…")
                    onClicked: backgroundDialog.open()
                }
                AppButton {
                    visible: root.hasBackground && root.backgroundFillMode === "cover"
                    text: qsTr("调整画面")
                    onClicked: cropDialog.openCrop(
                                   root.backgroundSource,
                                   root.backgroundFocusX,
                                   root.backgroundFocusY,
                                   root.backgroundZoom,
                                   1440,
                                   Math.round(1440 / root.windowAspectRatio))
                }
                AppButton {
                    visible: root.hasBackground
                    text: qsTr("清除")
                    onClicked: root.appContext.skins.setAppearanceValue(
                                   "background.asset", "")
                }
            }

            AppLabel { text: qsTr("背景填充") }
            AppComboBox {
                Layout.fillWidth: true
                Layout.maximumWidth: 640
                model: [qsTr("裁切填满"), qsTr("完整显示"), qsTr("拉伸"), qsTr("平铺")]
                currentIndex: ["cover", "contain", "stretch", "tile"].indexOf(
                                  root.backgroundFillMode)
                onActivated: index => root.appContext.skins.setAppearanceValue(
                                 "background.fillMode",
                                 ["cover", "contain", "stretch", "tile"][index])
            }

            AppLabel { text: qsTr("背景可见度") }
            AppValueSlider {
                Layout.fillWidth: true
                from: 0; to: 80
                suffix: "%"
                value: 100 - Math.round(
                           (root.materials.pageOpacity === undefined
                            ? 0.62 : root.materials.pageOpacity) * 100)
                onValueModified: value => root.appContext.skins.setAppearanceValue(
                                     "materials.pageOpacity", 1 - value / 100)
            }

            AppLabel { text: qsTr("界面字体") }
            FontFamilyComboBox {
                Layout.fillWidth: true
                Layout.maximumWidth: 640
                selectedFamily: root.appContext.settings.fontFamily
                onFamilySelected: family =>
                                  root.appContext.settings.fontFamily = family
            }

            AppLabel { text: qsTr("界面字号") }
            AppSpinBox {
                Layout.preferredWidth: 180
                from: 8; to: 24
                value: Math.round(root.appContext.settings.fontPointSize)
                onValueModified: root.appContext.settings.fontPointSize = value
            }

            AppLabel { text: qsTr("圆角") }
            AppSpinBox {
                Layout.preferredWidth: 180
                from: 0; to: 32
                value: root.appearance.radius || 14
                onValueModified: {
                    root.appContext.skins.setAppearanceValue("radius", value);
                    root.appContext.skins.setAppearanceValue("radiusLarge",
                                                              Math.min(40, value + 6));
                }
            }

            AppLabel { text: qsTr("当前变体强调色") }
            AppTextField {
                Layout.fillWidth: true
                Layout.maximumWidth: 640
                text: root.appearance.accent || Theme.accent.toString()
                placeholderText: "#0067c0"
                onEditingFinished: {
                    if (/^#[0-9a-fA-F]{6}$/.test(text))
                        root.appContext.skins.setAppearanceValue(
                                    root.variantPrefix + "accent", text);
                }
            }
        }

        AppLabel {
            visible: root.appContext.skins.lastError.length > 0
            Layout.fillWidth: true
            text: root.appContext.skins.lastError
            color: Theme.error
            wrapMode: Text.WordWrap
        }
    }

    ImageCropDialog {
        id: cropDialog

        onCropAccepted: (focusX, focusY, zoom) => {
            root.appContext.skins.setAppearanceValue("background.focusX", focusX);
            root.appContext.skins.setAppearanceValue("background.focusY", focusY);
            root.appContext.skins.setAppearanceValue("background.zoom", zoom);
        }
    }

    FileDialog {
        id: backgroundDialog
        title: qsTr("选择全局背景图片")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("支持的图片 (*.png *.jpg *.jpeg *.webp)")]
        onAccepted: root.appContext.skins.setBackgroundImage(selectedFile)
    }

    FileDialog {
        id: importDialog
        title: qsTr("导入 Minifox 皮肤")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Minifox 皮肤 (*.minifoxskin)")]
        onAccepted: root.appContext.skins.importSkin(selectedFile)
    }

    FileDialog {
        id: exportDialog
        title: qsTr("导出 Minifox 皮肤")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "minifoxskin"
        nameFilters: [qsTr("Minifox 皮肤 (*.minifoxskin)")]
        onAccepted: root.appContext.skins.exportSkin(selectedFile)
    }

    AppDialog {
        id: deleteDialog
        title: qsTr("删除皮肤")
        modal: true
        standardButtons: Dialog.Yes | Dialog.Cancel
        acceptText: qsTr("删除")
        rejectText: qsTr("取消")

        AppLabel {
            width: deleteDialog.availableWidth
            text: qsTr("将删除当前皮肤及其已复制图片。此操作无法撤销。")
            wrapMode: Text.WordWrap
        }
        onAccepted: root.appContext.skins.removeSkin()
    }
}
