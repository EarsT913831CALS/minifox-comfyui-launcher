pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Minifox.Shared

Pane {
    id: page

    required property var appContext
    readonly property var versions: appContext.versions
    readonly property string monoFont: appContext.settings.consoleFontFamily
    readonly property real cellFont: Theme.captionSize + 0.5
    readonly property bool english: appContext.settings.language === "en_US"
                                    || (appContext.settings.language === "system"
                                        && Qt.locale().name.toLowerCase().startsWith("en"))

    property int tab: 0
    property int channel: 0
    property int coreSource: String(versions.remoteUrl).toLowerCase().indexOf("cnb.cool/indexmirror/comfyui") >= 0 ? 1 : 0
    property string installUrl: ""

    // The extension the version or removal dialog is about.
    property string dialogPath: ""
    property string dialogName: ""
    property int dialogChanges: 0

    // Column widths of the installed-extension table; the remote column takes the rest.
    readonly property int switchWidth: 68
    readonly property int statusWidth: english ? 72 : 60
    readonly property int versionWidth: english ? 104 : 76
    readonly property int installWidth: english ? 86 : 74
    readonly property real pad: 16
    property real tableWidth: 1280
    property real enabledWidth: 56
    property real nameWidth: 300
    property real branchWidth: 120
    property real commitWidth: 100
    property real dateWidth: 190
    readonly property real actionsWidth: statusWidth * 2 + versionWidth
    readonly property real remoteWidth: Math.max(160, tableWidth - enabledWidth - nameWidth - branchWidth
                                                 - commitWidth - dateWidth - actionsWidth)

    padding: 0

    onVisibleChanged: if (visible) versions.ensureLoaded(tab)
    onTabChanged: if (visible) versions.ensureLoaded(tab)
    Component.onCompleted: if (visible) versions.ensureLoaded(tab)

    function clamp(value, low, high) {
        return Math.max(low, Math.min(high, value));
    }

    // Widening a column takes the room from the remote column, down to its minimum.
    function widen(name, delta, low, high) {
        const spare = Math.max(0, page.remoteWidth - 160);
        page[name] = page.clamp(page[name] + delta, low, Math.min(high, page[name] + spare));
    }

    function trade(left, right, delta, leftLow, rightLow) {
        const total = page[left] + page[right];
        page[left] = page.clamp(page[left] + delta, leftLow, total - rightLow);
        page[right] = total - page[left];
    }

    function statusColor(status) {
        if (status === "outdated" || status === "error") return Theme.error;
        if (status === "checking" || status === "updating") return Theme.warning;
        if (status === "latest") return Theme.foreground;
        return Theme.foregroundSecondary;
    }

    function statusText(status) {
        switch (status) {
        case "checking": return qsTr("检测中");
        case "updating": return qsTr("更新中");
        case "outdated": return qsTr("更新");
        case "latest": return qsTr("最新");
        case "error": return qsTr("重试");
        default: return "—";
        }
    }

    function openVersions(entry) {
        page.dialogPath = entry.path;
        page.dialogName = entry.name;
        page.dialogChanges = entry.changes || 0;
        page.versions.loadExtensionVersions(entry.path);
        versionDialog.open();
    }

    component RemoteLink: AppLabel {
        id: link

        property string remote: ""
        readonly property string target: page.appContext.skins.repositoryBrowserUrl(remote)

        text: remote.length > 0 ? remote : "—"
        color: target.length > 0 ? Theme.info : Theme.foregroundSecondary
        font.family: page.monoFont
        font.underline: target.length > 0
        elide: Text.ElideRight

        MouseArea {
            id: linkArea
            anchors.fill: parent
            enabled: link.target.length > 0
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: mouse => {
                if (mouse.modifiers & Qt.ControlModifier)
                    page.appContext.skins.openExternalLink(link.target);
            }
        }

        ToolTip.visible: linkArea.containsMouse
        ToolTip.text: qsTr("按住 Ctrl 并左键点击，在默认浏览器中打开")
    }

    component HeaderCell: Item {
        id: cell

        property string label
        property bool centered: false
        property bool resizable: true
        signal dragged(real delta)

        AppLabel {
            anchors.fill: parent
            leftPadding: cell.centered ? 0 : page.pad
            rightPadding: cell.centered ? 0 : page.pad
            horizontalAlignment: cell.centered ? Text.AlignHCenter : Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
            text: cell.label
            font.pointSize: page.cellFont
            elide: Text.ElideRight
        }

        Rectangle {
            visible: cell.resizable
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: 1
            height: parent.height - 10
            color: Theme.materialStroke
        }

        MouseArea {
            enabled: cell.resizable
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 10
            cursorShape: Qt.SplitHCursor
            preventStealing: true
            property real lastX: 0
            onPressed: mouse => lastX = mapToItem(page, mouse.x, mouse.y).x
            onPositionChanged: mouse => {
                const x = mapToItem(page, mouse.x, mouse.y).x;
                cell.dragged(x - lastX);
                lastX = x;
            }
        }
    }

    // A table body with its header row, frame and an empty/loading hint.
    component Table: Rectangle {
        id: table

        property alias header: headerRow.data
        property alias model: list.model
        property alias delegate: list.delegate
        property alias reuseItems: list.reuseItems
        property bool loading: false
        property string emptyText: ""

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
                    id: headerRow
                    anchors.fill: parent
                }
            }

            ListView {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            }
        }

        BusyIndicator {
            anchors.centerIn: parent
            running: table.loading && list.count === 0
            visible: running
        }

        AppLabel {
            anchors.centerIn: parent
            visible: !table.loading && list.count === 0 && table.emptyText.length > 0
            text: table.emptyText
            color: Theme.foregroundSecondary
        }
    }

    component Cell: AppLabel {
        height: parent ? parent.height : 0
        leftPadding: 10
        rightPadding: 8
        verticalAlignment: Text.AlignVCenter
        font.pointSize: page.cellFont
        elide: Text.ElideRight
    }

    component CommitRow: Rectangle {
        id: commitRow

        required property int index
        required property var modelData
        property int buttonWidth: page.switchWidth
        property bool canSwitch: true
        signal switchRequested(string commit)

        width: ListView.view.width
        height: 42
        color: index % 2 === 0 ? Theme.materialFill : Theme.surfaceRaised
        border.width: 1
        border.color: Theme.materialStroke

        Row {
            anchors.fill: parent
            Cell { width: 110; text: commitRow.modelData.shortCommit; color: Theme.info; font.family: page.monoFont }
            Cell { width: parent.width - 110 - 220 - 64 - commitRow.buttonWidth; text: commitRow.modelData.subject }
            Cell { width: 220; text: commitRow.modelData.date; font.family: page.monoFont }
            Cell {
                width: 64
                leftPadding: 0
                rightPadding: 0
                horizontalAlignment: Text.AlignHCenter
                text: commitRow.modelData.current ? "✓" : ""
                color: Theme.success
                font.pointSize: Theme.bodySize
            }
            AppButton {
                width: commitRow.buttonWidth
                height: parent.height
                leftPadding: 6
                rightPadding: 6
                text: commitRow.modelData.current ? qsTr("当前") : qsTr("切换")
                enabled: !commitRow.modelData.current && commitRow.canSwitch
                font.pointSize: page.cellFont
                onClicked: commitRow.switchRequested(commitRow.modelData.commit)
            }
        }
    }

    component CommitHeader: Row {
        property int buttonWidth: page.switchWidth
        anchors.fill: parent
        Cell { width: 110; text: qsTr("版本 ID") }
        Cell { width: parent.width - 110 - 220 - 64 - parent.buttonWidth; text: qsTr("更新内容") }
        Cell { width: 220; text: qsTr("日期") }
        Cell { width: 64; leftPadding: 0; rightPadding: 0; horizontalAlignment: Text.AlignHCenter; text: qsTr("当前") }
    }

    Connections {
        target: page.versions

        function onOperationCompleted(success, message) {
            if (success && page.tab === 2) page.installUrl = "";
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
                        { title: qsTr("内核"), icon: "" },
                        { title: qsTr("扩展"), icon: "" },
                        { title: qsTr("安装新扩展"), icon: "" }
                    ]

                    delegate: Item {
                        id: tabItem
                        required property int index
                        required property var modelData
                        readonly property bool current: page.tab === index
                        Layout.preferredWidth: tabContent.implicitWidth
                        Layout.fillHeight: true

                        Row {
                            id: tabContent
                            anchors.centerIn: parent
                            spacing: Theme.spacingSm
                            AppLabel {
                                text: tabItem.modelData.icon
                                font.family: Theme.iconFontFamily
                                font.pointSize: Theme.bodySize
                                color: tabItem.current ? Theme.foreground : Theme.foregroundSecondary
                            }
                            AppLabel {
                                text: tabItem.modelData.title
                                font.pointSize: Theme.bodySize
                                color: tabItem.current ? Theme.foreground : Theme.foregroundSecondary
                            }
                        }

                        Rectangle {
                            visible: tabItem.current
                            anchors.bottom: parent.bottom
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: Math.max(24, tabContent.width * 0.28)
                            height: 4
                            radius: 2
                            color: Theme.accent
                        }

                        TapHandler { onTapped: page.tab = tabItem.index }
                    }
                }

                Item { Layout.fillWidth: true }

                AppButton {
                    readonly property bool checking: page.tab === 0 ? page.versions.coreChecking
                                                     : page.tab === 1 ? page.versions.extensionsChecking
                                                     : page.versions.catalogLoading
                    text: checking ? qsTr("刷新中…") : qsTr("刷新列表")
                    enabled: !checking
                    onClicked: {
                        if (page.tab === 0) page.versions.refreshCore();
                        else if (page.tab === 1) page.versions.refreshInstalledExtensions();
                        else page.versions.refreshAvailableExtensions();
                    }
                }

                AppButton {
                    visible: page.tab !== 2
                    text: (page.tab === 0 ? page.versions.coreBusy : page.versions.updating && !page.versions.coreBusy)
                          ? qsTr("更新中…") : qsTr("一键更新")
                    enabled: page.tab === 0 ? page.versions.canUpdate
                                            : page.versions.outdatedCount > 0 && !page.versions.interruptedOperation
                    onClicked: {
                        if (page.tab === 0) page.versions.updateComfyUi(page.channel);
                        else page.versions.updateAllExtensions();
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.materialStroke
        }

        MaterialPanel {
            visible: page.versions.interruptedOperation
            Layout.fillWidth: true
            Layout.margins: Theme.spacingLg
            padding: Theme.spacingMd
            strong: true

            RowLayout {
                width: parent.width
                spacing: Theme.spacingMd
                AppLabel {
                    Layout.fillWidth: true
                    text: qsTr("上次的版本操作（%1）没有完成，正在或未能自动撤销。撤销完成前无法启动 ComfyUI。")
                          .arg(page.versions.interruptedOperationDescription)
                    color: Theme.error
                    wrapMode: Text.WordWrap
                }
                AppButton {
                    text: qsTr("重试恢复")
                    enabled: !page.versions.coreBusy
                    onClicked: page.versions.recoverInterruptedOperations()
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: page.tab

            // ---- ComfyUI
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
                            RemoteLink { Layout.fillWidth: true; remote: page.versions.remoteUrl }

                            AppLabel { text: qsTr("当前分支："); color: Theme.foregroundSecondary }
                            // The branch switcher sits on the branch row it changes.
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingMd

                                AppLabel {
                                    Layout.fillWidth: true
                                    text: page.versions.branch.length > 0 ? page.versions.branch : "—"
                                    font.family: page.monoFont
                                    elide: Text.ElideRight
                                }

                                AppComboBox {
                                    Layout.preferredWidth: Math.min(400, Math.max(300, page.width * 0.28))
                                    model: [
                                        qsTr("Comfy-Org/ComfyUI · master（官方）"),
                                        qsTr("IndexMirror/ComfyUI · master（国内镜像）")
                                    ]
                                    currentIndex: page.coreSource
                                    onActivated: index => page.coreSource = index
                                }

                                AppButton {
                                    text: qsTr("切换分支")
                                    enabled: page.versions.canUpdate
                                    onClicked: {
                                        page.channel = 0;
                                        page.versions.switchBranch(page.coreSource);
                                    }
                                }
                            }

                            AppLabel { text: qsTr("当前版本："); color: Theme.foregroundSecondary }
                            AppLabel {
                                Layout.fillWidth: true
                                text: page.versions.commitFull.length === 0 ? "—"
                                      : page.versions.commitFull
                                        + (page.versions.commitDate.length > 0 ? "  (" + page.versions.commitDate + ")" : "")
                                font.family: page.monoFont
                                elide: Text.ElideRight
                            }

                            AppLabel {
                                visible: page.versions.localChanges > 0
                                text: qsTr("本地修改：")
                                color: Theme.foregroundSecondary
                            }
                            RowLayout {
                                visible: page.versions.localChanges > 0
                                Layout.fillWidth: true
                                spacing: Theme.spacingMd
                                AppLabel {
                                    text: qsTr("%n 个已跟踪文件被修改过，更新时会保留", "", page.versions.localChanges)
                                }
                                AppButton {
                                    compact: true
                                    destructive: true
                                    text: qsTr("放弃修改…")
                                    enabled: page.versions.canUpdate
                                    onClicked: discardDialog.openFor("", "ComfyUI", page.versions.localChanges)
                                }
                                Item { Layout.fillWidth: true }
                            }
                        }
                    }

                    AppLabel {
                        visible: page.versions.coreError.length > 0
                        Layout.fillWidth: true
                        text: page.versions.coreError
                        color: Theme.error
                        wrapMode: Text.WordWrap
                    }

                    Row {
                        Repeater {
                            model: [qsTr("稳定版"), qsTr("开发版")]
                            delegate: Rectangle {
                                id: channelTab
                                required property int index
                                required property string modelData
                                width: 180
                                height: 42
                                radius: 5
                                color: page.channel === index ? Theme.surfaceRaised : Theme.surfaceSubtle
                                border.width: 1
                                border.color: Theme.materialStroke

                                AppLabel {
                                    anchors.centerIn: parent
                                    text: channelTab.modelData
                                    font.pointSize: Theme.bodySize
                                }

                                TapHandler { onTapped: page.channel = channelTab.index }
                            }
                        }
                    }

                    Table {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        loading: page.versions.coreChecking
                        emptyText: page.versions.repository ? qsTr("没有找到版本，请刷新列表。") : ""
                        header: CommitHeader {}
                        model: page.channel === 0 ? page.versions.stableVersions : page.versions.coreVersions
                        delegate: CommitRow {
                            canSwitch: page.versions.canUpdate
                            onSwitchRequested: commit => page.versions.switchCoreVersion(commit)
                        }
                    }
                }
            }

            // ---- Installed extensions
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingLg
                    spacing: Theme.spacingMd

                    AppTextField {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        placeholderText: qsTr("搜索已安装插件…")
                        // The filter lives in the model, so it survives leaving the page.
                        Component.onCompleted: text = page.versions.installedExtensionModel.filterText
                        onTextChanged: page.versions.installedExtensionModel.filterText = text
                    }

                    Table {
                        id: installedTable
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Component.onCompleted: page.tableWidth = width
                        onWidthChanged: page.tableWidth = width
                        loading: page.versions.extensionsChecking
                        emptyText: qsTr("custom_nodes 里还没有扩展。")
                        model: page.versions.installedExtensionModel

                        header: [
                            HeaderCell {
                                width: page.enabledWidth; height: parent.height; centered: true; label: qsTr("启用")
                                onDragged: delta => page.widen("enabledWidth", delta, 46, 100)
                            },
                            HeaderCell {
                                width: page.nameWidth; height: parent.height; label: qsTr("插件名")
                                onDragged: delta => page.widen("nameWidth", delta, 150, 520)
                            },
                            HeaderCell {
                                width: page.remoteWidth; height: parent.height; label: qsTr("远端地址")
                                onDragged: delta => page.branchWidth = page.clamp(page.branchWidth - delta, 80, 260)
                            },
                            HeaderCell {
                                width: page.branchWidth; height: parent.height; label: qsTr("当前分支")
                                onDragged: delta => page.trade("branchWidth", "commitWidth", delta, 80, 72)
                            },
                            HeaderCell {
                                width: page.commitWidth; height: parent.height; label: qsTr("版本 ID")
                                onDragged: delta => page.trade("commitWidth", "dateWidth", delta, 72, 130)
                            },
                            HeaderCell {
                                width: page.dateWidth; height: parent.height; label: qsTr("更新日期")
                                onDragged: delta => page.widen("dateWidth", delta, 130, 300)
                            }
                        ]

                        delegate: Rectangle {
                            id: extensionRow
                            required property int index
                            required property var entry
                            readonly property string status: entry.status || ""
                            readonly property bool rowBusy: status === "checking" || status === "updating"
                            readonly property color tone: page.statusColor(status)

                            width: ListView.view.width
                            height: 40
                            clip: true
                            color: index % 2 === 0 ? Theme.materialFill : Theme.surfaceRaised
                            border.width: 1
                            border.color: Theme.materialStroke

                            Row {
                                anchors.fill: parent

                                CheckBox {
                                    width: page.enabledWidth
                                    height: parent.height
                                    checked: extensionRow.entry.enabled
                                    enabled: !extensionRow.rowBusy
                                    onToggled: page.versions.setExtensionEnabled(extensionRow.entry.path, checked)
                                }
                                Cell {
                                    width: page.nameWidth; leftPadding: page.pad; rightPadding: page.pad
                                    text: extensionRow.entry.name
                                    font.family: page.monoFont
                                    opacity: extensionRow.entry.enabled ? 1 : 0.55
                                }
                                RemoteLink {
                                    width: page.remoteWidth; height: parent.height
                                    leftPadding: page.pad; rightPadding: page.pad
                                    verticalAlignment: Text.AlignVCenter
                                    font.pointSize: page.cellFont
                                    remote: extensionRow.entry.remote || ""
                                }
                                Cell {
                                    width: page.branchWidth; leftPadding: page.pad; rightPadding: page.pad
                                    text: extensionRow.entry.branch || "—"
                                    font.family: page.monoFont
                                }
                                Cell {
                                    width: page.commitWidth; leftPadding: page.pad; rightPadding: page.pad
                                    text: (extensionRow.entry.commit || "—") + (extensionRow.entry.changes > 0 ? " *" : "")
                                    color: extensionRow.tone
                                    font.family: page.monoFont
                                }
                                Cell {
                                    width: page.dateWidth; leftPadding: page.pad; rightPadding: page.pad
                                    text: extensionRow.entry.date || ""
                                    color: extensionRow.tone
                                    font.family: page.monoFont
                                }
                                AppButton {
                                    id: statusButton
                                    width: page.statusWidth
                                    height: parent.height
                                    leftPadding: 6
                                    rightPadding: 6
                                    text: page.statusText(extensionRow.status)
                                    enabled: extensionRow.entry.repository && !page.versions.interruptedOperation
                                             && (extensionRow.status === "outdated" || extensionRow.status === "error")
                                    font.pointSize: page.cellFont
                                    hoverEnabled: true
                                    ToolTip.visible: hovered && extensionRow.status === "error"
                                    ToolTip.text: extensionRow.entry.error || ""
                                    onClicked: {
                                        if (extensionRow.status === "error")
                                            page.versions.checkExtensionNow(extensionRow.entry.path);
                                        else
                                            page.versions.updateExtension(extensionRow.entry.path);
                                    }
                                }
                                AppButton {
                                    width: page.versionWidth
                                    height: parent.height
                                    leftPadding: 6
                                    rightPadding: 6
                                    text: qsTr("切换版本")
                                    enabled: extensionRow.entry.repository && !extensionRow.rowBusy
                                    font.pointSize: page.cellFont
                                    onClicked: page.openVersions(extensionRow.entry)
                                }
                                AppButton {
                                    width: page.statusWidth
                                    height: parent.height
                                    leftPadding: 3
                                    rightPadding: 3
                                    text: qsTr("卸载")
                                    destructive: true
                                    enabled: !extensionRow.rowBusy
                                    font.pointSize: page.cellFont
                                    onClicked: {
                                        page.dialogPath = extensionRow.entry.path;
                                        page.dialogName = extensionRow.entry.name;
                                        removeDialog.open();
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ---- Catalog
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingLg
                    spacing: Theme.spacingMd

                    AppTextField {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        placeholderText: qsTr("搜索新插件…")
                        Component.onCompleted: text = page.versions.availableExtensionModel.filterText
                        onTextChanged: page.versions.availableExtensionModel.filterText = text
                    }

                    AppLabel {
                        visible: page.versions.catalogError.length > 0
                        Layout.fillWidth: true
                        text: page.versions.catalogError
                        color: Theme.error
                        wrapMode: Text.WordWrap
                    }

                    Table {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        loading: page.versions.catalogLoading
                        emptyText: page.versions.availableExtensionModel.filterText.length > 0
                                   ? qsTr("没有找到匹配的插件。") : ""
                        model: page.versions.availableExtensionModel
                        reuseItems: true
                        header: [
                            Cell { width: 300; text: qsTr("插件名") },
                            Cell { width: parent.width - 300 - page.installWidth; text: qsTr("简介") }
                        ]

                        delegate: Rectangle {
                            id: catalogRow
                            required property int index
                            required property var entry

                            width: ListView.view.width
                            height: Math.max(48, description.implicitHeight + 14)
                            clip: true
                            color: index % 2 === 0 ? Theme.materialFill : Theme.surfaceRaised
                            border.width: 1
                            border.color: Theme.materialStroke

                            Row {
                                anchors.fill: parent
                                RemoteLink {
                                    width: 300
                                    height: parent.height
                                    leftPadding: 10
                                    rightPadding: 8
                                    verticalAlignment: Text.AlignVCenter
                                    wrapMode: Text.WordWrap
                                    font.pointSize: page.cellFont
                                    remote: catalogRow.entry.remote || ""
                                    text: catalogRow.entry.name
                                }
                                AppLabel {
                                    id: description
                                    width: parent.width - 300 - page.installWidth
                                    height: parent.height
                                    leftPadding: 10
                                    rightPadding: 10
                                    verticalAlignment: Text.AlignVCenter
                                    wrapMode: Text.WordWrap
                                    font.pointSize: page.cellFont
                                    text: catalogRow.entry.description
                                }
                                AppButton {
                                    width: page.installWidth
                                    height: parent.height
                                    leftPadding: 5
                                    rightPadding: 5
                                    text: catalogRow.entry.installing ? qsTr("安装中")
                                          : catalogRow.entry.installed ? qsTr("已装") : qsTr("安装")
                                    enabled: !catalogRow.entry.installed && !catalogRow.entry.installing
                                    font.pointSize: page.cellFont
                                    onClicked: page.versions.installExtension(catalogRow.entry.remote)
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingMd

                        AppTextField {
                            Layout.fillWidth: true
                            placeholderText: qsTr("扩展 URL")
                            text: page.installUrl
                            onTextChanged: page.installUrl = text
                            onAccepted: if (page.installUrl.trim().length > 0) page.versions.installExtension(page.installUrl)
                        }

                        AppButton {
                            text: qsTr("安装")
                            enabled: page.installUrl.trim().length > 0
                            onClicked: page.versions.installExtension(page.installUrl)
                        }
                    }
                }
            }
        }
    }

    AppDialog {
        id: versionDialog
        anchors.centerIn: Overlay.overlay
        width: Math.min(980, page.width - Theme.spacingXl * 2)
        height: Math.min(640, page.height - Theme.spacingXl * 2)
        modal: true
        title: qsTr("版本切换 · %1").arg(page.dialogName)
        standardButtons: Dialog.Cancel
        rejectText: qsTr("关闭")

        contentItem: ColumnLayout {
            spacing: Theme.spacingSm

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMd

                AppLabel {
                    Layout.fillWidth: true
                    text: page.versions.extensionVersionsLoading ? qsTr("正在读取版本历史…")
                          : page.versions.extensionVersionsError.length > 0 ? page.versions.extensionVersionsError
                          : page.dialogChanges > 0
                            ? qsTr("这个扩展有 %n 个文件被修改过。切换时会保留修改；与新版本冲突的地方采用新版本，你的版本会备份。", "", page.dialogChanges)
                            : qsTr("选择要切换到的版本。")
                    color: Theme.foregroundSecondary
                    wrapMode: Text.WordWrap
                }

                AppButton {
                    visible: page.dialogChanges > 0
                    compact: true
                    destructive: true
                    text: qsTr("放弃修改…")
                    onClicked: {
                        versionDialog.close();
                        discardDialog.openFor(page.dialogPath, page.dialogName, page.dialogChanges);
                    }
                }
            }

            Table {
                Layout.fillWidth: true
                Layout.fillHeight: true
                loading: page.versions.extensionVersionsLoading
                header: CommitHeader { buttonWidth: 84 }
                model: page.versions.extensionVersions
                delegate: CommitRow {
                    buttonWidth: 84
                    onSwitchRequested: commit => {
                        versionDialog.close();
                        page.versions.switchExtensionVersion(page.dialogPath, commit);
                    }
                }
            }
        }
    }

    AppDialog {
        id: discardDialog

        // Empty path means ComfyUI itself.
        property string path: ""
        property string name: ""
        property int changes: 0

        function openFor(path, name, changes) {
            discardDialog.path = path;
            discardDialog.name = name;
            discardDialog.changes = changes;
            open();
        }

        anchors.centerIn: Overlay.overlay
        width: Math.min(520, page.width - Theme.spacingXl * 2)
        modal: true
        title: qsTr("放弃本地修改")
        standardButtons: Dialog.Yes | Dialog.No
        acceptText: qsTr("放弃修改")
        rejectText: qsTr("取消")
        destructiveAccept: true
        onAccepted: {
            if (path.length === 0) page.versions.discardCoreChanges();
            else page.versions.discardExtensionChanges(path);
        }

        AppLabel {
            width: discardDialog.availableWidth
            text: qsTr("%1 中 %n 个被修改过的文件将恢复为当前版本的原样。修改过的文件会先复制到备份文件夹。确定继续吗？", "",
                       discardDialog.changes).arg(discardDialog.name)
            wrapMode: Text.WordWrap
        }
    }

    AppDialog {
        id: removeDialog
        anchors.centerIn: Overlay.overlay
        width: Math.min(480, page.width - Theme.spacingXl * 2)
        modal: true
        title: qsTr("卸载扩展")
        standardButtons: Dialog.Yes | Dialog.No
        acceptText: qsTr("卸载")
        rejectText: qsTr("取消")
        destructiveAccept: true
        onAccepted: page.versions.removeExtension(page.dialogPath)

        AppLabel {
            width: removeDialog.availableWidth
            text: qsTr("将把扩展 %1 的文件夹移到回收站。确定继续吗？").arg(page.dialogName)
            wrapMode: Text.WordWrap
        }
    }
}
