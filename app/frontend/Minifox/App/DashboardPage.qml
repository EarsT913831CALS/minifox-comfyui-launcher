pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Minifox.Shared

Pane {
    id: root

    required property var appContext
    signal openConfiguration()
    signal openRuntime()
    signal launchRequested()

    property string selectedItemId: ""
    property real normalCanvasWidth: 0
    property real normalCanvasHeight: 0
    readonly property var selectedItem: itemById(selectedItemId)

    function itemById(id) {
        const items = appContext.skins.homeItems;
        for (let index = 0; index < items.length; ++index) {
            if (items[index].id === id)
                return items[index];
        }
        return ({});
    }

    function selectNew(type) {
        const id = appContext.skins.addItem(type);
        if (id.length > 0)
            selectedItemId = id;
    }

    function setSelectedProperty(key, value) {
        let values = {};
        values[key] = value;
        setSelectedProperties(values);
    }

    function setSelectedProperties(values) {
        if (selectedItemId.length === 0)
            return;
        const source = selectedItem.properties || ({});
        let properties = {};
        for (const name in source)
            properties[name] = source[name];
        for (const name in values)
            properties[name] = values[name];
        appContext.skins.updateItem(selectedItemId, {"properties": properties});
    }

    padding: Theme.spacingLg
    background: Rectangle { color: "transparent" }

    Shortcut {
        enabled: root.appContext.skins.editing
        sequence: StandardKey.Undo
        onActivated: root.appContext.skins.undo()
    }
    Shortcut {
        enabled: root.appContext.skins.editing
        sequence: StandardKey.Redo
        onActivated: root.appContext.skins.redo()
    }
    Shortcut {
        enabled: root.appContext.skins.editing && root.selectedItemId.length > 0
        sequence: StandardKey.Delete
        onActivated: root.appContext.skins.removeItem(root.selectedItemId)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingMd

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMd

            PageHeader {
                title: root.appContext.skins.editing ? qsTr("编辑首页") : qsTr("一键启动")
                description: root.appContext.skins.editing
                             ? qsTr("拖动组件并使用八个控制点调整尺寸；完成后保存布局。")
                             : qsTr("确认当前配置，然后启动 ComfyUI。")
                icon: root.appContext.skins.editing ? "\uE70F" : "\uE768"
                Layout.fillWidth: true
            }

            AppButton {
                visible: !root.appContext.skins.editing
                text: qsTr("编辑首页")
                onClicked: {
                    root.normalCanvasWidth = homeCanvas.width;
                    root.normalCanvasHeight = homeCanvas.height;
                    root.appContext.skins.beginEdit();
                    root.selectedItemId = "launch-card";
                }
            }
        }

        MaterialPanel {
            visible: root.appContext.skins.editing
            Layout.fillWidth: true
            padding: Theme.spacingSm
            strong: true

            RowLayout {
                anchors.fill: parent
                spacing: Theme.spacingSm

                AppButton {
                    text: qsTr("添加组件")
                    onClicked: addMenu.open()

                    Menu {
                        id: addMenu
                        y: parent.height

                        MenuItem { text: qsTr("图片 / 横幅"); onTriggered: root.selectNew("image") }
                        MenuItem { text: qsTr("快捷文件夹"); onTriggered: root.selectNew("folders") }
                        MenuItem { text: qsTr("文本 / 公告"); onTriggered: root.selectNew("text") }
                        MenuItem { text: qsTr("版本信息"); onTriggered: root.selectNew("version") }
                        MenuItem { text: qsTr("空白材质面板"); onTriggered: root.selectNew("panel") }
                    }
                }

                AppButton {
                    text: qsTr("撤销")
                    enabled: root.appContext.skins.canUndo
                    onClicked: root.appContext.skins.undo()
                }
                AppButton {
                    text: qsTr("重做")
                    enabled: root.appContext.skins.canRedo
                    onClicked: root.appContext.skins.redo()
                }
                AppButton {
                    text: qsTr("恢复默认")
                    onClicked: root.appContext.skins.resetHomeLayout()
                }

                Item { Layout.fillWidth: true }

                AppButton {
                    text: qsTr("取消")
                    onClicked: {
                        root.appContext.skins.cancelEdit();
                        root.selectedItemId = "";
                    }
                }
                AppButton {
                    text: qsTr("保存布局")
                    accented: true
                    onClicked: {
                        if (root.appContext.skins.commitEdit())
                            root.selectedItemId = "";
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingMd

            Item {
                id: canvasViewport

                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                Item {
                    id: homeCanvas

                    readonly property real editScale: Math.min(
                                                          canvasViewport.width
                                                          / Math.max(1, width),
                                                          canvasViewport.height
                                                          / Math.max(1, height))

                    width: root.appContext.skins.editing
                           && root.normalCanvasWidth > 0
                           ? root.normalCanvasWidth : canvasViewport.width
                    height: root.appContext.skins.editing
                            && root.normalCanvasHeight > 0
                            ? root.normalCanvasHeight : canvasViewport.height
                    scale: root.appContext.skins.editing ? editScale : 1
                    transformOrigin: Item.TopLeft
                    x: root.appContext.skins.editing
                       ? (canvasViewport.width - width * scale) / 2 : 0
                    y: root.appContext.skins.editing
                       ? (canvasViewport.height - height * scale) / 2 : 0
                    onWidthChanged: {
                        if (!root.appContext.skins.editing && width > 0)
                            root.normalCanvasWidth = width;
                    }
                    onHeightChanged: {
                        if (!root.appContext.skins.editing && height > 0)
                            root.normalCanvasHeight = height;
                    }

                    Rectangle {
                        anchors.fill: parent
                        visible: root.appContext.skins.editing
                        color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b,
                                       Theme.dark ? 0.035 : 0.025)
                        border.width: 1
                        border.color: Theme.materialStroke
                        radius: Theme.radius
                    }

                    Repeater {
                        model: root.appContext.skins.homeItems

                        delegate: HomeWidgetFrame {
                            required property var modelData

                            appContext: root.appContext
                            canvas: homeCanvas
                            itemData: modelData
                            editing: root.appContext.skins.editing
                            selected: root.selectedItemId === modelData.id
                            onSelectionRequested: id => root.selectedItemId = id
                            onOpenConfiguration: root.openConfiguration()
                            onOpenRuntime: root.openRuntime()
                            onLaunchRequested: root.launchRequested()
                        }
                    }
                }
            }

            MaterialPanel {
                visible: root.appContext.skins.editing
                Layout.preferredWidth: 292
                Layout.fillHeight: true
                padding: Theme.spacingLg
                strong: true

                ScrollView {
                    anchors.fill: parent
                    contentWidth: availableWidth

                    ColumnLayout {
                        width: parent.width
                        spacing: Theme.spacingMd

                        AppLabel {
                            text: qsTr("组件属性")
                            font.pointSize: Theme.titleSize
                            font.weight: Font.DemiBold
                        }
                        AppLabel {
                            text: root.selectedItemId.length > 0
                                  ? root.selectedItem.type
                                  : qsTr("选择一个组件")
                            color: Theme.foregroundSecondary
                        }

                        GridLayout {
                            visible: root.selectedItemId.length > 0
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: Theme.spacingSm
                            rowSpacing: Theme.spacingSm

                            AppLabel { text: "X %" }
                            AppSpinBox {
                                Layout.fillWidth: true
                                from: 0; to: 100
                                value: Math.round((root.selectedItem.x || 0) * 100)
                                onValueModified: root.appContext.skins.updateItem(
                                                     root.selectedItemId, {"x": value / 100})
                            }
                            AppLabel { text: "Y %" }
                            AppSpinBox {
                                Layout.fillWidth: true
                                from: 0; to: 100
                                value: Math.round((root.selectedItem.y || 0) * 100)
                                onValueModified: root.appContext.skins.updateItem(
                                                     root.selectedItemId, {"y": value / 100})
                            }
                            AppLabel { text: qsTr("宽度 %") }
                            AppSpinBox {
                                Layout.fillWidth: true
                                from: 8; to: 100
                                value: Math.round((root.selectedItem.w || 0.4) * 100)
                                onValueModified: root.appContext.skins.updateItem(
                                                     root.selectedItemId, {"w": value / 100})
                            }
                            AppLabel { text: qsTr("高度 %") }
                            AppSpinBox {
                                Layout.fillWidth: true
                                from: 8; to: 100
                                value: Math.round((root.selectedItem.h || 0.3) * 100)
                                onValueModified: root.appContext.skins.updateItem(
                                                     root.selectedItemId, {"h": value / 100})
                            }
                            AppLabel { text: qsTr("透明度 %") }
                            AppSpinBox {
                                Layout.fillWidth: true
                                from: 5; to: 100
                                value: Math.round((root.selectedItem.opacity || 1) * 100)
                                onValueModified: root.appContext.skins.updateItem(
                                                     root.selectedItemId, {"opacity": value / 100})
                            }
                        }

                        AppSwitch {
                            visible: root.selectedItem.type === "launch"
                            text: qsTr("显示详细信息")
                            checked: root.selectedItem.properties
                                     && root.selectedItem.properties.detailed !== false
                            onToggled: root.setSelectedProperty("detailed", checked)
                        }

                        AppButton {
                            visible: root.selectedItem.type === "image"
                            text: qsTr("选择本地图片…")
                            onClicked: imageDialog.open()
                        }
                        AppComboBox {
                            visible: root.selectedItem.type === "image"
                            Layout.fillWidth: true
                            model: [
                                qsTr("裁切填满"),
                                qsTr("完整显示"),
                                qsTr("拉伸"),
                                qsTr("平铺")
                            ]
                            currentIndex: ["cover", "contain", "stretch", "tile"].indexOf(
                                              root.selectedItem.properties
                                              ? (root.selectedItem.properties.fillMode || "cover")
                                              : "cover")
                            onActivated: index => root.setSelectedProperty(
                                             "fillMode",
                                             ["cover", "contain", "stretch", "tile"][index])
                        }
                        AppButton {
                            visible: root.selectedItem.type === "image"
                                     && (!root.selectedItem.properties
                                         || (root.selectedItem.properties.fillMode || "cover")
                                            === "cover")
                            Layout.fillWidth: true
                            text: qsTr("调整画面")
                            onClicked: {
                                const properties = root.selectedItem.properties || ({});
                                const source = root.appContext.skins.assetUrl(
                                                 properties.asset || "");
                                const targetWidth = Math.min(
                                    homeCanvas.width,
                                    Math.max(180, root.selectedItem.w * homeCanvas.width));
                                const targetHeight = Math.min(
                                    homeCanvas.height,
                                    Math.max(120, root.selectedItem.h * homeCanvas.height));
                                cropDialog.openCrop(
                                            source,
                                            properties.focusX === undefined
                                            ? 0.5 : properties.focusX,
                                            properties.focusY === undefined
                                            ? 0.5 : properties.focusY,
                                            properties.zoom === undefined
                                            ? 1 : properties.zoom,
                                            targetWidth,
                                            targetHeight);
                            }
                        }
                        AppTextField {
                            visible: root.selectedItem.type === "image"
                                     || root.selectedItem.type === "text"
                            Layout.fillWidth: true
                            placeholderText: qsTr("标题")
                            text: root.selectedItem.properties
                                  ? (root.selectedItem.properties.title || "") : ""
                            onEditingFinished: root.setSelectedProperty("title", text)
                        }
                        AppTextArea {
                            visible: root.selectedItem.type === "text"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 160
                            placeholderText: qsTr("支持基础 Markdown 文本")
                            text: root.selectedItem.properties
                                  ? (root.selectedItem.properties.text || "") : ""
                            onActiveFocusChanged: {
                                if (!activeFocus)
                                    root.setSelectedProperty("text", text);
                            }
                        }

                        RowLayout {
                            visible: root.selectedItemId.length > 0
                            Layout.fillWidth: true
                            spacing: Theme.spacingSm

                            AppButton {
                                text: qsTr("下移")
                                onClicked: root.appContext.skins.moveItemLayer(
                                               root.selectedItemId, -1)
                            }
                            AppButton {
                                text: qsTr("上移")
                                onClicked: root.appContext.skins.moveItemLayer(
                                               root.selectedItemId, 1)
                            }
                        }
                        AppButton {
                            visible: root.selectedItemId.length > 0
                                     && root.selectedItem.type !== "launch"
                            Layout.fillWidth: true
                            text: qsTr("复制组件")
                            onClicked: root.appContext.skins.duplicateItem(
                                           root.selectedItemId)
                        }
                        AppButton {
                            visible: root.selectedItemId.length > 0
                                     && root.selectedItem.type !== "launch"
                            Layout.fillWidth: true
                            text: qsTr("删除组件")
                            destructive: true
                            onClicked: {
                                if (root.appContext.skins.removeItem(root.selectedItemId))
                                    root.selectedItemId = "";
                            }
                        }
                        AppLabel {
                            visible: root.appContext.skins.lastError.length > 0
                            Layout.fillWidth: true
                            text: root.appContext.skins.lastError
                            color: Theme.error
                            wrapMode: Text.WordWrap
                        }
                        Item { Layout.fillHeight: true }
                    }
                }
            }
        }
    }

    ImageCropDialog {
        id: cropDialog

        onCropAccepted: (focusX, focusY, zoom) =>
            root.setSelectedProperties({
                "focusX": focusX,
                "focusY": focusY,
                "zoom": zoom
            })
    }

    FileDialog {
        id: imageDialog
        title: qsTr("选择皮肤图片")
        fileMode: FileDialog.OpenFile
        nameFilters: [
            qsTr("支持的图片 (*.png *.jpg *.jpeg *.webp)"),
            qsTr("所有文件 (*)")
        ]
        onAccepted: {
            const reference = root.appContext.skins.importImage(selectedFile);
            if (reference.length > 0)
                root.setSelectedProperty("asset", reference);
        }
    }
}
