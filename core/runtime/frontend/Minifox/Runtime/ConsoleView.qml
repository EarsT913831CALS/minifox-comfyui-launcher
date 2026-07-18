pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Minifox.Shared

Rectangle {
    id: root

    required property var appContext
    property bool followTail: true
    property bool forceCompact: false
    readonly property string consoleFontFamily: appContext.settings.consoleFontFamily
    readonly property real consoleFontSize: appContext.settings.consoleFontSize
    readonly property bool consoleWordWrap: appContext.settings.consoleWordWrap
    readonly property bool consoleShowTimestamps: appContext.settings.showTimestamps
    readonly property bool darkConsole: appContext.settings.consoleTheme === "dark" || (appContext.settings.consoleTheme === "system" && Theme.dark)
    readonly property color consoleBackground: darkConsole ? "#0c0c0c" : "#ffffff"
    readonly property color consoleForeground: darkConsole ? "#f2f2f2" : "#1b1b1b"
    readonly property color consoleSecondary: darkConsole ? "#a6a6a6" : "#6b6b6b"
    readonly property real lineNumberColumnWidth: Math.ceil(lineNumberMetrics.advanceWidth)
    readonly property real timestampColumnWidth: Math.ceil(timestampMetrics.advanceWidth)
    readonly property real streamColumnWidth: Math.ceil(streamMetrics.advanceWidth)

    color: root.consoleBackground
    radius: Theme.radius
    border.width: 1
    border.color: Theme.outline

    TextMetrics {
        id: lineNumberMetrics
        font.family: root.consoleFontFamily
        font.pointSize: root.consoleFontSize
        text: "000000"
    }

    TextMetrics {
        id: timestampMetrics
        font.family: root.consoleFontFamily
        font.pointSize: root.consoleFontSize
        text: "88:88:88.888"
    }

    TextMetrics {
        id: streamMetrics
        font.family: root.consoleFontFamily
        font.pointSize: root.consoleFontSize
        font.weight: Font.DemiBold
        text: "SYS"
    }

    ListView {
        id: consoleList
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: progressPanel.visible ? progressPanel.top : parent.bottom
        anchors.topMargin: 1
        anchors.leftMargin: 1
        anchors.rightMargin: 1
        anchors.bottomMargin: progressPanel.visible ? Theme.spacingSm : 1
        model: root.appContext.runtime.logModel
        reuseItems: true
        clip: true
        spacing: 1
        contentWidth: root.consoleWordWrap
            ? width
            : Math.max(width, contentItem.childrenRect.width)
        flickableDirection: root.consoleWordWrap
            ? Flickable.VerticalFlick
            : Flickable.HorizontalAndVerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        ScrollBar.horizontal: ScrollBar {
            policy: root.consoleWordWrap ? ScrollBar.AlwaysOff : ScrollBar.AsNeeded
        }

        delegate: Item {
            id: logDelegate
            objectName: "consoleLogRow"

            required property int index
            required property string timestamp
            required property string text
            required property string stream
            required property string ansiColor
            required property int lineNumber

            width: root.consoleWordWrap
                ? ListView.view.width
                : Math.max(ListView.view.width, logRow.implicitWidth + Theme.spacingSm * 2)
            implicitHeight: logRow.implicitHeight + Theme.spacingXs * 2

            RowLayout {
                id: logRow
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingSm
                anchors.rightMargin: Theme.spacingSm
                spacing: Theme.spacingSm

                AppLabel {
                    objectName: "consoleLineNumber"
                    visible: !root.forceCompact
                    text: logDelegate.lineNumber
                    color: root.consoleSecondary
                    font.family: root.consoleFontFamily
                    font.pointSize: root.consoleFontSize
                    horizontalAlignment: Text.AlignRight
                    Layout.minimumWidth: root.lineNumberColumnWidth
                    Layout.preferredWidth: root.lineNumberColumnWidth
                    Layout.alignment: Qt.AlignTop
                }

                AppLabel {
                    objectName: "consoleTimestamp"
                    visible: !root.forceCompact && root.consoleShowTimestamps
                    text: logDelegate.timestamp
                    color: root.consoleSecondary
                    font.family: root.consoleFontFamily
                    font.pointSize: root.consoleFontSize
                    Layout.minimumWidth: root.timestampColumnWidth
                    Layout.preferredWidth: root.timestampColumnWidth
                    Layout.alignment: Qt.AlignTop
                }

                AppLabel {
                    objectName: "consoleStream"
                    visible: !root.forceCompact
                    text: logDelegate.stream === "stderr" ? "ERR" : (logDelegate.stream === "system" ? "SYS" : "")
                    color: logDelegate.stream === "stderr" ? Theme.warning : Theme.info
                    font.family: root.consoleFontFamily
                    font.pointSize: root.consoleFontSize
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignLeft
                    Layout.minimumWidth: root.streamColumnWidth
                    Layout.preferredWidth: root.streamColumnWidth
                    Layout.alignment: Qt.AlignTop
                }

                AppLabel {
                    objectName: "consoleContent"
                    text: logDelegate.text
                    textFormat: Text.PlainText
                    color: logDelegate.ansiColor.length > 0 ? logDelegate.ansiColor : root.consoleForeground
                    font.family: root.consoleFontFamily
                    font.pointSize: root.consoleFontSize
                    wrapMode: root.consoleWordWrap ? Text.WrapAnywhere : Text.NoWrap
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                }
            }
        }

        onMovementStarted: root.followTail = false
        onMovementEnded: root.followTail = atYEnd
        onCountChanged: {
            if (root.followTail)
                Qt.callLater(positionViewAtEnd);
        }

        AppLabel {
            anchors.centerIn: parent
            visible: consoleList.count === 0
            text: qsTr("启动 ComfyUI 后，完整输出会显示在这里。")
            color: root.consoleSecondary
            font.family: root.consoleFontFamily
            font.pointSize: root.consoleFontSize
        }
    }

    ConsoleProgressPanel {
        id: progressPanel

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacingSm
        progressModel: root.appContext.runtime.logModel
        consoleFontFamily: root.consoleFontFamily
        consoleFontSize: root.consoleFontSize
        darkConsole: root.darkConsole
    }
}
