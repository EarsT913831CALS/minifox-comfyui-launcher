pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Minifox.Shared

Rectangle {
    id: root

    required property var progressModel
    required property string consoleFontFamily
    required property real consoleFontSize
    required property bool darkConsole

    readonly property color panelColor: darkConsole ? "#1b1b1b" : "#f3f3f3"
    readonly property color trackColor: darkConsole ? "#404040" : "#d6d6d6"
    readonly property bool hasKnownTotal: progressModel.progressTotal > 0
    readonly property string currentStepText: hasKnownTotal
        ? qsTr("第 %1 / %2 步").arg(progressModel.progressCurrent).arg(progressModel.progressTotal)
        : qsTr("进行中")
    readonly property string detailText: {
        const details = [];
        if (progressModel.progressElapsed.length > 0)
            details.push(qsTr("已用时 %1").arg(progressModel.progressElapsed));
        if (progressModel.progressRemaining.length > 0
                && progressModel.progressRemaining !== "?")
            details.push(qsTr("预计剩余 %1").arg(progressModel.progressRemaining));
        if (progressModel.progressRate.length > 0)
            details.push(qsTr("速度 %1").arg(progressModel.progressRate));
        return details.join("  ·  ");
    }

    visible: progressModel.progressActive
    implicitHeight: progressLayout.implicitHeight + Theme.spacingMd * 2
    color: panelColor
    radius: Theme.radius
    border.width: 1
    border.color: Theme.outline

    ColumnLayout {
        id: progressLayout

        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingSm

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Rectangle {
                Layout.preferredWidth: 8
                Layout.preferredHeight: 8
                radius: 4
                color: Theme.accent
                Accessible.ignored: true
            }

            AppLabel {
                text: root.progressModel.progressLabel.length > 0
                    ? root.progressModel.progressLabel
                    : qsTr("正在执行")
                color: root.darkConsole ? "#f2f2f2" : "#1b1b1b"
                font.family: root.consoleFontFamily
                font.pointSize: root.consoleFontSize
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
                maximumLineCount: 2
                Layout.fillWidth: true
            }

            RowLayout {
                spacing: Theme.spacingSm

                AppLabel {
                    text: root.currentStepText
                    color: Theme.accent
                    font.family: root.consoleFontFamily
                    font.pointSize: root.consoleFontSize + 1
                    font.weight: Font.DemiBold
                }

                AppLabel {
                    text: root.progressModel.progressPercent + "%"
                    color: root.darkConsole ? "#f2f2f2" : "#1b1b1b"
                    font.family: root.consoleFontFamily
                    font.pointSize: root.consoleFontSize
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignRight
                }
            }
        }

        ProgressBar {
            id: progressBar

            Layout.fillWidth: true
            Layout.preferredHeight: 8
            from: 0
            to: 1
            value: root.progressModel.progressValue
            indeterminate: root.progressModel.progressIndeterminate
            Accessible.name: qsTr("ComfyUI 处理进度")
            Accessible.description: root.detailText

            background: Rectangle {
                implicitHeight: 8
                radius: 4
                color: root.trackColor
            }

            contentItem: Item {
                clip: true

                Rectangle {
                    id: determinateFill

                    visible: !progressBar.indeterminate
                    width: parent.width
                    height: parent.height
                    radius: 4
                    color: Theme.accent
                    transform: Scale {
                        origin.x: 0
                        origin.y: determinateFill.height / 2
                        xScale: progressBar.visualPosition
                        yScale: 1

                        Behavior on xScale {
                            enabled: Theme.motionDuration > 0
                            NumberAnimation {
                                duration: Theme.motionDuration
                                easing.type: Easing.OutCubic
                            }
                        }
                    }
                }

                Rectangle {
                    id: indeterminateFill

                    visible: progressBar.indeterminate
                    x: 0
                    width: Math.max(36, parent.width * 0.28)
                    height: parent.height
                    radius: 4
                    color: Theme.accent

                    XAnimator {
                        target: indeterminateFill
                        from: -indeterminateFill.width
                        to: progressBar.availableWidth
                        duration: 1100
                        loops: Animation.Infinite
                        running: indeterminateFill.visible
                            && progressBar.visible
                            && !Theme.reducedMotion
                    }
                }
            }
        }

        AppLabel {
            visible: root.detailText.length > 0
            text: root.detailText
            color: root.darkConsole ? "#a6a6a6" : "#5d5d5d"
            font.family: root.consoleFontFamily
            font.pointSize: root.consoleFontSize
            wrapMode: Text.Wrap
            maximumLineCount: 2
            Layout.fillWidth: true
        }
    }
}
