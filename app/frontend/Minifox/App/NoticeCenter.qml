pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Minifox.Shared

// Version and repository results are shown here instead of on the page that
// started the work: the shell's Loader destroys pages on navigation, so a
// result that arrives later would otherwise be lost.
//
// Several notices collapse into one card with the others peeking out behind
// it; a click fans them out into a list that scrolls with the wheel. Copy and
// close appear on hover, and a click on a card shows its full text. Successes
// close by themselves; failures and warnings stay until dismissed.
Item {
    id: root

    required property var appContext
    property int maximumNotices: 20
    property int nextNoticeId: 0
    property bool expanded: false
    readonly property int cardWidth: Math.min(400, Overlay.overlay ? Overlay.overlay.width - 48 : 400)
    readonly property int motion: Theme.motionDuration

    function post(success, message, warning) {
        const text = String(message || "").trim();
        if (text.length === 0)
            return;
        notices.insert(0, { noticeId: root.nextNoticeId++, success: success, warning: warning === true, message: text });
        while (notices.count > root.maximumNotices) {
            // Drop the oldest self-closing notice first, else the oldest one.
            let victim = notices.count - 1;
            for (let i = notices.count - 1; i >= 0; --i) {
                if (notices.get(i).success && !notices.get(i).warning) {
                    victim = i;
                    break;
                }
            }
            notices.remove(victim);
        }
    }

    function dismiss(noticeId) {
        for (let i = 0; i < notices.count; ++i) {
            if (notices.get(i).noticeId === noticeId) {
                notices.remove(i);
                break;
            }
        }
        if (notices.count < 2)
            root.expanded = false;
    }

    function clearAll() {
        notices.clear();
        root.expanded = false;
    }

    // Esc folds an expanded stack back up.
    Shortcut {
        sequence: "Esc"
        enabled: root.expanded
        onActivated: root.expanded = false
    }

    // Newest first.
    ListModel {
        id: notices
    }

    Connections {
        target: root.appContext.versions

        function onRefreshCompleted(success, message) {
            root.post(success, message);
        }

        function onOperationCompleted(success, message) {
            root.post(success, message);
        }

        function onMergeConflictsReported(message) {
            root.post(true, message, true);
        }

        function onDependencyInstallCompleted(success, message) {
            root.post(success, message);
        }
    }

    component Card: Item {
        id: card

        required property int noticeId
        required property bool success
        required property bool warning
        required property string message
        // Collapsed cards show a few lines; a click reveals the rest.
        property bool clamped: true
        // The front card of a collapsed stack fans the stack out instead.
        property bool stacked: false
        readonly property color tone: card.warning ? Theme.warning : card.success ? Theme.success : Theme.error
        readonly property bool pointerInside: hover.hovered

        implicitWidth: root.cardWidth
        implicitHeight: body.implicitHeight

        HoverHandler {
            id: hover
            cursorShape: card.stacked && notices.count > 1 ? Qt.PointingHandCursor : Qt.ArrowCursor
        }

        // Successes fade out unless the pointer rests on them.
        Timer {
            running: card.success && !card.warning && !card.pointerInside
            interval: 5000
            onTriggered: root.dismiss(card.noticeId)
        }

        Rectangle {
            // A soft shadow without an effect pass.
            y: 3
            width: body.width
            height: body.height
            radius: body.radius
            color: Theme.shadow
            opacity: 0.5
        }

        Rectangle {
            id: body

            width: parent.width
            implicitHeight: content.implicitHeight + 24
            height: implicitHeight
            radius: 14
            color: Qt.rgba(Theme.surfaceRaised.r, Theme.surfaceRaised.g, Theme.surfaceRaised.b, 1)
            border.width: 1
            border.color: Theme.materialStroke

            TapHandler {
                onTapped: {
                    if (card.stacked && notices.count > 1)
                        root.expanded = true;
                    else
                        card.clamped = !card.clamped;
                }
            }

            RowLayout {
                id: content

                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                Rectangle {
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    radius: 12
                    color: Qt.rgba(card.tone.r, card.tone.g, card.tone.b, 0.18)

                    AppLabel {
                        anchors.centerIn: parent
                        text: card.warning ? "" : card.success ? "" : ""
                        font.family: Theme.iconFontFamily
                        font.pointSize: Theme.captionSize
                        color: card.tone
                    }
                }

                AppLabel {
                    id: messageText

                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    text: card.message
                    wrapMode: Text.Wrap
                    maximumLineCount: card.clamped ? 4 : 1000
                    elide: Text.ElideRight
                    font.pointSize: Theme.captionSize
                    lineHeight: 1.1
                }

                Row {
                    Layout.alignment: Qt.AlignTop
                    spacing: 2
                    opacity: card.pointerInside ? 1 : 0
                    visible: opacity > 0

                    Behavior on opacity {
                        NumberAnimation { duration: root.motion }
                    }

                    AppToolButton {
                        width: 26
                        height: 26
                        padding: 0
                        flat: true
                        text: ""
                        font.family: Theme.iconFontFamily
                        font.pointSize: Theme.captionSize - 1
                        Accessible.name: qsTr("复制")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("复制")
                        onClicked: {
                            copyHelper.text = card.message;
                            copyHelper.selectAll();
                            copyHelper.copy();
                        }
                    }

                    AppToolButton {
                        width: 26
                        height: 26
                        padding: 0
                        flat: true
                        text: ""
                        font.family: Theme.iconFontFamily
                        font.pointSize: Theme.captionSize - 1
                        Accessible.name: qsTr("关闭")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("关闭")
                        onClicked: root.dismiss(card.noticeId)
                    }
                }
            }
        }
    }

    // Copying goes through an invisible editor; Qt Quick has no clipboard API.
    TextEdit {
        id: copyHelper
        visible: false
    }

    Popup {
        id: popup

        parent: Overlay.overlay
        visible: notices.count > 0
        x: Overlay.overlay.width - width - 24
        y: Overlay.overlay.height - height - 24
        width: root.cardWidth
        height: Math.min(column.implicitHeight, Overlay.overlay.height - 48)
        padding: 0
        modal: false
        focus: false
        closePolicy: Popup.NoAutoClose
        background: null

        Behavior on height {
            enabled: root.motion > 0
            NumberAnimation { duration: root.motion; easing.type: Easing.OutCubic }
        }

        contentItem: ColumnLayout {
            id: column

            spacing: 8

            // Shown once there is more than one notice, like "Show less / Clear".
            RowLayout {
                visible: notices.count > 1
                Layout.fillWidth: true
                spacing: 6

                Rectangle {
                    Layout.preferredHeight: 26
                    Layout.preferredWidth: countLabel.implicitWidth + 20
                    radius: 13
                    color: Qt.rgba(Theme.surfaceRaised.r, Theme.surfaceRaised.g, Theme.surfaceRaised.b, 1)
                    border.width: 1
                    border.color: Theme.materialStroke

                    AppLabel {
                        id: countLabel
                        anchors.centerIn: parent
                        text: qsTr("%n 条通知", "", notices.count)
                        font.pointSize: Theme.captionSize
                        color: Theme.foregroundSecondary
                    }
                }

                Item { Layout.fillWidth: true }

                AppButton {
                    compact: true
                    Layout.preferredHeight: 26
                    leftPadding: 10
                    rightPadding: 10
                    font.pointSize: Theme.captionSize
                    text: root.expanded ? qsTr("收起") : qsTr("展开")
                    onClicked: root.expanded = !root.expanded
                }

                AppButton {
                    compact: true
                    Layout.preferredHeight: 26
                    leftPadding: 10
                    rightPadding: 10
                    font.pointSize: Theme.captionSize
                    text: qsTr("全部清除")
                    onClicked: root.clearAll()
                }
            }

            // Collapsed: the newest card in front, two more peeking out below.
            Item {
                id: stack

                visible: !root.expanded || notices.count < 2
                Layout.fillWidth: true
                Layout.preferredHeight: front.implicitHeight + Math.max(0, Math.min(2, notices.count - 1)) * 8

                Repeater {
                    model: Math.min(2, Math.max(0, notices.count - 1))

                    Rectangle {
                        required property int index
                        readonly property real inset: (index + 1) * 12

                        x: inset
                        y: front.height - height + (index + 1) * 8
                        z: -index - 1
                        width: stack.width - inset * 2
                        height: 28
                        radius: 12
                        color: Qt.tint(Theme.surfaceRaised, Qt.rgba(0, 0, 0, 0.12 * (index + 1)))
                        border.width: 1
                        border.color: Theme.materialStroke

                        TapHandler { onTapped: root.expanded = true }
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                    }
                }

                Loader {
                    id: front

                    width: parent.width
                    active: notices.count > 0
                    sourceComponent: Card {
                        noticeId: notices.count > 0 ? notices.get(0).noticeId : -1
                        success: notices.count > 0 ? notices.get(0).success : true
                        warning: notices.count > 0 ? notices.get(0).warning : false
                        message: notices.count > 0 ? notices.get(0).message : ""
                        stacked: true
                    }
                    // A new notice replaces the front card's contents.
                    Connections {
                        target: notices
                        function onCountChanged() {
                            front.active = false;
                            front.active = notices.count > 0;
                        }
                    }
                }
            }

            // Expanded: every card, newest nearest the corner, scrolled with the wheel.
            ListView {
                id: list

                visible: root.expanded && notices.count > 1
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, Overlay.overlay.height - 48 - 34)
                spacing: 8
                clip: true
                model: notices
                verticalLayoutDirection: ListView.BottomToTop
                boundsBehavior: Flickable.StopAtBounds
                interactive: contentHeight > height
                ScrollBar.vertical: ScrollBar { policy: list.interactive ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

                delegate: Card {
                    width: ListView.view.width
                    clamped: false
                }

                add: Transition {
                    NumberAnimation { property: "opacity"; from: 0; to: 1; duration: root.motion }
                }
                remove: Transition {
                    NumberAnimation { property: "opacity"; to: 0; duration: root.motion }
                }
                displaced: Transition {
                    NumberAnimation { property: "y"; duration: root.motion; easing.type: Easing.OutCubic }
                }
            }
        }
    }
}
