pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Controls.FluentWinUI3 as Fluent

Controls.Dialog {
    id: control

    property bool destructiveAccept: false
    property bool acceptEnabled: true
    property string acceptText: ""
    property string rejectText: ""

    font.family: Theme.uiFontFamily
    palette.window: Theme.surfaceRaised
    palette.windowText: Theme.foreground
    palette.text: Theme.foreground
    palette.button: Theme.surfaceRaised
    palette.buttonText: Theme.foreground
    palette.accent: Theme.accent
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentForeground
    palette.placeholderText: Theme.foregroundSecondary

    footer: Fluent.DialogButtonBox {
        visible: count > 0
        standardButtons: control.standardButtons

        delegate: AppButton {
            id: dialogButton

            readonly property bool acceptsAction: Controls.DialogButtonBox.buttonRole === Controls.DialogButtonBox.AcceptRole
                                                  || Controls.DialogButtonBox.buttonRole === Controls.DialogButtonBox.YesRole
            readonly property bool rejectsAction: Controls.DialogButtonBox.buttonRole === Controls.DialogButtonBox.RejectRole
                                                  || Controls.DialogButtonBox.buttonRole === Controls.DialogButtonBox.NoRole

            accented: acceptsAction && !control.destructiveAccept
            destructive: acceptsAction && control.destructiveAccept
            enabled: !acceptsAction || control.acceptEnabled

            Binding {
                target: dialogButton
                property: "text"
                value: dialogButton.acceptsAction ? control.acceptText : control.rejectText
                when: (dialogButton.acceptsAction && control.acceptText.length > 0)
                      || (dialogButton.rejectsAction && control.rejectText.length > 0)
            }
        }
    }
}
