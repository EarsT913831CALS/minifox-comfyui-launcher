import QtQuick
import QtQuick.Controls.FluentWinUI3 as Fluent

Fluent.ToolButton {
    id: control

    property bool accented: false
    property bool destructive: false
    readonly property int motionDuration: Theme.controlMotionDuration

    hoverEnabled: true
    activeFocusOnTab: true
    implicitWidth: Theme.controlHeight
    implicitHeight: Theme.controlHeight
    leftPadding: 8
    rightPadding: 8
    topPadding: 7
    bottomPadding: 7
    scale: !enabled ? 1.0 : down ? 0.97 : hovered ? 1.015 : 1.0
    transformOrigin: Item.Center
    font.family: Theme.uiFontFamily
    palette.window: Theme.surface
    palette.windowText: Theme.foreground
    palette.text: Theme.foreground
    palette.button: Theme.surfaceRaised
    palette.buttonText: Theme.foreground
    palette.accent: Theme.accent
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentForeground
    palette.placeholderText: Theme.foregroundSecondary

    background: ButtonSurface {
        controlEnabled: control.enabled
        controlHovered: control.hovered
        controlPressed: control.down
        controlFocused: control.visualFocus
        accented: control.accented
        destructive: control.destructive
    }

    Behavior on scale {
        enabled: control.motionDuration > 0

        ScaleAnimator {
            duration: control.motionDuration
            easing.type: Easing.OutCubic
        }
    }

    Binding {
        target: control.contentItem
        property: "color"
        value: !control.enabled
               ? Theme.buttonTextDisabled
               : control.destructive
                 ? Theme.dangerForeground
                 : control.accented
                   ? Theme.accentForeground
                   : Theme.foreground
        when: control.contentItem !== null
    }
}
