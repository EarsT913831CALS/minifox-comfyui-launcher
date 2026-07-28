import QtQuick

Item {
    id: root

    property url source
    property string fillMode: "cover"
    property real focusX: 0.5
    property real focusY: 0.5
    property real zoom: 1.0
    property bool asynchronous: true
    property bool cache: true
    readonly property int status: image.status
    readonly property size nativeSize: Qt.size(image.implicitWidth, image.implicitHeight)
    readonly property size renderedSize: Qt.size(image.width, image.height)

    clip: true

    function bounded(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value));
    }

    Image {
        id: image

        readonly property bool cropped: root.fillMode === "cover"
        readonly property real naturalWidth: Math.max(1, implicitWidth)
        readonly property real naturalHeight: Math.max(1, implicitHeight)
        readonly property real coverScale: Math.max(root.width / naturalWidth,
                                                    root.height / naturalHeight)
                                                 * root.bounded(root.zoom, 1, 4)

        source: root.source
        asynchronous: root.asynchronous
        cache: root.cache
        smooth: true
        mipmap: true
        width: cropped ? naturalWidth * coverScale : root.width
        height: cropped ? naturalHeight * coverScale : root.height
        x: cropped ? (root.width - width) * root.bounded(root.focusX, 0, 1) : 0
        y: cropped ? (root.height - height) * root.bounded(root.focusY, 0, 1) : 0
        fillMode: root.fillMode === "contain"
                  ? Image.PreserveAspectFit
                  : root.fillMode === "tile"
                    ? Image.Tile
                    : Image.Stretch
    }
}
