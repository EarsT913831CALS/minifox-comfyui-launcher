import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: root

    property url imageSource
    property real targetWidth: 1280
    property real targetHeight: 720
    property real draftFocusX: 0.5
    property real draftFocusY: 0.5
    property real draftZoom: 1.0
    readonly property real minimumZoom: 1.0
    readonly property real maximumZoom: 4.0
    readonly property real zoomStep: 0.1
    readonly property real targetAspectRatio: Math.max(0.1, targetWidth / targetHeight)
    signal cropAccepted(real focusX, real focusY, real zoom)

    function bounded(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value));
    }

    function openCrop(source, focusX, focusY, zoom, frameWidth, frameHeight) {
        imageSource = source;
        draftFocusX = bounded(focusX, 0, 1);
        draftFocusY = bounded(focusY, 0, 1);
        draftZoom = bounded(zoom, minimumZoom, maximumZoom);
        targetWidth = Math.max(1, frameWidth);
        targetHeight = Math.max(1, frameHeight);
        open();
    }

    function resetCrop() {
        draftFocusX = 0.5;
        draftFocusY = 0.5;
        draftZoom = minimumZoom;
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(760, parent.width - Theme.spacingXl * 2)
    height: Math.min(720, parent.height - Theme.spacingXl * 2)
    title: qsTr("调整画面")
    modal: true
    standardButtons: Dialog.Ok | Dialog.Cancel
    acceptText: qsTr("应用")
    rejectText: qsTr("取消")
    closePolicy: Popup.CloseOnEscape
    onAccepted: cropAccepted(draftFocusX, draftFocusY, draftZoom)

    contentItem: ColumnLayout {
        spacing: Theme.spacingMd

        RowLayout {
            Layout.fillWidth: true

            AppLabel {
                Layout.fillWidth: true
                text: qsTr("拖动图片调整可见区域；滚轮或下方按钮调整缩放。")
                color: Theme.foregroundSecondary
                wrapMode: Text.WordWrap
            }
            AppLabel {
                text: qsTr("目标画面 %1 × %2")
                      .arg(Math.round(root.targetWidth))
                      .arg(Math.round(root.targetHeight))
                color: Theme.foregroundSecondary
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 180

            Rectangle {
                id: viewport

                anchors.centerIn: parent
                width: Math.min(parent.width, parent.height * root.targetAspectRatio)
                height: width / root.targetAspectRatio
                color: Theme.surfaceSubtle
                radius: Theme.radius
                border.width: 1
                border.color: Theme.materialStroke
                clip: true

                SkinImage {
                    id: cropImage

                    anchors.fill: parent
                    source: root.imageSource
                    fillMode: "cover"
                    focusX: root.draftFocusX
                    focusY: root.draftFocusY
                    zoom: root.draftZoom
                }

                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.38)
                    radius: Theme.radius
                }

                MouseArea {
                    property real pressX: 0
                    property real pressY: 0
                    property real startFocusX: 0.5
                    property real startFocusY: 0.5

                    anchors.fill: parent
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                    onPressed: mouse => {
                        pressX = mouse.x;
                        pressY = mouse.y;
                        startFocusX = root.draftFocusX;
                        startFocusY = root.draftFocusY;
                    }
                    onPositionChanged: mouse => {
                        if (!pressed)
                            return;
                        const overflowX = Math.max(1, cropImage.renderedSize.width - width);
                        const overflowY = Math.max(1, cropImage.renderedSize.height - height);
                        if (cropImage.renderedSize.width > width)
                            root.draftFocusX = root.bounded(
                                        startFocusX - (mouse.x - pressX) / overflowX, 0, 1);
                        if (cropImage.renderedSize.height > height)
                            root.draftFocusY = root.bounded(
                                        startFocusY - (mouse.y - pressY) / overflowY, 0, 1);
                    }
                    onWheel: wheel => {
                        const factor = wheel.angleDelta.y > 0 ? 1.1 : 0.9;
                        root.draftZoom = root.bounded(
                                    root.draftZoom * factor,
                                    root.minimumZoom,
                                    root.maximumZoom);
                        wheel.accepted = true;
                    }
                }
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Theme.spacingSm

            AppButton {
                text: "−"
                Layout.preferredWidth: 96
                Accessible.name: qsTr("缩小")
                enabled: root.draftZoom > root.minimumZoom + 0.001
                onClicked: root.draftZoom = root.bounded(
                               root.draftZoom - root.zoomStep,
                               root.minimumZoom,
                               root.maximumZoom)
            }
            AppLabel {
                Layout.preferredWidth: 72
                horizontalAlignment: Text.AlignHCenter
                text: Math.round(root.draftZoom * 100) + "%"
            }
            AppButton {
                text: "+"
                Layout.preferredWidth: 96
                Accessible.name: qsTr("放大")
                enabled: root.draftZoom < root.maximumZoom - 0.001
                onClicked: root.draftZoom = root.bounded(
                               root.draftZoom + root.zoomStep,
                               root.minimumZoom,
                               root.maximumZoom)
            }
            AppButton {
                text: qsTr("居中")
                Layout.preferredWidth: 96
                onClicked: root.resetCrop()
            }
        }
    }
}
