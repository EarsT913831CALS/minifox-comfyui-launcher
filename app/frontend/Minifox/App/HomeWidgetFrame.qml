pragma ComponentBehavior: Bound

import QtQuick
import Minifox.Shared

Item {
    id: root

    required property var appContext
    required property Item canvas
    required property var itemData
    required property bool editing
    property bool selected: false
    signal selectionRequested(string id)
    signal openConfiguration()
    signal openRuntime()
    signal launchRequested()

    readonly property real minimumWidgetWidth: itemData.type === "launch" ? 520 : 180
    readonly property real minimumWidgetHeight: itemData.type === "launch" ? 320 : 120

    x: Math.max(0, Math.min(canvas.width - width, itemData.x * canvas.width))
    y: Math.max(0, Math.min(canvas.height - height, itemData.y * canvas.height))
    width: Math.min(canvas.width, Math.max(minimumWidgetWidth, itemData.w * canvas.width))
    height: Math.min(canvas.height, Math.max(minimumWidgetHeight, itemData.h * canvas.height))
    z: itemData.z || 0
    opacity: itemData.opacity === undefined ? 1.0 : itemData.opacity

    function commitGeometry() {
        appContext.skins.updateItem(itemData.id, {
            "x": Math.max(0, Math.min(1, x / canvas.width)),
            "y": Math.max(0, Math.min(1, y / canvas.height)),
            "w": Math.max(0.08, Math.min(1, width / canvas.width)),
            "h": Math.max(0.08, Math.min(1, height / canvas.height))
        });
    }

    HomeWidgetContent {
        anchors.fill: parent
        appContext: root.appContext
        itemData: root.itemData
        onOpenConfiguration: root.openConfiguration()
        onOpenRuntime: root.openRuntime()
        onLaunchRequested: root.launchRequested()
    }

    Rectangle {
        anchors.fill: parent
        visible: root.editing && root.selected
        color: "transparent"
        border.width: 2
        border.color: Theme.accent
        radius: Theme.radius
    }

    MouseArea {
        id: moveArea
        anchors.fill: parent
        enabled: root.editing
        drag.target: root
        drag.minimumX: 0
        drag.minimumY: 0
        drag.maximumX: Math.max(0, root.canvas.width - root.width)
        drag.maximumY: Math.max(0, root.canvas.height - root.height)
        cursorShape: drag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        onPressed: root.selectionRequested(root.itemData.id)
        onReleased: root.commitGeometry()
    }

    Repeater {
        model: root.editing && root.selected ? [
            { x: -1, y: -1 }, { x: 0, y: -1 }, { x: 1, y: -1 },
            { x: -1, y: 0 },                    { x: 1, y: 0 },
            { x: -1, y: 1 },  { x: 0, y: 1 },  { x: 1, y: 1 }
        ] : []

        Rectangle {
            id: handle
            required property var modelData
            property real startX
            property real startY
            property real startWidth
            property real startHeight
            property real pressX
            property real pressY

            width: 12
            height: 12
            radius: 6
            color: Theme.accent
            border.width: 2
            border.color: Theme.accentForeground
            x: modelData.x < 0 ? -width / 2
               : modelData.x > 0 ? root.width - width / 2
                 : root.width / 2 - width / 2
            y: modelData.y < 0 ? -height / 2
               : modelData.y > 0 ? root.height - height / 2
                 : root.height / 2 - height / 2
            z: 1001

            MouseArea {
                anchors.fill: parent
                cursorShape: handle.modelData.x === 0 ? Qt.SizeVerCursor
                             : handle.modelData.y === 0 ? Qt.SizeHorCursor
                             : handle.modelData.x === handle.modelData.y
                               ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
                onPressed: mouse => {
                    handle.startX = root.x;
                    handle.startY = root.y;
                    handle.startWidth = root.width;
                    handle.startHeight = root.height;
                    const point = handle.mapToItem(root.canvas, mouse.x, mouse.y);
                    handle.pressX = point.x;
                    handle.pressY = point.y;
                }
                onPositionChanged: mouse => {
                    if (!pressed)
                        return;
                    const point = handle.mapToItem(root.canvas, mouse.x, mouse.y);
                    const dx = point.x - handle.pressX;
                    const dy = point.y - handle.pressY;
                    let newX = handle.startX;
                    let newY = handle.startY;
                    let newWidth = handle.startWidth;
                    let newHeight = handle.startHeight;
                    if (handle.modelData.x < 0) {
                        newX = Math.max(0, Math.min(handle.startX + handle.startWidth - root.minimumWidgetWidth, handle.startX + dx));
                        newWidth = handle.startWidth + handle.startX - newX;
                    } else if (handle.modelData.x > 0) {
                        newWidth = Math.max(root.minimumWidgetWidth,
                                            Math.min(root.canvas.width - handle.startX,
                                                     handle.startWidth + dx));
                    }
                    if (handle.modelData.y < 0) {
                        newY = Math.max(0, Math.min(handle.startY + handle.startHeight - root.minimumWidgetHeight, handle.startY + dy));
                        newHeight = handle.startHeight + handle.startY - newY;
                    } else if (handle.modelData.y > 0) {
                        newHeight = Math.max(root.minimumWidgetHeight,
                                             Math.min(root.canvas.height - handle.startY,
                                                      handle.startHeight + dy));
                    }
                    root.x = newX;
                    root.y = newY;
                    root.width = newWidth;
                    root.height = newHeight;
                }
                onReleased: root.commitGeometry()
            }
        }
    }
}
