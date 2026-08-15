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
    signal alignmentGuidesRequested(string owner, var vertical, var horizontal, var spacing)
    signal alignmentGuidesClearRequested(string owner)
    signal openConfiguration()
    signal openRuntime()
    signal launchRequested()

    readonly property real minimumWidgetWidth: minimumWidthFor(itemData)
    readonly property real minimumWidgetHeight: minimumHeightFor(
                                                    itemData,
                                                    width,
                                                    itemData.h * canvas.height)
    readonly property real snapDistance: 7 / Math.max(0.05, Math.abs(canvas.scale))
    readonly property bool interactionActive: moveArea.drag.active || resizeActive
    property bool resizeActive: false

    function folderCount(data) {
        const properties = data && data.properties ? data.properties : ({});
        const folders = properties.folders;
        return folders && folders.length > 0
               ? Math.max(1, Math.min(8, folders.length)) : 4;
    }

    function folderRequiredHeight(count, columns) {
        const rows = Math.ceil(count / columns);
        return 92 + rows * Theme.controlHeight
               + Math.max(0, rows - 1) * Theme.spacingSm;
    }

    function folderColumnsForSize(pixelWidth, pixelHeight, count) {
        if (count <= 1 || pixelWidth < 360)
            return 1;
        const oneColumnHeight = folderRequiredHeight(count, 1);
        return pixelWidth > 480 || pixelHeight < oneColumnHeight ? 2 : 1;
    }

    function minimumWidthFor(data) {
        return data.type === "launch" ? 520 : 180;
    }

    function minimumHeightFor(data, pixelWidth, pixelHeight) {
        if (data.type === "launch")
            return 320;
        if (data.type !== "folders")
            return 120;
        const count = folderCount(data);
        const columns = folderColumnsForSize(pixelWidth, pixelHeight, count);
        return Math.max(120, folderRequiredHeight(count, columns));
    }

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

    function itemGeometry(data) {
        const minimumWidth = minimumWidthFor(data);
        const itemWidth = Math.min(
            canvas.width,
            Math.max(minimumWidth,
                     (data.w === undefined ? 0.4 : data.w) * canvas.width));
        const requestedHeight = (data.h === undefined ? 0.3 : data.h) * canvas.height;
        const minimumHeight = minimumHeightFor(data, itemWidth, requestedHeight);
        const itemHeight = Math.min(
            canvas.height,
            Math.max(minimumHeight,
                     requestedHeight));
        return {
            "x": Math.max(0, Math.min(canvas.width - itemWidth,
                                       (data.x || 0) * canvas.width)),
            "y": Math.max(0, Math.min(canvas.height - itemHeight,
                                       (data.y || 0) * canvas.height)),
            "width": itemWidth,
            "height": itemHeight
        };
    }

    function alignmentTargets(horizontal) {
        let targets = [];
        const items = appContext.skins.homeItems;
        for (let index = 0; index < items.length; ++index) {
            const data = items[index];
            if (data.id === itemData.id)
                continue;
            const geometry = itemGeometry(data);
            if (horizontal) {
                targets.push(geometry.x);
                targets.push(geometry.x + geometry.width / 2);
                targets.push(geometry.x + geometry.width);
            } else {
                targets.push(geometry.y);
                targets.push(geometry.y + geometry.height / 2);
                targets.push(geometry.y + geometry.height);
            }
        }
        return targets;
    }

    function peerGeometries() {
        let peers = [];
        const items = appContext.skins.homeItems;
        for (let index = 0; index < items.length; ++index) {
            const data = items[index];
            if (data.id === itemData.id)
                continue;
            const geometry = itemGeometry(data);
            geometry.id = data.id;
            peers.push(geometry);
        }
        return peers;
    }

    function closestAlignment(anchors, targets) {
        let result = {"matched": false, "delta": 0, "line": 0};
        let closestDistance = snapDistance + 0.001;
        for (const anchor of anchors) {
            for (const target of targets) {
                const distance = Math.abs(target - anchor);
                if (distance < closestDistance) {
                    closestDistance = distance;
                    result = {
                        "matched": true,
                        "delta": target - anchor,
                        "line": target
                    };
                }
            }
        }
        return result;
    }

    function horizontalSpacingAlignment(proposedX, proposedY) {
        let best = {"matched": false, "delta": 0, "guides": []};
        let closestDistance = snapDistance + 0.001;
        const peers = peerGeometries();
        for (let firstIndex = 0; firstIndex < peers.length; ++firstIndex) {
            for (let secondIndex = firstIndex + 1;
                 secondIndex < peers.length; ++secondIndex) {
                let left = peers[firstIndex];
                let right = peers[secondIndex];
                if (left.x > right.x) {
                    const swap = left;
                    left = right;
                    right = swap;
                }
                const leftRight = left.x + left.width;
                if (leftRight > right.x)
                    continue;
                const bandTop = Math.max(proposedY, left.y, right.y);
                const bandBottom = Math.min(proposedY + height,
                                            left.y + left.height,
                                            right.y + right.height);
                if (bandBottom <= bandTop)
                    continue;
                const coordinate = (bandTop + bandBottom) / 2;
                const fixedGap = right.x - leftRight;
                const candidates = [
                    {
                        "position": (leftRight + right.x - width) / 2,
                        "minimum": leftRight,
                        "maximum": right.x - width,
                        "guides": function(position) {
                            const gap = position - leftRight;
                            return [
                                {"orientation": "horizontal", "from": leftRight,
                                 "to": position, "coordinate": coordinate,
                                 "distance": gap},
                                {"orientation": "horizontal", "from": position + width,
                                 "to": right.x, "coordinate": coordinate,
                                 "distance": gap}
                            ];
                        }
                    },
                    {
                        "position": right.x + right.width + fixedGap,
                        "minimum": right.x + right.width,
                        "maximum": canvas.width - width,
                        "guides": function(position) {
                            return [
                                {"orientation": "horizontal", "from": leftRight,
                                 "to": right.x, "coordinate": coordinate,
                                 "distance": fixedGap},
                                {"orientation": "horizontal",
                                 "from": right.x + right.width, "to": position,
                                 "coordinate": coordinate, "distance": fixedGap}
                            ];
                        }
                    },
                    {
                        "position": left.x - fixedGap - width,
                        "minimum": 0,
                        "maximum": left.x - width,
                        "guides": function(position) {
                            return [
                                {"orientation": "horizontal", "from": position + width,
                                 "to": left.x, "coordinate": coordinate,
                                 "distance": fixedGap},
                                {"orientation": "horizontal", "from": leftRight,
                                 "to": right.x, "coordinate": coordinate,
                                 "distance": fixedGap}
                            ];
                        }
                    }
                ];
                for (const candidate of candidates) {
                    if (candidate.position < candidate.minimum
                            || candidate.position > candidate.maximum)
                        continue;
                    const distance = Math.abs(candidate.position - proposedX);
                    if (distance < closestDistance) {
                        closestDistance = distance;
                        best = {
                            "matched": true,
                            "delta": candidate.position - proposedX,
                            "guides": candidate.guides(candidate.position)
                        };
                    }
                }
            }
        }
        return best;
    }

    function verticalSpacingAlignment(proposedX, proposedY) {
        let best = {"matched": false, "delta": 0, "guides": []};
        let closestDistance = snapDistance + 0.001;
        const peers = peerGeometries();
        for (let firstIndex = 0; firstIndex < peers.length; ++firstIndex) {
            for (let secondIndex = firstIndex + 1;
                 secondIndex < peers.length; ++secondIndex) {
                let top = peers[firstIndex];
                let bottom = peers[secondIndex];
                if (top.y > bottom.y) {
                    const swap = top;
                    top = bottom;
                    bottom = swap;
                }
                const topBottom = top.y + top.height;
                if (topBottom > bottom.y)
                    continue;
                const bandLeft = Math.max(proposedX, top.x, bottom.x);
                const bandRight = Math.min(proposedX + width,
                                           top.x + top.width,
                                           bottom.x + bottom.width);
                if (bandRight <= bandLeft)
                    continue;
                const coordinate = (bandLeft + bandRight) / 2;
                const fixedGap = bottom.y - topBottom;
                const candidates = [
                    {
                        "position": (topBottom + bottom.y - height) / 2,
                        "minimum": topBottom,
                        "maximum": bottom.y - height,
                        "guides": function(position) {
                            const gap = position - topBottom;
                            return [
                                {"orientation": "vertical", "from": topBottom,
                                 "to": position, "coordinate": coordinate,
                                 "distance": gap},
                                {"orientation": "vertical", "from": position + height,
                                 "to": bottom.y, "coordinate": coordinate,
                                 "distance": gap}
                            ];
                        }
                    },
                    {
                        "position": bottom.y + bottom.height + fixedGap,
                        "minimum": bottom.y + bottom.height,
                        "maximum": canvas.height - height,
                        "guides": function(position) {
                            return [
                                {"orientation": "vertical", "from": topBottom,
                                 "to": bottom.y, "coordinate": coordinate,
                                 "distance": fixedGap},
                                {"orientation": "vertical",
                                 "from": bottom.y + bottom.height, "to": position,
                                 "coordinate": coordinate, "distance": fixedGap}
                            ];
                        }
                    },
                    {
                        "position": top.y - fixedGap - height,
                        "minimum": 0,
                        "maximum": top.y - height,
                        "guides": function(position) {
                            return [
                                {"orientation": "vertical", "from": position + height,
                                 "to": top.y, "coordinate": coordinate,
                                 "distance": fixedGap},
                                {"orientation": "vertical", "from": topBottom,
                                 "to": bottom.y, "coordinate": coordinate,
                                 "distance": fixedGap}
                            ];
                        }
                    }
                ];
                for (const candidate of candidates) {
                    if (candidate.position < candidate.minimum
                            || candidate.position > candidate.maximum)
                        continue;
                    const distance = Math.abs(candidate.position - proposedY);
                    if (distance < closestDistance) {
                        closestDistance = distance;
                        best = {
                            "matched": true,
                            "delta": candidate.position - proposedY,
                            "guides": candidate.guides(candidate.position)
                        };
                    }
                }
            }
        }
        return best;
    }

    function snapMove(proposedX, proposedY) {
        let snappedX = Math.max(0, Math.min(canvas.width - width, proposedX));
        let snappedY = Math.max(0, Math.min(canvas.height - height, proposedY));
        const vertical = closestAlignment(
            [snappedX, snappedX + width / 2, snappedX + width],
            alignmentTargets(true));
        const horizontal = closestAlignment(
            [snappedY, snappedY + height / 2, snappedY + height],
            alignmentTargets(false));
        const horizontalSpacing = horizontalSpacingAlignment(snappedX, snappedY);
        const verticalSpacing = verticalSpacingAlignment(snappedX, snappedY);
        let verticalGuides = [];
        let horizontalGuides = [];
        let spacingGuides = [];
        if (horizontalSpacing.matched
                && (!vertical.matched
                    || Math.abs(horizontalSpacing.delta) < Math.abs(vertical.delta))) {
            snappedX = Math.max(0, Math.min(canvas.width - width,
                                           snappedX + horizontalSpacing.delta));
            spacingGuides = spacingGuides.concat(horizontalSpacing.guides);
        } else if (vertical.matched) {
            snappedX = Math.max(0, Math.min(canvas.width - width,
                                           snappedX + vertical.delta));
            verticalGuides = [vertical.line];
        }
        if (verticalSpacing.matched
                && (!horizontal.matched
                    || Math.abs(verticalSpacing.delta) < Math.abs(horizontal.delta))) {
            snappedY = Math.max(0, Math.min(canvas.height - height,
                                           snappedY + verticalSpacing.delta));
            spacingGuides = spacingGuides.concat(verticalSpacing.guides);
        } else if (horizontal.matched) {
            snappedY = Math.max(0, Math.min(canvas.height - height,
                                           snappedY + horizontal.delta));
            horizontalGuides = [horizontal.line];
        }
        return {
            "x": snappedX,
            "y": snappedY,
            "verticalGuides": verticalGuides,
            "horizontalGuides": horizontalGuides,
            "spacingGuides": spacingGuides
        };
    }

    function snapResize(geometry, horizontalDirection, verticalDirection) {
        let result = {
            "x": geometry.x,
            "y": geometry.y,
            "width": geometry.width,
            "height": geometry.height,
            "verticalGuides": [],
            "horizontalGuides": [],
            "spacingGuides": []
        };
        if (horizontalDirection !== 0) {
            const edge = horizontalDirection < 0
                         ? result.x : result.x + result.width;
            const alignment = closestAlignment([edge], alignmentTargets(true));
            if (alignment.matched) {
                if (horizontalDirection < 0) {
                    const fixedRight = result.x + result.width;
                    const candidateWidth = fixedRight - alignment.line;
                    if (alignment.line >= 0 && candidateWidth >= minimumWidgetWidth) {
                        result.x = alignment.line;
                        result.width = candidateWidth;
                        result.verticalGuides = [alignment.line];
                    }
                } else {
                    const candidateWidth = alignment.line - result.x;
                    if (alignment.line <= canvas.width
                            && candidateWidth >= minimumWidgetWidth) {
                        result.width = candidateWidth;
                        result.verticalGuides = [alignment.line];
                    }
                }
            }
        }
        if (verticalDirection !== 0) {
            const requiredHeight = minimumHeightFor(
                                       itemData, result.width, result.height);
            const edge = verticalDirection < 0
                         ? result.y : result.y + result.height;
            const alignment = closestAlignment([edge], alignmentTargets(false));
            if (alignment.matched) {
                if (verticalDirection < 0) {
                    const fixedBottom = result.y + result.height;
                    const candidateHeight = fixedBottom - alignment.line;
                    if (alignment.line >= 0 && candidateHeight >= requiredHeight) {
                        result.y = alignment.line;
                        result.height = candidateHeight;
                        result.horizontalGuides = [alignment.line];
                    }
                } else {
                    const candidateHeight = alignment.line - result.y;
                    if (alignment.line <= canvas.height
                            && candidateHeight >= requiredHeight) {
                        result.height = candidateHeight;
                        result.horizontalGuides = [alignment.line];
                    }
                }
            }
        }
        return result;
    }

    function publishGuides(vertical, horizontal, spacing) {
        alignmentGuidesRequested(itemData.id, vertical, horizontal, spacing);
    }

    function clearGuides() {
        alignmentGuidesClearRequested(itemData.id);
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

    Rectangle {
        id: sizeBadge

        visible: root.editing && root.selected && root.interactionActive
        x: Theme.spacingSm
        y: root.y >= height + Theme.spacingSm
           ? -height - Theme.spacingSm : Theme.spacingSm
        z: 2001
        width: sizeLabel.implicitWidth + Theme.spacingMd * 2
        height: 28
        radius: Theme.controlRadius
        color: Theme.materialFillStrong
        border.width: 1
        border.color: Theme.accent

        AppLabel {
            id: sizeLabel

            anchors.centerIn: parent
            text: Math.round(root.width) + " × " + Math.round(root.height) + " px"
            color: Theme.foreground
            font.pointSize: Theme.captionSize
            font.weight: Font.DemiBold
        }
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
        drag.smoothed: false
        cursorShape: drag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        onPressed: {
            root.selectionRequested(root.itemData.id);
            root.clearGuides();
        }
        onPositionChanged: {
            if (!drag.active)
                return;
            const geometry = root.snapMove(root.x, root.y);
            root.x = geometry.x;
            root.y = geometry.y;
            root.publishGuides(geometry.verticalGuides,
                               geometry.horizontalGuides,
                               geometry.spacingGuides);
        }
        onReleased: {
            root.clearGuides();
            root.commitGeometry();
        }
        onCanceled: root.clearGuides()
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
                    root.resizeActive = true;
                    root.clearGuides();
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
                    const requiredMinimumHeight = root.minimumHeightFor(
                                                      root.itemData,
                                                      newWidth,
                                                      newHeight);
                    if (handle.modelData.y < 0) {
                        newY = Math.max(
                                   0,
                                   Math.min(handle.startY + handle.startHeight
                                            - requiredMinimumHeight,
                                            handle.startY + dy));
                        newHeight = handle.startHeight + handle.startY - newY;
                    } else if (handle.modelData.y > 0) {
                        newHeight = Math.max(requiredMinimumHeight,
                                             Math.min(root.canvas.height - handle.startY,
                                                      handle.startHeight + dy));
                    }
                    if (newHeight < requiredMinimumHeight) {
                        newHeight = Math.min(root.canvas.height,
                                             requiredMinimumHeight);
                        newY = Math.max(0, Math.min(
                                            newY,
                                            root.canvas.height - newHeight));
                    }
                    const geometry = root.snapResize({
                        "x": newX,
                        "y": newY,
                        "width": newWidth,
                        "height": newHeight
                    }, handle.modelData.x, handle.modelData.y);
                    root.x = geometry.x;
                    root.y = geometry.y;
                    root.width = geometry.width;
                    root.height = geometry.height;
                    root.publishGuides(geometry.verticalGuides,
                                       geometry.horizontalGuides,
                                       geometry.spacingGuides);
                }
                onReleased: {
                    root.resizeActive = false;
                    root.clearGuides();
                    root.commitGeometry();
                }
                onCanceled: {
                    root.resizeActive = false;
                    root.clearGuides();
                }
            }
        }
    }

    onEditingChanged: {
        if (!editing)
            clearGuides();
    }
    Component.onDestruction: clearGuides()
}
