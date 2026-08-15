import QtQuick
import QtQuick.Controls.FluentWinUI3 as Fluent

Fluent.Button {
    id: control

    property bool accented: false
    property bool compact: false
    property bool destructive: false
    property bool prominent: false
    readonly property int motionDuration: Theme.controlMotionDuration

    hoverEnabled: true
    activeFocusOnTab: true
    implicitWidth: compact
                   ? Math.max(72, implicitContentWidth + leftPadding + rightPadding)
                   : prominent
                   ? Math.max(196, implicitContentWidth + leftPadding + rightPadding)
                   : Math.max(96, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: compact ? 34
                            : prominent ? Theme.prominentControlHeight : Theme.controlHeight
    leftPadding: compact ? 12 : prominent ? 22 : 16
    rightPadding: compact ? 12 : prominent ? 22 : 16
    topPadding: compact ? 5 : 7
    bottomPadding: compact ? 5 : 7
    spacing: Theme.spacingSm
    scale: !enabled ? 1.0 : down && hovered ? 0.97 : 1.0
    transformOrigin: Item.Center
    font.family: Theme.uiFontFamily
    font.pointSize: compact ? Math.max(9, Theme.bodySize * 0.92)
                            : prominent ? Theme.subtitleSize : Theme.bodySize
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
        prominent: control.prominent
    }

    Behavior on scale {
        enabled: control.motionDuration > 0

        ScaleAnimator {
            duration: control.down && control.hovered ? 120 : 90
            easing.type: Easing.BezierSpline
            easing.bezierCurve: [0.23, 1, 0.32, 1, 1, 1]
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
