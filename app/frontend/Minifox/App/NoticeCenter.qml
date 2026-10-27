pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Minifox.Shared

// Version and repository results are shown here instead of on the page that
// started the work: the shell's Loader destroys pages on navigation, so a
// result that arrives later would otherwise be lost. Notices stack rather than
// replace each other; failures and warnings stay until dismissed, successes
// close by themselves.
Item {
    id: root

    required property var appContext
    property int maximumNotices: 5
    property int nextNoticeId: 0

    function post(success, message, warning) {
        const text = String(message || "").trim();
        if (text.length === 0)
            return;
        notices.append({ noticeId: root.nextNoticeId++, success: success, warning: warning === true, message: text });
        while (notices.count > root.maximumNotices) {
            let oldest = 0;
            for (let i = 0; i < notices.count; ++i) {
                if (notices.get(i).success && !notices.get(i).warning) {
                    oldest = i;
                    break;
                }
            }
            notices.remove(oldest);
        }
    }

    function dismiss(noticeId) {
        for (let i = 0; i < notices.count; ++i) {
            if (notices.get(i).noticeId === noticeId) {
                notices.remove(i);
                return;
            }
        }
    }

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

        function onExtensionVersionsLoaded(success, message) {
            if (!success)
                root.post(false, message);
        }
    }

    Popup {
        id: popup

        parent: Overlay.overlay
        visible: notices.count > 0
        x: Math.max(24, Overlay.overlay.width - width - 24)
        y: Math.max(24, Overlay.overlay.height - height - 24)
        width: Math.min(440, Overlay.overlay.width - 48)
        height: Math.min(noticeList.contentHeight, Overlay.overlay.height - 48)
        padding: 0
        modal: false
        focus: false
        closePolicy: Popup.NoAutoClose
        background: null

        contentItem: ListView {
            id: noticeList

            clip: true
            spacing: Theme.spacingSm
            interactive: contentHeight > height
            verticalLayoutDirection: ListView.BottomToTop
            model: notices
            ScrollBar.vertical: ScrollBar {
                policy: noticeList.interactive ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            }

            delegate: Rectangle {
                id: notice

                required property int noticeId
                required property bool success
                required property bool warning
                required property string message
                readonly property color tone: notice.warning ? Theme.warning
                    : notice.success ? Theme.success : Theme.error

                width: ListView.view.width
                height: noticeRow.implicitHeight + 24
                color: Theme.surfaceRaised
                border.width: 1
                border.color: notice.tone
                radius: Theme.controlRadius

                Timer {
                    running: notice.success && !notice.warning
                    interval: 5000
                    onTriggered: root.dismiss(notice.noticeId)
                }

                RowLayout {
                    id: noticeRow

                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: Theme.spacingSm

                    AppLabel {
                        Layout.alignment: Qt.AlignTop
                        text: notice.warning ? "!" : notice.success ? "✓" : "×"
                        color: notice.tone
                        font.pointSize: Theme.subtitleSize
                        font.bold: true
                    }

                    // Long git or pip errors are capped in height and scroll, and
                    // the text stays selectable so it can be copied for reports.
                    Flickable {
                        id: messageView

                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        Layout.preferredHeight: Math.min(messageText.implicitHeight, 160)
                        contentWidth: width
                        contentHeight: messageText.implicitHeight
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        interactive: contentHeight > height
                        ScrollBar.vertical: ScrollBar {
                            policy: messageView.interactive ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                        }

                        TextEdit {
                            id: messageText

                            width: messageView.width
                            text: notice.message
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            color: Theme.foreground
                            selectionColor: Theme.accent
                            selectedTextColor: Theme.accentForeground
                            font.family: Theme.uiFontFamily
                            font.pointSize: Theme.captionSize
                        }
                    }

                    ColumnLayout {
                        Layout.alignment: Qt.AlignTop
                        spacing: Theme.spacingXs

                        AppButton {
                            Layout.fillWidth: true
                            leftPadding: 8
                            rightPadding: 8
                            text: qsTr("复制")
                            font.pointSize: Theme.captionSize
                            onClicked: {
                                messageText.selectAll();
                                messageText.copy();
                                messageText.deselect();
                            }
                        }

                        AppButton {
                            Layout.fillWidth: true
                            leftPadding: 8
                            rightPadding: 8
                            text: qsTr("关闭")
                            font.pointSize: Theme.captionSize
                            onClicked: root.dismiss(notice.noticeId)
                        }
                    }
                }
            }
        }
    }
}
