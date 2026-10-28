import QtQuick
import QtQuick.Layouts

// Peer actions in one row all take the width of the widest visible one;
// children bind Layout.preferredWidth to cellWidth.
RowLayout {
    readonly property real cellWidth: {
        let width = 0;
        for (const child of children) {
            if (child.visible)
                width = Math.max(width, child.implicitWidth);
        }
        return width;
    }

    spacing: Theme.spacingSm
}
