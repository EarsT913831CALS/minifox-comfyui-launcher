pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Minifox.Shared

Pane {
    id: root

    required property var appContext
    readonly property var versions: appContext.versions
    property int selectedTab: 0
    property int coreChannel: 0
    property int branchRepositorySource: String(versions.remoteUrl).toLowerCase().indexOf(
                                             "cnb.cool/indexmirror/comfyui") >= 0 ? 1 : 0
    property bool branchSwitchPending: false
    property string installedSearch: ""
    property string availableSearch: ""
    property string extensionUrl: ""
    property string pendingRemovalPath: ""
    property string pendingExtensionPath: ""
    property string pendingExtensionName: ""
    readonly property bool englishUi: root.appContext.settings.language === "en_US"
                                      || (root.appContext.settings.language === "system"
                                          && Qt.locale().name.toLowerCase().startsWith("en"))
    readonly property real tableFontSize: Theme.captionSize + 0.5
    readonly property int coreActionWidth: 68
    readonly property int statusActionWidth: root.englishUi ? 72 : 60
    readonly property int versionActionWidth: root.englishUi ? 104 : 76
    readonly property int removeActionWidth: root.statusActionWidth
    readonly property int extensionActionsWidth: root.statusActionWidth
                                                 + root.versionActionWidth
                                                 + root.removeActionWidth
    readonly property int availableActionWidth: root.englishUi ? 86 : 74
    property real extensionViewportWidth: 1280
    property real extensionEnabledWidth: 56
    property real extensionNameWidth: 300
    property real extensionBranchWidth: 120
    property real extensionCommitWidth: 100
    property real extensionDateWidth: 190
    readonly property real extensionRemoteWidth: Math.max(
        160,
        root.extensionViewportWidth - root.extensionEnabledWidth
        - root.extensionNameWidth - root.extensionBranchWidth
        - root.extensionCommitWidth - root.extensionDateWidth
        - root.extensionActionsWidth)

    padding: 0

    function matches(text, query) {
        return query.length === 0 || String(text).toLowerCase().indexOf(query.toLowerCase()) >= 0;
    }

    function extensionStatusColor(status) {
        if (status === "outdated") return Theme.error;
        if (status === "checking") return Theme.warning;
        if (status === "latest") return Theme.foreground;
        return Theme.foregroundSecondary;
    }

    function clampColumn(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value));
    }

    function resizeAbsorbingRemote(propertyName, delta, minimum, maximum) {
        const current = root[propertyName];
        const allowedPositive = Math.max(0, root.extensionRemoteWidth - 160);
        root[propertyName] = root.clampColumn(
            current + delta, minimum, Math.min(maximum, current + allowedPositive));
    }

    function resizeColumnPair(leftProperty, rightProperty, delta, leftMinimum, rightMinimum) {
        const total = root[leftProperty] + root[rightProperty];
        const nextLeft = root.clampColumn(root[leftProperty] + delta,
                                          leftMinimum, total - rightMinimum);
        root[leftProperty] = nextLeft;
        root[rightProperty] = total - nextLeft;
    }

    function browserUrl(remote) {
        let value = String(remote || "").trim();
        if (value.length === 0)
            return "";
        if (value.startsWith("git@")) {
            const separator = value.indexOf(":");
            if (separator > 4)
                value = "https://" + value.slice(4, separator) + "/" + value.slice(separator + 1);
        } else if (value.startsWith("ssh://git@")) {
            value = "https://" + value.slice(10);
        } else if (!value.includes("://")) {
            value = "https://" + value;
        }
        if (value.endsWith(".git"))
            value = value.slice(0, -4);
        return value;
    }

    component CtrlRemoteLink: AppLabel {
        id: remoteLink

        property string remoteUrl: ""
        property string fallbackText: "—"
        readonly property string targetUrl: root.browserUrl(remoteUrl)

        text: remoteUrl.length > 0 ? remoteUrl : fallbackText
        color: targetUrl.length > 0 ? Theme.info : Theme.foregroundSecondary
        font.family: root.appContext.settings.consoleFontFamily
        font.underline: targetUrl.length > 0
        elide: Text.ElideRight

        MouseArea {
            id: linkMouse
            anchors.fill: parent
            enabled: remoteLink.targetUrl.length > 0
            acceptedButtons: Qt.LeftButton
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: mouse => {
                if ((mouse.modifiers & Qt.ControlModifier) !== 0)
                    Qt.openUrlExternally(remoteLink.targetUrl);
            }
        }

        ToolTip.visible: linkMouse.containsMouse
        ToolTip.text: qsTr("按住 Ctrl 并左键点击，在默认浏览器中打开")
    }

    component ExtensionHeaderCell: Item {
        id: headerCell

        property string label: ""
        property bool adjustable: true
        signal resizeRequested(real delta)

        AppLabel {
            anchors.fill: parent
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: headerCell.label === qsTr("启用")
                                 ? Text.AlignHCenter : Text.AlignLeft
            leftPadding: headerCell.label === qsTr("启用") ? 0 : 8
            rightPadding: headerCell.adjustable ? 8 : 0
            text: headerCell.label
            font.pointSize: root.tableFontSize
            elide: Text.ElideRight
        }

        Rectangle {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: 1
            height: parent.height - 10
            color: Theme.materialStroke
            visible: headerCell.adjustable
        }

        MouseArea {
            id: resizeArea
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 10
            enabled: headerCell.adjustable
            cursorShape: Qt.SplitHCursor
            preventStealing: true
            property real previousX: 0

            onPressed: mouse => previousX = mapToItem(root, mouse.x, mouse.y).x
            onPositionChanged: mouse => {
                if (!pressed) return;
                const currentX = mapToItem(root, mouse.x, mouse.y).x;
                headerCell.resizeRequested(currentX - previousX);
                previousX = currentX;
            }
        }
    }

    Connections {
        target: root.versions

        function onRefreshCompleted(success, message) {
            refreshNotice.success = success;
            refreshNotice.message = message;
            refreshNotice.open();
        }

        function onOperationCompleted(success, message) {
            refreshNotice.success = success;
            refreshNotice.message = message;
            refreshNotice.open();
            if (root.branchSwitchPending) {
                if (success) root.coreChannel = 0;
                root.branchSwitchPending = false;
            }
            if (success && root.selectedTab === 2) root.extensionUrl = "";
        }

        function onDependencyInstallCompleted(success, message) {
            refreshNotice.success = success;
            refreshNotice.message = message;
            refreshNotice.open();
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            color: Theme.materialFillStrong

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingLg
                anchors.rightMargin: Theme.spacingLg
                spacing: Theme.spacingLg

                Repeater {
                    model: [
                        { title: qsTr("内核"), icon: "\uE81C" },
                        { title: qsTr("扩展"), icon: "\uE71D" },
                        { title: qsTr("安装新扩展"), icon: "\uE7BF" }
                    ]

                    delegate: Item {
                        id: tabDelegate
                        required property int index
                        required property var modelData
                        Layout.preferredWidth: tabRow.implicitWidth
                        Layout.fillHeight: true

                        Row {
                            id: tabRow
                            anchors.centerIn: parent
                            spacing: Theme.spacingSm

                            AppLabel {
                                text: tabDelegate.modelData.icon
                                font.family: Theme.iconFontFamily
                                font.pointSize: Theme.bodySize
                                color: root.selectedTab === tabDelegate.index ? Theme.foreground : Theme.foregroundSecondary
                            }

                            AppLabel {
                                text: tabDelegate.modelData.title
                                font.pointSize: Theme.bodySize
                                color: root.selectedTab === tabDelegate.index ? Theme.foreground : Theme.foregroundSecondary
                            }
                        }

                        Rectangle {
                            anchors.bottom: parent.bottom
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: Math.max(24, tabRow.width * 0.28)
                            height: 4
                            radius: 2
                            color: Theme.accent
                            visible: root.selectedTab === tabDelegate.index
                        }

                        TapHandler {
                            onTapped: root.selectedTab = tabDelegate.index
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                AppButton {
                    text: root.versions.busy && !root.versions.updating
                          ? qsTr("刷新中…") : qsTr("↻  刷新列表")
                    enabled: !root.versions.busy
                    onClicked: {
                        if (root.selectedTab === 0) root.versions.refreshCore();
                        else if (root.selectedTab === 1) root.versions.refreshInstalledExtensions();
                        else root.versions.refreshAvailableExtensions();
                    }
                }

                AppButton {
                    text: root.versions.updating ? qsTr("更新中…") : qsTr("▣  一键更新")
                    visible: root.selectedTab !== 2
                    enabled: root.selectedTab === 0
                             ? root.versions.canCheck
                             : !root.versions.busy && root.versions.installedExtensions.length > 0
                    onClicked: {
                        if (root.selectedTab === 0) root.versions.updateComfyUi(root.coreChannel);
                        else root.versions.updateAllExtensions();
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.materialStroke
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.selectedTab

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingLg
                    spacing: Theme.spacingMd

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingLg

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: Theme.spacingLg
                            rowSpacing: Theme.spacingMd

                            AppLabel { text: qsTr("远端地址："); color: Theme.foregroundSecondary }
                            CtrlRemoteLink {
                                remoteUrl: root.versions.remoteUrl
                                Layout.fillWidth: true
                            }
                            AppLabel { text: qsTr("当前分支："); color: Theme.foregroundSecondary }
                            AppLabel {
                                text: root.versions.branch.length > 0 ? root.versions.branch : "—"
                                font.family: root.appContext.settings.consoleFontFamily
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            AppLabel { text: qsTr("当前版本："); color: Theme.foregroundSecondary }
                            AppLabel {
                                text: root.versions.commitFull.length > 0
                                      ? root.versions.commitFull + (root.versions.commitDate.length > 0 ? "  (" + root.versions.commitDate + ")" : "")
                                      : "—"
                                font.family: root.appContext.settings.consoleFontFamily
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                        }

                        ColumnLayout {
                            Layout.preferredWidth: Math.min(420, Math.max(320, root.width * 0.3))
                            spacing: Theme.spacingMd

                            AppComboBox {
                                Layout.fillWidth: true
                                model: [
                                    qsTr("Comfy-Org/ComfyUI · master（官方）"),
                                    qsTr("IndexMirror/ComfyUI · master（国内镜像）")
                                ]
                                currentIndex: root.branchRepositorySource
                                onActivated: index => root.branchRepositorySource = index
                            }

                            AppButton {
                                text: qsTr("⚯  切换分支")
                                enabled: root.versions.canCheck
                                Layout.alignment: Qt.AlignRight
                                Layout.preferredHeight: 34
                                leftPadding: 12
                                rightPadding: 12
                                font.pointSize: root.tableFontSize
                                onClicked: {
                                    root.branchSwitchPending = true;
                                    root.versions.switchBranch(
                                        "master", root.branchRepositorySource);
                                }
                            }
                        }
                    }

                    Row {
                        spacing: 0

                        Repeater {
                            model: [qsTr("稳定版"), qsTr("开发版")]
                            delegate: Rectangle {
                                id: channelTab
                                required property int index
                                required property string modelData
                                width: 180
                                height: 42
                                color: root.coreChannel === index ? Theme.surfaceRaised : Theme.surfaceSubtle
                                border.width: 1
                                border.color: Theme.materialStroke
                                radius: 5

                                AppLabel {
                                    anchors.centerIn: parent
                                    text: channelTab.modelData
                                    font.pointSize: Theme.bodySize
                                }

                                TapHandler { onTapped: root.coreChannel = channelTab.index }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: Theme.surfaceRaised
                        border.width: 1
                        border.color: Theme.materialStroke
                        radius: Theme.controlRadius
                        clip: true

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 0

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 36
                                color: Theme.surfaceSubtle

                                Row {
                                    anchors.fill: parent
                                    AppLabel { width: 110; height: parent.height; verticalAlignment: Text.AlignVCenter; leftPadding: 10; text: qsTr("版本 ID"); font.pointSize: root.tableFontSize }
                                    AppLabel { width: parent.width - 110 - 220 - 64 - root.coreActionWidth; height: parent.height; verticalAlignment: Text.AlignVCenter; leftPadding: 10; text: qsTr("更新内容"); font.pointSize: root.tableFontSize }
                                    AppLabel { width: 220; height: parent.height; verticalAlignment: Text.AlignVCenter; leftPadding: 10; text: qsTr("日期"); font.pointSize: root.tableFontSize }
                                    AppLabel { width: 64; height: parent.height; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; text: qsTr("当前"); font.pointSize: root.tableFontSize }
                                }
                            }

                            ListView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                model: root.coreChannel === 0
                                       ? root.versions.stableVersions
                                       : root.versions.coreVersions
                                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                                delegate: Rectangle {
                                    id: coreRow
                                    required property int index
                                    required property var modelData
                                    width: ListView.view.width
                                    height: 42
                                    color: index % 2 === 0 ? Theme.materialFill : Theme.surfaceRaised
                                    border.width: 1
                                    border.color: Theme.materialStroke

                                    Row {
                                        anchors.fill: parent
                                        AppLabel { width: 110; height: parent.height; verticalAlignment: Text.AlignVCenter; leftPadding: 10; text: coreRow.modelData.shortCommit; color: Theme.info; font.family: root.appContext.settings.consoleFontFamily; font.pointSize: root.tableFontSize }
                                        AppLabel { width: parent.width - 110 - 220 - 64 - root.coreActionWidth; height: parent.height; verticalAlignment: Text.AlignVCenter; leftPadding: 10; rightPadding: 8; text: coreRow.modelData.subject; elide: Text.ElideRight; font.pointSize: root.tableFontSize }
                                        AppLabel { width: 220; height: parent.height; verticalAlignment: Text.AlignVCenter; leftPadding: 10; text: coreRow.modelData.date; font.family: root.appContext.settings.consoleFontFamily; font.pointSize: root.tableFontSize }
                                        AppLabel { width: 64; height: parent.height; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; text: coreRow.modelData.current ? "✓" : ""; font.pointSize: Theme.bodySize }
                                        AppButton { width: root.coreActionWidth; height: parent.height; leftPadding: 8; rightPadding: 8; text: qsTr("切换"); enabled: !coreRow.modelData.current && root.versions.canCheck; font.pointSize: root.tableFontSize; onClicked: root.versions.switchCoreVersion(coreRow.modelData.commit, root.coreChannel) }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingLg
                    spacing: Theme.spacingMd

                    AppTextField {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        placeholderText: qsTr("搜索已安装插件…")
                        onTextChanged: root.installedSearch = text
                    }

                    Rectangle {
                        id: installedTable
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: Theme.surfaceRaised
                        border.width: 1
                        border.color: Theme.materialStroke
                        radius: Theme.controlRadius
                        clip: true
                        Component.onCompleted: root.extensionViewportWidth = width
                        onWidthChanged: root.extensionViewportWidth = width

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 0

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 36
                                color: Theme.surfaceSubtle
                                Row {
                                    anchors.fill: parent
                                    ExtensionHeaderCell {
                                        width: root.extensionEnabledWidth
                                        height: parent.height
                                        label: qsTr("启用")
                                        onResizeRequested: delta => root.resizeAbsorbingRemote(
                                            "extensionEnabledWidth", delta, 46, 100)
                                    }
                                    ExtensionHeaderCell {
                                        width: root.extensionNameWidth
                                        height: parent.height
                                        label: qsTr("插件名")
                                        onResizeRequested: delta => root.resizeAbsorbingRemote(
                                            "extensionNameWidth", delta, 150, 520)
                                    }
                                    ExtensionHeaderCell {
                                        width: root.extensionRemoteWidth
                                        height: parent.height
                                        label: qsTr("远端地址")
                                        onResizeRequested: delta => root.extensionBranchWidth = root.clampColumn(
                                            root.extensionBranchWidth - delta, 80, 260)
                                    }
                                    ExtensionHeaderCell {
                                        width: root.extensionBranchWidth
                                        height: parent.height
                                        label: qsTr("当前分支")
                                        onResizeRequested: delta => root.resizeColumnPair(
                                            "extensionBranchWidth", "extensionCommitWidth", delta, 80, 72)
                                    }
                                    ExtensionHeaderCell {
                                        width: root.extensionCommitWidth
                                        height: parent.height
                                        label: qsTr("版本 ID")
                                        onResizeRequested: delta => root.resizeColumnPair(
                                            "extensionCommitWidth", "extensionDateWidth", delta, 72, 130)
                                    }
                                    ExtensionHeaderCell {
                                        width: root.extensionDateWidth
                                        height: parent.height
                                        label: qsTr("更新日期")
                                        onResizeRequested: delta => root.resizeAbsorbingRemote(
                                            "extensionDateWidth", delta, 130, 300)
                                    }
                                }
                            }

                            ListView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                model: root.versions.installedExtensions
                                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                                delegate: Rectangle {
                                    id: extensionRow
                                    required property int index
                                    required property var modelData
                                    readonly property bool matched: root.matches(modelData.name + " " + modelData.remote, root.installedSearch)
                                    readonly property color statusColor: root.extensionStatusColor(modelData.status)
                                    width: ListView.view.width
                                    height: matched ? 40 : 0
                                    visible: matched
                                    color: index % 2 === 0 ? Theme.materialFill : Theme.surfaceRaised
                                    border.width: 1
                                    border.color: Theme.materialStroke
                                    clip: true

                                    Row {
                                        anchors.fill: parent
                                        CheckBox { width: root.extensionEnabledWidth; height: parent.height; checked: extensionRow.modelData.enabled; onToggled: root.versions.setExtensionEnabled(extensionRow.modelData.path, checked) }
                                        AppLabel { width: root.extensionNameWidth; height: parent.height; verticalAlignment: Text.AlignVCenter; leftPadding: 8; text: extensionRow.modelData.name; font.family: root.appContext.settings.consoleFontFamily; font.pointSize: root.tableFontSize; elide: Text.ElideRight }
                                        CtrlRemoteLink { width: root.extensionRemoteWidth; height: parent.height; verticalAlignment: Text.AlignVCenter; leftPadding: 8; remoteUrl: extensionRow.modelData.remote || ""; font.pointSize: root.tableFontSize }
                                        AppLabel { width: root.extensionBranchWidth; height: parent.height; verticalAlignment: Text.AlignVCenter; leftPadding: 8; text: extensionRow.modelData.branch || "—"; font.family: root.appContext.settings.consoleFontFamily; font.pointSize: root.tableFontSize; elide: Text.ElideRight }
                                        AppLabel { width: root.extensionCommitWidth; height: parent.height; verticalAlignment: Text.AlignVCenter; leftPadding: 8; text: extensionRow.modelData.commit || "—"; color: extensionRow.statusColor; font.family: root.appContext.settings.consoleFontFamily; font.pointSize: root.tableFontSize }
                                        AppLabel { width: root.extensionDateWidth; height: parent.height; verticalAlignment: Text.AlignVCenter; leftPadding: 8; text: extensionRow.modelData.date; color: extensionRow.statusColor; font.family: root.appContext.settings.consoleFontFamily; font.pointSize: root.tableFontSize }
                                        AppButton {
                                            width: root.statusActionWidth
                                            height: parent.height
                                            leftPadding: 6
                                            rightPadding: 6
                                            text: extensionRow.modelData.status === "checking" ? qsTr("检测中")
                                                  : extensionRow.modelData.status === "latest" ? qsTr("最新")
                                                  : qsTr("更新")
                                            enabled: extensionRow.modelData.repository
                                                     && extensionRow.modelData.status === "outdated"
                                                     && !root.versions.busy
                                            font.pointSize: root.tableFontSize
                                            onClicked: root.versions.updateExtension(extensionRow.modelData.path)
                                        }
                                        AppButton {
                                            width: root.versionActionWidth
                                            height: parent.height
                                            leftPadding: 6
                                            rightPadding: 6
                                            text: qsTr("切换版本")
                                            enabled: extensionRow.modelData.repository && !root.versions.busy
                                            font.pointSize: root.tableFontSize
                                            onClicked: {
                                                root.pendingExtensionPath = extensionRow.modelData.path;
                                                root.pendingExtensionName = extensionRow.modelData.name;
                                                root.versions.loadExtensionVersions(
                                                    extensionRow.modelData.path,
                                                    extensionRow.modelData.commit);
                                                extensionVersionDialog.open();
                                            }
                                        }
                                        AppButton { width: root.removeActionWidth; height: parent.height; leftPadding: 3; rightPadding: 3; text: qsTr("卸载"); destructive: true; font.pointSize: root.tableFontSize; onClicked: { root.pendingRemovalPath = extensionRow.modelData.path; removeDialog.open(); } }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingLg
                    spacing: Theme.spacingMd

                    AppTextField {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        placeholderText: qsTr("搜索新插件…")
                        onTextChanged: root.availableSearch = text
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: Theme.surfaceRaised
                        border.width: 1
                        border.color: Theme.materialStroke
                        radius: Theme.controlRadius
                        clip: true

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 0

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 36
                                color: Theme.surfaceSubtle
                                Row {
                                    anchors.fill: parent
                                    AppLabel { width: 300; height: parent.height; verticalAlignment: Text.AlignVCenter; leftPadding: 10; text: qsTr("插件名"); font.pointSize: root.tableFontSize }
                                    AppLabel { width: parent.width - 300 - root.availableActionWidth; height: parent.height; verticalAlignment: Text.AlignVCenter; leftPadding: 10; text: qsTr("简介"); font.pointSize: root.tableFontSize }
                                }
                            }

                            ListView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                model: root.versions.availableExtensions
                                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                                delegate: Rectangle {
                                    id: availableRow
                                    required property int index
                                    required property var modelData
                                    readonly property bool matched: root.matches(modelData.name + " " + modelData.description, root.availableSearch)
                                    width: ListView.view.width
                                    height: matched ? Math.max(48, descriptionLabel.implicitHeight + 14) : 0
                                    visible: matched
                                    color: index % 2 === 0 ? Theme.materialFill : Theme.surfaceRaised
                                    border.width: 1
                                    border.color: Theme.materialStroke
                                    clip: true

                                    Row {
                                        anchors.fill: parent
                                        CtrlRemoteLink { width: 300; height: parent.height; verticalAlignment: Text.AlignVCenter; leftPadding: 10; rightPadding: 8; text: availableRow.modelData.name; remoteUrl: availableRow.modelData.remote || ""; font.pointSize: root.tableFontSize; wrapMode: Text.WordWrap }
                                        AppLabel { id: descriptionLabel; width: parent.width - 300 - root.availableActionWidth; height: parent.height; verticalAlignment: Text.AlignVCenter; leftPadding: 10; rightPadding: 10; text: availableRow.modelData.description; font.pointSize: root.tableFontSize; wrapMode: Text.WordWrap }
                                        AppButton { width: root.availableActionWidth; height: parent.height; leftPadding: 5; rightPadding: 5; text: availableRow.modelData.installed ? qsTr("已装") : qsTr("安装"); enabled: !availableRow.modelData.installed && !root.versions.busy; font.pointSize: root.tableFontSize; onClicked: root.versions.installExtension(availableRow.modelData.remote) }
                                    }
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingMd

                        AppTextField {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 46
                            placeholderText: qsTr("扩展 URL")
                            text: root.extensionUrl
                            onTextChanged: root.extensionUrl = text
                        }

                        AppButton {
                            text: qsTr("安装")
                            enabled: root.extensionUrl.trim().length > 0 && !root.versions.busy
                            Layout.preferredHeight: 34
                            Layout.preferredWidth: root.availableActionWidth
                            leftPadding: 6
                            rightPadding: 6
                            font.pointSize: root.tableFontSize
                            onClicked: root.versions.installExtension(root.extensionUrl)
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: refreshNotice

        property bool success: true
        property string message: ""

        parent: Overlay.overlay
        x: Math.max(24, Overlay.overlay.width - width - 24)
        y: Math.max(24, Overlay.overlay.height - height - 24)
        width: Math.min(440, Overlay.overlay.width - 48)
        padding: 12
        modal: false
        closePolicy: Popup.NoAutoClose
        onOpened: refreshNoticeTimer.restart()

        background: Rectangle {
            color: Theme.surfaceRaised
            border.width: 1
            border.color: refreshNotice.success ? Theme.success : Theme.error
            radius: Theme.controlRadius
        }

        contentItem: RowLayout {
            spacing: Theme.spacingSm

            AppLabel {
                text: refreshNotice.success ? "✓" : "×"
                color: refreshNotice.success ? Theme.success : Theme.error
                font.pointSize: Theme.subtitleSize
                font.bold: true
            }

            AppLabel {
                Layout.fillWidth: true
                text: refreshNotice.message
                wrapMode: Text.WordWrap
                color: Theme.foreground
                font.pointSize: Theme.captionSize
            }
        }

        Timer {
            id: refreshNoticeTimer
            interval: 5000
            onTriggered: refreshNotice.close()
        }
    }

    AppDialog {
        id: extensionVersionDialog
        anchors.centerIn: Overlay.overlay
        width: Math.min(980, root.width - Theme.spacingXl * 2)
        height: Math.min(640, root.height - Theme.spacingXl * 2)
        modal: true
        title: qsTr("版本切换 · %1").arg(root.pendingExtensionName)
        standardButtons: Dialog.Cancel
        rejectText: qsTr("关闭")

        contentItem: ColumnLayout {
            spacing: Theme.spacingSm

            AppLabel {
                Layout.fillWidth: true
                text: root.versions.busy
                      ? qsTr("正在读取版本历史…")
                      : root.versions.extensionVersions.length > 0
                        ? qsTr("选择要切换到的版本。切换前会检查扩展目录中的未提交更改。")
                        : root.versions.lastError.length > 0
                          ? root.versions.lastError
                          : qsTr("没有可显示的版本历史。")
                color: Theme.foregroundSecondary
                wrapMode: Text.WordWrap
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.surfaceRaised
                border.width: 1
                border.color: Theme.materialStroke
                radius: Theme.controlRadius
                clip: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 38
                        color: Theme.surfaceSubtle

                        Row {
                            anchors.fill: parent
                            AppLabel { width: 110; height: parent.height; verticalAlignment: Text.AlignVCenter; leftPadding: 10; text: qsTr("版本 ID"); font.pointSize: root.tableFontSize }
                            AppLabel { width: parent.width - 110 - 220 - 64 - 84; height: parent.height; verticalAlignment: Text.AlignVCenter; leftPadding: 10; text: qsTr("更新内容"); font.pointSize: root.tableFontSize }
                            AppLabel { width: 220; height: parent.height; verticalAlignment: Text.AlignVCenter; leftPadding: 10; text: qsTr("日期"); font.pointSize: root.tableFontSize }
                            AppLabel { width: 64; height: parent.height; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; text: qsTr("当前"); font.pointSize: root.tableFontSize }
                        }
                    }

                    ListView {
                        id: extensionVersionList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: root.versions.extensionVersions
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                        delegate: Rectangle {
                            id: versionRow
                            required property int index
                            required property var modelData
                            width: ListView.view.width
                            height: 42
                            color: index % 2 === 0 ? Theme.materialFill : Theme.surfaceRaised
                            border.width: 1
                            border.color: Theme.materialStroke

                            Row {
                                anchors.fill: parent
                                AppLabel { width: 110; height: parent.height; verticalAlignment: Text.AlignVCenter; leftPadding: 10; text: versionRow.modelData.shortCommit; color: Theme.info; font.family: root.appContext.settings.consoleFontFamily; font.pointSize: root.tableFontSize }
                                AppLabel { width: parent.width - 110 - 220 - 64 - 84; height: parent.height; verticalAlignment: Text.AlignVCenter; leftPadding: 10; rightPadding: 8; text: versionRow.modelData.subject; elide: Text.ElideRight; font.pointSize: root.tableFontSize }
                                AppLabel { width: 220; height: parent.height; verticalAlignment: Text.AlignVCenter; leftPadding: 10; text: versionRow.modelData.date; font.family: root.appContext.settings.consoleFontFamily; font.pointSize: root.tableFontSize }
                                AppLabel { width: 64; height: parent.height; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; text: versionRow.modelData.current ? "✓" : ""; color: Theme.success; font.pointSize: Theme.subtitleSize }
                                AppButton {
                                    width: 84
                                    height: parent.height
                                    leftPadding: 6
                                    rightPadding: 6
                                    text: versionRow.modelData.current ? qsTr("当前") : qsTr("切换")
                                    enabled: !versionRow.modelData.current && !root.versions.busy
                                    font.pointSize: root.tableFontSize
                                    onClicked: {
                                        extensionVersionDialog.close();
                                        root.versions.switchExtensionVersion(
                                            root.pendingExtensionPath,
                                            versionRow.modelData.commit);
                                    }
                                }
                            }
                        }
                    }
                }

                BusyIndicator {
                    anchors.centerIn: parent
                    running: root.versions.busy
                    visible: running && root.versions.extensionVersions.length === 0
                }
            }
        }
    }

    AppDialog {
        id: removeDialog
        anchors.centerIn: Overlay.overlay
        width: Math.min(480, root.width - Theme.spacingXl * 2)
        modal: true
        title: qsTr("卸载扩展")
        standardButtons: Dialog.Yes | Dialog.No
        acceptText: qsTr("卸载")
        rejectText: qsTr("取消")
        destructiveAccept: true
        onAccepted: root.versions.removeExtension(root.pendingRemovalPath)

        AppLabel {
            width: removeDialog.availableWidth
            text: qsTr("将永久删除这个扩展目录。确定继续吗？")
            wrapMode: Text.WordWrap
        }
    }
}
