pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Controls.FluentWinUI3 as Fluent
import QtQuick.Templates as T

Fluent.ComboBox {
    id: control

    readonly property var hostWindow: Controls.ApplicationWindow.window

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
        required property var model
        required property int index

        width: ListView.view ? ListView.view.width - ListView.view.rightMargin : control.width
        text: model[control.textRole]
        font: control.font
        highlighted: control.highlightedIndex === index
        hoverEnabled: control.hoverEnabled
    }

    popup: T.Popup {
        id: comboPopup

        readonly property real maximumHeight: Math.min(360, Math.max(Theme.controlHeight,
            (control.hostWindow ? control.hostWindow.height : 720) - topMargin - bottomMargin))

        y: control.height
        width: control.width
        height: Math.min(popupList.contentHeight + topPadding + bottomPadding, maximumHeight)
        topMargin: Theme.spacingSm
        bottomMargin: Theme.spacingSm
        padding: Theme.spacingXs
        palette: control.palette

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
            color: Theme.surfaceRaised
            border.width: 1
            border.color: Theme.outline
            radius: Theme.radius
        }
    }

    Binding {
        target: control.contentItem
        property: "color"
        value: control.enabled ? Theme.foreground : Theme.foregroundSecondary
        when: control.contentItem !== null
    }
}
