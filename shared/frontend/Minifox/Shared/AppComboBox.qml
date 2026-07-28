pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Controls.FluentWinUI3 as Fluent
import QtQuick.Templates as T

Fluent.ComboBox {
    id: control

    readonly property var hostWindow: Controls.ApplicationWindow.window

    function widestItemText() {
        let width = 0;
        for (let index = 0; index < count; ++index)
            width = Math.max(width,
                             popupFontMetrics.advanceWidth(textAt(index)));
        return width;
    }

    font.family: Theme.uiFontFamily
    font.pointSize: Theme.bodySize
    palette.window: Theme.surfaceRaised
    palette.windowText: Theme.foreground
    palette.text: Theme.foreground
    palette.button: Theme.surfaceRaised
    palette.buttonText: Theme.foreground
    palette.accent: Theme.accent
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentForeground
    palette.placeholderText: Theme.foregroundSecondary

    delegate: AppItemDelegate {
        required property int index

        width: ListView.view
               ? ListView.view.width - ListView.view.rightMargin
               : comboPopup.width
        implicitHeight: Math.max(Theme.controlHeight,
                                 implicitContentHeight + topPadding + bottomPadding)
        leftPadding: Theme.spacingMd
        rightPadding: Theme.spacingMd
        topPadding: Theme.spacingSm
        bottomPadding: Theme.spacingSm
        text: control.textAt(index)
        font: control.font
        highlighted: control.highlightedIndex === index
        hoverEnabled: control.hoverEnabled
    }

    FontMetrics {
        id: popupFontMetrics
        font: control.font
    }

    popup: T.Popup {
        id: comboPopup

        readonly property point windowPosition: control.hostWindow
                                                ? control.mapToItem(
                                                      control.hostWindow.contentItem,
                                                      0,
                                                      0)
                                                : Qt.point(0, 0)
        readonly property real windowWidth: control.hostWindow
                                            ? control.hostWindow.width : 1280
        readonly property real windowHeight: control.hostWindow
                                             ? control.hostWindow.height : 720
        readonly property real edgeMargin: Theme.spacingSm
        readonly property real availableBelow: Math.max(
                                                   Theme.controlHeight,
                                                   windowHeight
                                                   - windowPosition.y
                                                   - control.height
                                                   - edgeMargin)
        readonly property real availableAbove: Math.max(
                                                   Theme.controlHeight,
                                                   windowPosition.y
                                                   - edgeMargin)
        readonly property real desiredHeight: popupList.contentHeight
                                              + topPadding + bottomPadding
        readonly property bool opensAbove: availableBelow
                                           < Math.min(360, desiredHeight)
                                           && availableAbove > availableBelow
        readonly property real maximumHeight: Math.min(
                                                  360,
                                                  opensAbove
                                                  ? availableAbove
                                                  : availableBelow)
        readonly property real maximumWidth: Math.max(
                                                 control.width,
                                                 windowWidth
                                                 - edgeMargin * 2)
        readonly property real desiredWidth: Math.max(
                                                 control.width,
                                                 control.widestItemText()
                                                 + Theme.spacingMd * 2
                                                 + (popupScrollBar.visible
                                                    ? popupScrollBar.width : 0))

        x: {
            const rightAligned = windowWidth - edgeMargin
                                 - windowPosition.x - width;
            return Math.max(edgeMargin - windowPosition.x,
                            Math.min(0, rightAligned));
        }
        y: opensAbove ? -height : control.height
        width: Math.min(maximumWidth, desiredWidth)
        height: Math.min(popupList.contentHeight + topPadding + bottomPadding, maximumHeight)
        topMargin: Theme.spacingSm
        bottomMargin: Theme.spacingSm
        padding: Theme.spacingXs
        palette: control.palette
        onOpened: popupList.positionViewAtIndex(
                      Math.max(0, control.currentIndex),
                      ListView.Contain)

        contentItem: ListView {
            id: popupList

            clip: true
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            highlightMoveDuration: 0
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: true
            rightMargin: popupScrollBar.visible ? popupScrollBar.width : 0

            T.ScrollBar.vertical: Controls.ScrollBar {
                id: popupScrollBar

                policy: T.ScrollBar.AsNeeded
            }
        }

        background: Rectangle {
            // Popup menus must remain readable even when the selected skin
            // deliberately uses translucent page and panel materials.
            color: Qt.rgba(Theme.surfaceRaised.r,
                           Theme.surfaceRaised.g,
                           Theme.surfaceRaised.b,
                           1.0)
            border.width: 1
            border.color: Theme.dark
                          ? Qt.rgba(1, 1, 1, 0.24)
                          : Qt.rgba(0, 0, 0, 0.18)
            radius: Theme.radius

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: Theme.radius
                anchors.rightMargin: Theme.radius
                height: 1
                color: Theme.materialEdge
            }
        }
    }

    Binding {
        target: control.contentItem
        property: "color"
        value: control.enabled ? Theme.foreground : Theme.foregroundSecondary
        when: control.contentItem !== null
    }
}
