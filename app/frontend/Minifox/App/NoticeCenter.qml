pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Minifox.Shared

// Version and repository results are shown here instead of on the page that
// started the work: the shell's Loader destroys pages on navigation, so a
// result that arrives later would otherwise be lost.
//
// A result pops up briefly in the corner, lined up with the page content, and
// then stays in the history behind the navigation rail's bell, so nothing
// remains on top of the page. Hovering a pop-up keeps it open.
Item {
    id: root

    required property var appContext
    // Results the visible page already shows inline go to the history only.
    property bool versionPageVisible: false
    // Pop-ups line up with the page content.
    property int contentMargin: Theme.spacingLg
    property int maximumHistory: 50
    property int maximumToasts: 3
    property int nextNoticeId: 0
    readonly property int count: history.count
    property int unreadCount: 0
    readonly property bool historyOpen: historyPopup.opened
    readonly property int toastWidth: Math.min(400, width - 2 * contentMargin)
    readonly property int motion: Theme.motionDuration

    function post(success, message, warning, quiet) {
        const text = String(message || "").trim();
        if (text.length === 0)
            return;
        const notice = { noticeId: root.nextNoticeId++, success: success === true, warning: warning === true, message: text };
        history.insert(0, notice);
        while (history.count > root.maximumHistory)
            history.remove(history.count - 1);
        if (historyPopup.opened)
            return;
        root.unreadCount = Math.min(root.unreadCount + 1, history.count);
        if (quiet === true)
            return;
        toasts.append(notice);
        while (toasts.count > root.maximumToasts)
            toasts.remove(0);
    }

    function removeFrom(model, noticeId) {
        for (let i = 0; i < model.count; ++i) {
            if (model.get(i).noticeId === noticeId) {
                model.remove(i);
                return;
            }
        }
    }

    function dismissToast(noticeId) {
        root.removeFrom(toasts, noticeId);
    }

    function deleteNotice(noticeId) {
        root.removeFrom(toasts, noticeId);
        root.removeFrom(history, noticeId);
        root.unreadCount = Math.min(root.unreadCount, history.count);
        if (history.count === 0)
            historyPopup.close();
    }

    function clearAll() {
        toasts.clear();
        history.clear();
        root.unreadCount = 0;
        historyPopup.close();
    }

    // Opens the history beside the given item (the rail's bell).
    function toggleHistory(anchor) {
        // A press on the bell already closed the history; don't reopen it.
        if (historyPopup.opened || Date.now() - historyPopup.closedAt < 300) {
            historyPopup.close();
            return;
        }
        if (anchor)
            historyPopup.anchorItem = anchor;
        toasts.clear();
        root.unreadCount = 0;
        historyPopup.open();
    }

    function copy(text) {
        copyHelper.text = text;
        copyHelper.selectAll();
        copyHelper.copy();
    }

    // Newest first.
    ListModel {
        id: history
    }

    // Oldest first; the newest pop-up sits nearest the corner.
    ListModel {
        id: toasts
    }

    Connections {
        target: root.appContext.versions

        function onRefreshCompleted(success, message) {
            root.post(success, message, false, !success && root.versionPageVisible);
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

    // Qt Quick has no clipboard API; an invisible editor does the copying.
    TextEdit {
        id: copyHelper
        visible: false
    }

    component Card: Rectangle {
        id: card

        required property int noticeId
        required property bool success
        required property bool warning
        required property string message
        // Pop-ups show a few lines; the history shows everything.
        property bool clamped: false
        readonly property color tone: card.warning ? Theme.warning : card.success ? Theme.success : Theme.error
        readonly property bool pointerInside: hover.hovered
        signal closeRequested()

        implicitHeight: content.implicitHeight + 2 * Theme.spacingMd
        radius: Theme.radius
        color: Qt.rgba(Theme.surfaceRaised.r, Theme.surfaceRaised.g, Theme.surfaceRaised.b, 1)
        border.width: 1
        border.color: Theme.materialStroke

        HoverHandler {
            id: hover
        }

        RowLayout {
            id: content

            anchors.fill: parent
            anchors.margins: Theme.spacingMd
            spacing: Theme.spacingSm

            Rectangle {
                Layout.alignment: Qt.AlignTop
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                radius: 12
                color: Qt.rgba(card.tone.r, card.tone.g, card.tone.b, 0.18)

                AppLabel {
                    anchors.centerIn: parent
                    text: card.warning ? "\uE7BA" : card.success ? "\uE73E" : "\uE711"
                    font.family: Theme.iconFontFamily
                    font.pointSize: Theme.captionSize
                    color: card.tone
                }
            }

            AppLabel {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                text: card.message
                wrapMode: Text.Wrap
                maximumLineCount: card.clamped ? 4 : 1000
                elide: Text.ElideRight
                font.pointSize: Theme.captionSize
                lineHeight: 1.1
            }

            // Copy and close appear while the pointer is over the card.
            Row {
                Layout.alignment: Qt.AlignTop
                spacing: 2
                opacity: card.pointerInside ? 1 : 0

                Behavior on opacity {
                    NumberAnimation { duration: root.motion }
                }

                AppToolButton {
                    width: 26
                    height: 26
                    padding: 0
                    flat: true
                    enabled: parent.opacity > 0
                    text: "\uE8C8"
                    font.family: Theme.iconFontFamily
                    font.pointSize: Theme.captionSize - 1
                    Accessible.name: qsTr("复制")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("复制")
                    onClicked: root.copy(card.message)
                }

                AppToolButton {
                    width: 26
                    height: 26
                    padding: 0
                    flat: true
                    enabled: parent.opacity > 0
                    text: "\uE711"
                    font.family: Theme.iconFontFamily
                    font.pointSize: Theme.captionSize - 1
                    Accessible.name: qsTr("关闭")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("关闭")
                    onClicked: card.closeRequested()
                }
            }
        }
    }

    // ---- Pop-ups, bottom right, anchored to the page area so they line up with
    // the page content.
    Item {
        id: toastLayer

        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: root.contentMargin
        width: root.toastWidth
        height: toastColumn.implicitHeight
        visible: toasts.count > 0 && !historyPopup.opened

        Column {
            id: toastColumn

            width: parent.width
            spacing: Theme.spacingSm

            Repeater {
                model: toasts

                delegate: Card {
                    id: toast

                    width: toastColumn.width
                    clamped: true
                    onCloseRequested: root.dismissToast(toast.noticeId)

                    Rectangle {
                        // A soft shadow without an effect pass.
                        z: -1
                        y: 3
                        width: parent.width
                        height: parent.height
                        radius: parent.radius
                        color: Theme.shadow
                        opacity: 0.5
                    }

                    // Failures and warnings stay longer; hovering pauses both.
                    Timer {
                        running: !toast.pointerInside
                        interval: toast.success && !toast.warning ? 5000 : 10000
                        onTriggered: root.dismissToast(toast.noticeId)
                    }

                    // A click opens the history with the full text.
                    TapHandler {
                        onTapped: root.toggleHistory(null)
                    }
                }
            }
        }
    }

    // ---- History, opened from the rail's bell.
    Popup {
        id: historyPopup

        property Item anchorItem: null
        property double closedAt: 0
        readonly property point anchorPoint: anchorItem
                                             ? anchorItem.mapToItem(root, anchorItem.width, anchorItem.height)
                                             : Qt.point(0, root.height - root.contentMargin)

        x: anchorPoint.x + Theme.spacingSm
        y: Math.max(root.contentMargin, anchorPoint.y - height)
        width: Math.min(420, root.width - x - root.contentMargin)
        height: Math.min(historyColumn.implicitHeight + topPadding + bottomPadding,
                         root.height - 2 * root.contentMargin)
        padding: Theme.spacingMd
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: closedAt = Date.now()

        background: Rectangle {
            radius: Theme.radius
            color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 1)
            border.width: 1
            border.color: Theme.materialStroke
        }

        contentItem: ColumnLayout {
            id: historyColumn

            spacing: Theme.spacingSm

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                AppLabel {
                    Layout.fillWidth: true
                    text: qsTr("通知")
                    font.pointSize: Theme.subtitleSize
                    font.weight: Font.DemiBold
                }

                AppButton {
                    compact: true
                    text: qsTr("全部清除")
                    onClicked: root.clearAll()
                }

                AppToolButton {
                    compact: true
                    flat: true
                    text: "\uE711"
                    font.family: Theme.iconFontFamily
                    Accessible.name: qsTr("关闭")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("关闭")
                    onClicked: historyPopup.close()
                }
            }

            ListView {
                id: historyList

                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: contentHeight
                spacing: Theme.spacingSm
                clip: true
                model: history
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { id: historyBar; policy: ScrollBar.AsNeeded }

                delegate: Card {
                    id: entry

                    width: historyList.width - (historyBar.size < 1 ? historyBar.width + 2 : 0)
                    onCloseRequested: root.deleteNotice(entry.noticeId)
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
