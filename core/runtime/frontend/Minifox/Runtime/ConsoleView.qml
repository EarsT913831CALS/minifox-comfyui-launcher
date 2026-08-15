pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import Minifox.Shared

Rectangle {
    id: root

    required property var appContext
    property bool followTail: true
    property bool manualNavigation: false
    property int renderedLogCount: 0
    property bool forceCompact: false
    readonly property bool selectionActive:
        consoleText.selectionStart !== consoleText.selectionEnd
    readonly property string consoleFontFamily: appContext.settings.consoleFontFamily
    readonly property real consoleFontSize: appContext.settings.consoleFontSize
    readonly property bool consoleWordWrap: appContext.settings.consoleWordWrap
    readonly property bool consoleShowTimestamps: appContext.settings.showTimestamps
    readonly property bool darkConsole: appContext.settings.consoleTheme === "dark"
        || (appContext.settings.consoleTheme === "system" && Theme.dark)
    readonly property color consoleBackground: darkConsole ? "#0c0c0c" : "#ffffff"
    readonly property color consoleForeground: darkConsole ? "#f2f2f2" : "#1b1b1b"
    readonly property color consoleSecondary: darkConsole ? "#a6a6a6" : "#6b6b6b"
    readonly property Flickable consoleViewport:
        consoleScroll.contentItem as Flickable

    color: root.consoleBackground
    radius: Theme.radius
    border.width: 1
    border.color: Theme.outline

    Component.onCompleted: root.rebuildConsoleText()
    onConsoleShowTimestampsChanged: root.rebuildConsoleText()
    onForceCompactChanged: root.rebuildConsoleText()
    onDarkConsoleChanged: root.rebuildConsoleText()

    Connections {
        target: root.appContext.runtime.logModel

        function onCountChanged() {
            root.scheduleLogRefresh();
        }
    }

    ScrollView {
        id: consoleScroll
        objectName: "consoleScroll"

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: progressPanel.visible ? progressPanel.top : parent.bottom
        anchors.margins: 1
        anchors.bottomMargin: progressPanel.visible ? Theme.spacingSm : 1
        clip: true
        contentWidth: availableWidth
        LayoutMirroring.enabled: false
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        Component.onCompleted: {
            // ScrollView owns the only viewport. Prevent its internal Flickable
            // from exposing any area beyond the real document bounds.
            root.consoleViewport.boundsBehavior = Flickable.StopAtBounds;
            root.consoleViewport.boundsMovement = Flickable.StopAtBounds;
        }

        ScrollBar.vertical: ScrollBar {
            id: verticalBar

            parent: consoleScroll
            anchors.top: consoleScroll.top
            anchors.right: consoleScroll.right
            anchors.bottom: consoleScroll.bottom
            anchors.topMargin: 2
            anchors.rightMargin: 2
            anchors.bottomMargin: 2
            z: 4
            policy: ScrollBar.AsNeeded
            interactive: true
            hoverEnabled: true
            implicitWidth: 12
            padding: 3
            minimumSize: Math.min(1.0, 28 / Math.max(1, height))
            active: root.manualNavigation || pressed || hovered
            opacity: size < 1.0
                     ? (root.manualNavigation || pressed || hovered ? 1.0 : 0.55)
                     : 0.0

            background: Rectangle {
                color: "transparent"
            }

            contentItem: Rectangle {
                implicitWidth: 6
                radius: width / 2
                color: root.darkConsole ? "#f2f2f2" : "#1b1b1b"

                Behavior on color {
                    ColorAnimation {
                        duration: Theme.controlMotionDuration
                    }
                }
            }

            Behavior on opacity {
                NumberAnimation {
                    duration: Theme.controlMotionDuration
                    easing.type: Easing.OutCubic
                }
            }

            onSizeChanged: {
                if (root.followTail)
                    root.scheduleScrollToEnd();
            }
            onPositionChanged: {
                if (pressed)
                    root.parkTextCursorInViewport();
            }
            onPressedChanged: {
                if (pressed) {
                    root.beginUserNavigation();
                } else {
                    root.parkTextCursorInViewport();
                }
            }
        }

        AppTextArea {
            id: consoleText
            objectName: "consoleText"

            width: consoleScroll.availableWidth
            leftPadding: Theme.spacingMd
            rightPadding: Theme.spacingMd
            topPadding: Theme.spacingSm
            bottomPadding: Theme.spacingSm
            text: ""
            readOnly: true
            selectByMouse: true
            persistentSelection: false
            activeFocusOnPress: true
            cursorVisible: false
            textFormat: TextEdit.RichText
            verticalAlignment: TextEdit.AlignTop
            color: root.consoleForeground
            selectionColor: Theme.accent
            selectedTextColor: root.consoleForeground
            font.family: root.consoleFontFamily
            font.pointSize: root.consoleFontSize
            font.weight: Font.Normal
            wrapMode: root.consoleWordWrap ? TextEdit.WrapAnywhere : TextEdit.Wrap
            background: null
            placeholderText: qsTr("启动 ComfyUI 后，完整输出会显示在这里。")
            placeholderTextColor: root.consoleSecondary

            onContentHeightChanged: {
                if (root.followTail)
                    root.scheduleScrollToEnd();
            }

            onActiveFocusChanged: {
                if (!activeFocus && root.selectionActive)
                    deselect();
            }
        }

        WheelHandler {
            target: null
            orientation: Qt.Vertical
            blocking: false
            onWheel: event => {
                root.handleWheelInput(event.angleDelta.y, event.pixelDelta.y);
            }
        }
    }

    ToolButton {
        id: resumeTailButton
        objectName: "resumeTailButton"

        visible: root.manualNavigation && root.renderedLogCount > 0
        z: 2
        anchors.right: consoleScroll.right
        anchors.bottom: consoleScroll.bottom
        anchors.rightMargin: Theme.spacingMd
            + (verticalBar.visible ? verticalBar.width : 0)
        anchors.bottomMargin: Theme.spacingMd
        implicitWidth: Theme.controlHeight
        implicitHeight: Theme.controlHeight
        hoverEnabled: true
        activeFocusOnTab: true
        scale: down && hovered ? 0.97 : 1.0

        contentItem: IconLabel {
            glyph: "\uE70D"
            iconPointSize: Theme.subtitleSize
            color: root.consoleForeground
        }

        background: Rectangle {
            radius: width / 2
            color: root.darkConsole
                ? Qt.rgba(0.10, 0.10, 0.11,
                          resumeTailButton.down ? 0.76
                                                : resumeTailButton.hovered ? 0.66 : 0.52)
                : Qt.rgba(1, 1, 1,
                          resumeTailButton.down ? 0.94
                                                : resumeTailButton.hovered ? 0.88 : 0.76)
            border.width: 1
            border.color: root.darkConsole
                ? Qt.rgba(1, 1, 1, resumeTailButton.hovered ? 0.34 : 0.22)
                : Qt.rgba(0, 0, 0, resumeTailButton.hovered ? 0.28 : 0.18)
        }

        Accessible.name: qsTr("跳到最新输出")
        ToolTip.visible: hovered
        ToolTip.text: qsTr("跳到最新输出")
        onClicked: root.resumeTailFollowing()

        Behavior on scale {
            enabled: Theme.controlMotionDuration > 0

            ScaleAnimator {
                duration: resumeTailButton.down && resumeTailButton.hovered ? 120 : 90
                easing.type: Easing.BezierSpline
                easing.bezierCurve: [0.23, 1, 0.32, 1, 1, 1]
            }
        }
    }

    Timer {
        id: logRefreshTimer

        interval: 32
        repeat: false
        onTriggered: root.refreshConsoleText()
    }

    Timer {
        id: tailUpdateTimer

        interval: 0
        repeat: false
        onTriggered: root.scrollToEnd()
    }

    function scheduleScrollToEnd() {
        if (!tailUpdateTimer.running)
            tailUpdateTimer.start();
    }

    function scheduleLogRefresh() {
        if (!logRefreshTimer.running)
            logRefreshTimer.start();
    }

    function refreshConsoleText() {
        const model = root.appContext.runtime.logModel;
        const modelCount = model.count;
        const wasFollowing = root.followTail;

        if (modelCount < root.renderedLogCount) {
            consoleText.deselect();
            consoleText.clear();
            root.renderedLogCount = 0;
        }

        if (modelCount > root.renderedLogCount) {
            // Append structured text blocks through a private QTextCursor.
            // This preserves the TextArea's selection/cursor and gives wrapped
            // lines a stable hanging indent without HTML table semantics.
            model.appendStyledTextRangeToDocument(
                consoleText.textDocument,
                root.renderedLogCount,
                root.consoleShowTimestamps,
                root.forceCompact,
                root.consoleSecondary,
                root.consoleForeground,
                Theme.warning,
                Theme.info,
                Theme.success,
                Theme.error);
            root.renderedLogCount = modelCount;
        }

        if (wasFollowing)
            root.scheduleScrollToEnd();
    }

    function rebuildConsoleText() {
        consoleText.deselect();
        consoleText.clear();
        root.renderedLogCount = 0;
        root.scheduleLogRefresh();
    }

    function handleWheelInput(angleDeltaY, pixelDeltaY) {
        // Hover and pointer movement are not navigation. Only a real wheel or
        // touchpad scroll delta may hand control from follow-tail to the user.
        if (Math.abs(angleDeltaY) + Math.abs(pixelDeltaY) <= 0)
            return;

        root.beginUserNavigation();
        // Park immediately as well as after event delivery. This closes the
        // race where a log batch arrives between the wheel callback and the
        // deferred ScrollView update while TextArea's cursor is still at EOF.
        root.parkTextCursorInViewport();
        Qt.callLater(root.parkTextCursorInViewport);
    }

    function scrollToEnd() {
        if (!root.followTail)
            return;

        root.consoleViewport.contentY = Math.max(
            0,
            root.consoleViewport.contentHeight - root.consoleViewport.height);
    }

    function beginUserNavigation() {
        tailUpdateTimer.stop();
        root.manualNavigation = true;
        root.followTail = false;
    }

    function resumeTailFollowing() {
        root.manualNavigation = false;
        root.followTail = true;
        root.scrollToEnd();
        root.scheduleScrollToEnd();
    }

    function parkTextCursorInViewport() {
        if (!root.manualNavigation || root.selectionActive)
            return;

        const viewportCenterY = root.consoleViewport.contentY
            + root.consoleViewport.height / 2;
        const visiblePosition = consoleText.positionAt(
            consoleText.leftPadding,
            viewportCenterY);
        if (visiblePosition >= 0)
            consoleText.cursorPosition = visiblePosition;
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
