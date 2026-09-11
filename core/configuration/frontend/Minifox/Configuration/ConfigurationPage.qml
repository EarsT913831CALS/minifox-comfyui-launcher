pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Minifox.Shared

Pane {
    id: root

    required property var appContext
    signal launchRequested()
    property int selectedCategoryIndex: 0
    property bool removeRequested: false
    property bool commandPromptErrorRequested: false
    property bool configurationPackageErrorRequested: false
    property bool sensitiveExportRequested: false
    property var pendingExportUrl
    property string searchQuery: ""
    readonly property var categoryModel: appContext.configuration.categories
    readonly property var selectedCategory: categoryModel[selectedCategoryIndex]
    readonly property string consoleFontFamily: appContext.settings.consoleFontFamily
    readonly property real consoleFontSize: appContext.settings.consoleFontSize
    readonly property bool consoleWordWrap: appContext.settings.consoleWordWrap
    readonly property bool compactCategories: width < 1040

    padding: Theme.spacingLg

    // Categories are separate views inside the configuration page. Flush any
    // edits before rendering the newly selected view.
    onSelectedCategoryIndexChanged: root.appContext.configuration.savePendingChanges()


    function filteredParameters() {
        const query = searchQuery.trim().toLowerCase();
        if (query.length === 0)
            return appContext.configuration.parametersForCategory(selectedCategory.key);

        const results = [];
        for (let categoryIndex = 0; categoryIndex < categoryModel.length; ++categoryIndex) {
            const category = categoryModel[categoryIndex];
            if (category.key === "environment" || category.key === "custom")
                continue;
            const parameters = appContext.configuration.parametersForCategory(category.key);
            for (let parameterIndex = 0; parameterIndex < parameters.length; ++parameterIndex) {
                const parameter = parameters[parameterIndex];
                const haystack = [parameter.title, parameter.description, parameter.flag, category.title]
                    .join(" ").toLowerCase();
                if (haystack.indexOf(query) >= 0)
                    results.push(parameter);
            }
        }
        return results;
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingMd

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMd

            PageHeader {
                title: qsTr("高级选项")
                description: qsTr("管理启动配置，并按需调整 ComfyUI 命令行参数。")
                icon: "\uE713"
                Layout.fillWidth: true
            }

            AppButton {
                text: qsTr("启动命令提示符")
                onClicked: {
                    if (!root.appContext.runtime.openCommandPrompt())
                        root.commandPromptErrorRequested = true;
                }
            }

            AppButton {
                text: qsTr("一键启动")
                accented: true
                enabled: root.appContext.runtime.canStart
                         && root.appContext.configuration.valid
                onClicked: root.launchRequested()
            }
        }

        MaterialPanel {
            Layout.fillWidth: true
            padding: Theme.spacingSm

            AppTextField {
                anchors.fill: parent
                placeholderText: qsTr("搜索参数、分类或命令行标志")
                text: root.searchQuery
                Accessible.name: qsTr("搜索高级选项")
                onTextChanged: root.searchQuery = text
            }
        }

        MaterialPanel {
            Layout.fillWidth: true
            padding: Theme.spacingMd

            GridLayout {
                anchors.fill: parent
                columns: 2
                columnSpacing: Theme.spacingMd
                rowSpacing: Theme.spacingSm

                AppLabel {
                    id: profileLabel
                    text: qsTr("配置")
                }

                RowLayout {
                    id: profileFields

                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    AppComboBox {
                        id: profileSelector

                        Layout.preferredWidth: Math.max(
                                                   0,
                                                   (profileFields.width
                                                    - profileFields.spacing) * 0.35)
                        Layout.maximumWidth: Layout.preferredWidth
                        Layout.minimumWidth: 0
                        model: root.appContext.configuration.profileNames
                        currentIndex: root.appContext.configuration.currentProfileIndex
                        contentItem: Text {
                            text: profileSelector.displayText
                            font: profileSelector.font
                            rightPadding: Theme.spacingSm
                                          + (profileSelector.indicator
                                             ? profileSelector.indicator.width : 0)
                            color: profileSelector.enabled
                                   ? Theme.foreground : Theme.foregroundSecondary
                            verticalAlignment: Text.AlignVCenter
                            maximumLineCount: 1
                            wrapMode: Text.NoWrap
                            elide: Text.ElideRight
                            clip: true
                        }
                        onActivated: index => {
                            if (!root.appContext.configurationPackages.switchProfile(index))
                                root.configurationPackageErrorRequested = true;
                        }
                    }

                    AppTextField {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: root.appContext.configuration.currentProfileName
                        placeholderText: qsTr("配置名称")
                        maximumLength: 18
                        onEditingFinished: root.appContext.configuration.currentProfileName = text
                    }

                }

                Item {
                    Layout.preferredWidth: profileLabel.implicitWidth
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    spacing: Theme.spacingSm

                    Item {
                        Layout.fillWidth: true
                    }

                    AppButton {
                        visible: root.appContext.configurationPackages.restartRequired
                        text: qsTr("重启启动器")
                        accented: true
                        compact: true
                        onClicked: root.appContext.configurationPackages.restartLauncher()
                    }

                    AppButton {
                        text: qsTr("导入")
                        accented: true
                        compact: true
                        onClicked: importConfigurationDialog.open()
                    }

                    AppButton {
                        text: qsTr("导出")
                        compact: true
                        onClicked: exportConfigurationDialog.open()
                    }

                    AppButton {
                        text: qsTr("新建")
                        accented: true
                        compact: true
                        onClicked: {
                            if (!root.appContext.configurationPackages.addProfile())
                                root.configurationPackageErrorRequested = true;
                        }
                    }

                    AppButton {
                        text: qsTr("复制")
                        compact: true
                        onClicked: {
                            if (!root.appContext.configurationPackages.duplicateCurrentProfile())
                                root.configurationPackageErrorRequested = true;
                        }
                    }

                    AppToolButton {
                        text: "\uE74D"
                        destructive: true
                        compact: true
                        font.family: Theme.iconFontFamily
                        Accessible.name: qsTr("删除配置")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("删除配置")
                        onClicked: root.removeRequested = true
                    }

                }

                AppLabel {
                    Layout.columnSpan: 2
                    visible: root.appContext.configurationPackages.lastError.length > 0
                             || root.appContext.configurationPackages.lastMessage.length > 0
                    Layout.fillWidth: true
                    text: root.appContext.configurationPackages.lastError.length > 0
                          ? root.appContext.configurationPackages.lastError
                          : root.appContext.configurationPackages.lastMessage
                    color: root.appContext.configurationPackages.lastError.length > 0
                           ? Theme.error : Theme.foregroundSecondary
                    wrapMode: Text.Wrap
                }
            }
        }

        MaterialPanel {
            Layout.fillWidth: true
            padding: Theme.spacingMd

            GridLayout {
                anchors.fill: parent
                columns: 2
                columnSpacing: Theme.spacingMd
                rowSpacing: Theme.spacingSm

                AppLabel {
                    text: qsTr("Python 可执行文件")
                }
                PathField {
                    Layout.fillWidth: true
                    appContext: root.appContext
                    compactButton: true
                    pathValue: root.appContext.configuration.pythonPath
                    executableMode: true
                    onPathEdited: value => root.appContext.configuration.pythonPath = value
                }

                AppLabel {
                    text: qsTr("ComfyUI 根目录")
                }
                PathField {
                    Layout.fillWidth: true
                    appContext: root.appContext
                    compactButton: true
                    pathValue: root.appContext.configuration.comfyRoot
                    folderMode: true
                    onPathEdited: value => root.appContext.configuration.comfyRoot = value
                }
            }
        }

        MaterialPanel {
            visible: !root.appContext.configuration.valid
            Layout.fillWidth: true
            padding: Theme.spacingMd

            ColumnLayout {
                anchors.fill: parent
                spacing: Theme.spacingXs

                RowLayout {
                    IconLabel {
                        glyph: "\uE7BA"
                        color: Theme.error
                    }
                    AppLabel {
                        text: qsTr("请先修正以下配置")
                        color: Theme.error
                        font.weight: Font.DemiBold
                    }
                }

                Repeater {
                    model: root.appContext.configuration.validationErrors
                    delegate: AppLabel {
                        required property string modelData
                        text: "• " + modelData
                        color: Theme.error
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingMd

            MaterialPanel {
                Layout.fillHeight: true
                Layout.preferredWidth: root.compactCategories ? 72 : 230
                padding: Theme.spacingXs

                ListView {
                    id: categoryList
                    anchors.fill: parent
                    model: root.categoryModel
                    currentIndex: root.selectedCategoryIndex
                    spacing: Theme.spacingXs
                    clip: true
                    ScrollBar.vertical: ScrollBar {}

                    delegate: AppItemDelegate {
                        id: categoryDelegate

                        required property int index
                        required property var modelData

                        width: ListView.view.width
                        highlighted: index === categoryList.currentIndex
                        Accessible.name: modelData.title
                        onClicked: {
                            root.selectedCategoryIndex = index;
                            categoryList.currentIndex = index;
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: root.compactCategories ? Theme.spacingSm : Theme.spacingMd
                            anchors.rightMargin: root.compactCategories ? Theme.spacingSm : Theme.spacingMd
                            spacing: root.compactCategories ? 0 : Theme.spacingMd

                            IconLabel {
                                glyph: categoryDelegate.modelData.icon
                                iconPointSize: Theme.bodySize
                                color: Theme.foreground
                                Layout.alignment: Qt.AlignHCenter
                            }

                            AppLabel {
                                visible: !root.compactCategories
                                text: categoryDelegate.modelData.title
                                color: Theme.foreground
                                font.pointSize: Theme.bodySize
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
            }

            ScrollView {
                id: optionScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                contentHeight: optionColumn.implicitHeight

                Column {
                    id: optionColumn

                    width: optionScroll.availableWidth
                    spacing: Theme.spacingMd

                    PageHeader {
                        title: root.searchQuery.trim().length > 0 ? qsTr("搜索结果") : root.selectedCategory.title
                        description: root.searchQuery.trim().length > 0
                                     ? qsTr("显示所有分类中与“%1”匹配的选项。").arg(root.searchQuery.trim())
                                     : root.selectedCategory.description
                        icon: root.searchQuery.trim().length > 0 ? "\uE721" : root.selectedCategory.icon
                        width: optionColumn.width
                    }

                    Loader {
                        active: root.searchQuery.trim().length === 0
                                && root.selectedCategory.key === "device"
                                && root.appContext.runtime.acceleratorDevices.length > 0
                        sourceComponent: cudaDevicePanelComponent
                        visible: active
                        width: optionColumn.width
                        height: active ? implicitHeight : 0
                    }

                    Repeater {
                        model: {
                            root.appContext.configuration.parameterRevision;
                            return root.filteredParameters();
                        }

                        delegate: ParameterEditor {
                            required property var modelData
                            appContext: root.appContext
                            parameter: modelData
                            width: optionColumn.width
                        }
                    }

                    Loader {
                        active: root.searchQuery.trim().length === 0 && root.selectedCategory.key === "environment"
                        sourceComponent: environmentEditorComponent
                        visible: active
                        width: optionColumn.width
                        height: active ? implicitHeight : 0
                    }

                    Loader {
                        active: root.searchQuery.trim().length === 0 && root.selectedCategory.key === "custom"
                        sourceComponent: customArgumentsComponent
                        visible: active
                        width: optionColumn.width
                        height: active ? implicitHeight : 0
                    }

                    AppLabel {
                        visible: root.searchQuery.trim().length > 0 && root.filteredParameters().length === 0
                        text: qsTr("没有找到匹配的高级选项。")
                        color: Theme.foregroundSecondary
                        horizontalAlignment: Text.AlignHCenter
                        width: optionColumn.width
                        topPadding: Theme.spacingXl
                    }

                }
            }
        }
    }

    Component {
        id: cudaDevicePanelComponent

        CudaDevicePanel {
            appContext: root.appContext
        }
    }

    Component {
        id: environmentEditorComponent

        EnvironmentEditor {
            appContext: root.appContext
        }
    }

    Component {
        id: customArgumentsComponent

        MaterialPanel {
            padding: Theme.spacingMd

            ColumnLayout {
                anchors.fill: parent
                spacing: Theme.spacingSm

                AppLabel {
                    text: qsTr("自定义参数会追加在可视化参数之后，可用于尚未被启动器覆盖的新参数。")
                    color: Theme.foregroundSecondary
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }

                AppTextArea {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 140
                    text: root.appContext.configuration.customArguments
                    placeholderText: qsTr("例如：--some-new-option value")
                    font.family: root.consoleFontFamily
                    font.pointSize: root.consoleFontSize
                    selectByMouse: true
                    wrapMode: root.consoleWordWrap ? TextEdit.WrapAnywhere : TextEdit.NoWrap
                    onTextChanged: {
                        if (activeFocus)
                            root.appContext.configuration.customArguments = text;
                    }
                }
            }
        }
    }

    FileDialog {
        id: importConfigurationDialog
        title: qsTr("导入启动配置")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Minifox 配置包 (*.zip)"), qsTr("所有文件 (*)")]
        onAccepted: {
            if (!root.appContext.configurationPackages.importPackage(selectedFile))
                root.configurationPackageErrorRequested = true;
        }
    }

    FileDialog {
        id: exportConfigurationDialog
        title: qsTr("导出启动配置")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "zip"
        nameFilters: [qsTr("Minifox 配置包 (*.zip)"), qsTr("所有文件 (*)")]
        onAccepted: {
            root.pendingExportUrl = selectedFile;
            root.sensitiveExportRequested = true;
        }
    }

    Loader {
        active: root.removeRequested
        sourceComponent: removeDialogComponent
    }

    Loader {
        active: root.commandPromptErrorRequested
        sourceComponent: commandPromptErrorDialogComponent
    }

    Loader {
        active: root.configurationPackageErrorRequested
        sourceComponent: configurationPackageErrorDialogComponent
    }

    Loader {
        active: root.sensitiveExportRequested
        sourceComponent: sensitiveExportDialogComponent
    }

    Component {
        id: sensitiveExportDialogComponent

        AppDialog {
            id: sensitiveExportDialog

            function exportSelectedProfile(includeSensitiveValues) {
                const destination = root.pendingExportUrl;
                root.pendingExportUrl = undefined;
                if (!root.appContext.configurationPackages.exportPackage(
                            destination, includeSensitiveValues))
                    root.configurationPackageErrorRequested = true;
                sensitiveExportDialog.close();
            }

            title: qsTr("选择导出内容")
            anchors.centerIn: Overlay.overlay
            width: Math.min(560, root.width - Theme.spacingXl * 2)
            modal: true
            rejectText: qsTr("取消")
            standardButtons: Dialog.Cancel
            closePolicy: Popup.CloseOnEscape
            Component.onCompleted: open()

            contentItem: ColumnLayout {
                spacing: Theme.spacingMd

                AppLabel {
                    text: qsTr("分享导出仅保留预设选项和数值参数，不含环境变量、自由文本参数和界面状态。完整备份保留原始配置，可能包含明文密码或令牌，请勿分享。")
                    color: Theme.foregroundSecondary
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    Item { Layout.fillWidth: true }

                    AppButton {
                        text: qsTr("完整备份（含明文敏感信息）")
                        onClicked: sensitiveExportDialog.exportSelectedProfile(true)
                    }

                    AppButton {
                        text: qsTr("分享启动选项（推荐）")
                        accented: true
                        onClicked: sensitiveExportDialog.exportSelectedProfile(false)
                    }
                }
            }

            onClosed: {
                root.pendingExportUrl = undefined;
                root.sensitiveExportRequested = false;
            }
        }
    }

    Component {
        id: configurationPackageErrorDialogComponent

        AppDialog {
            title: qsTr("配置操作失败")
            modal: true
            acceptText: qsTr("确定")
            standardButtons: Dialog.Ok
            closePolicy: Popup.CloseOnEscape
            Component.onCompleted: open()

            AppLabel {
                text: root.appContext.configurationPackages.lastError
                wrapMode: Text.Wrap
            }

            onAccepted: root.configurationPackageErrorRequested = false
            onClosed: root.configurationPackageErrorRequested = false
        }
    }

    Component {
        id: commandPromptErrorDialogComponent

        AppDialog {
            title: qsTr("无法启动命令提示符")
            modal: true
            acceptText: qsTr("确定")
            standardButtons: Dialog.Ok
            closePolicy: Popup.CloseOnEscape
            Component.onCompleted: open()

            AppLabel {
                text: root.appContext.runtime.lastError.length > 0
                      ? root.appContext.runtime.lastError
                      : qsTr("命令提示符未能启动。")
                wrapMode: Text.Wrap
            }

            onAccepted: root.commandPromptErrorRequested = false
            onClosed: root.commandPromptErrorRequested = false
        }
    }

    Component {
        id: removeDialogComponent

        AppDialog {
            id: profileDeleteDialog

            property var selectedIds: []
            readonly property var profiles: root.appContext.configuration.profileEntries

            function setSelected(profileId, selected) {
                const updated = selectedIds.slice();
                const index = updated.indexOf(profileId);
                if (selected && index < 0)
                    updated.push(profileId);
                else if (!selected && index >= 0)
                    updated.splice(index, 1);
                selectedIds = updated;
            }

            title: qsTr("选择要删除的配置")
            anchors.centerIn: Overlay.overlay
            width: Math.min(520, root.width - Theme.spacingXl * 2)
            modal: true
            destructiveAccept: true
            acceptEnabled: selectedIds.length > 0
            acceptText: qsTr("删除")
            rejectText: qsTr("取消")
            standardButtons: Dialog.Yes | Dialog.Cancel
            closePolicy: Popup.CloseOnEscape
            Component.onCompleted: open()

            contentItem: ColumnLayout {
                spacing: Theme.spacingMd

                AppLabel {
                    text: qsTr("选择一个或多个配置。当前使用的配置不能删除。")
                    color: Theme.foregroundSecondary
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(
                                                Math.max(deleteProfileList.contentHeight, 48),
                                                240)
                    color: Theme.surfaceRaised
                    border.width: 1
                    border.color: Theme.materialStroke
                    radius: Theme.controlRadius
                    clip: true

                    ListView {
                        id: deleteProfileList

                        anchors.fill: parent
                        model: profileDeleteDialog.profiles
                        boundsBehavior: Flickable.StopAtBounds
                        clip: true
                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AsNeeded
                        }

                        delegate: Rectangle {
                            id: deleteProfileRow

                            required property int index
                            required property var modelData

                            width: ListView.view.width
                            height: 48
                            color: modelData.current
                                   ? Theme.materialFillStrong
                                   : index % 2 === 0
                                     ? Theme.materialFill : Theme.surfaceRaised

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.spacingSm
                                anchors.rightMargin: Theme.spacingMd
                                spacing: Theme.spacingSm

                                CheckBox {
                                    enabled: !deleteProfileRow.modelData.current
                                    checked: profileDeleteDialog.selectedIds.indexOf(
                                                 deleteProfileRow.modelData.id) >= 0
                                    Accessible.name: deleteProfileRow.modelData.name
                                    onToggled: profileDeleteDialog.setSelected(
                                                   deleteProfileRow.modelData.id, checked)
                                }

                                AppLabel {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    text: deleteProfileRow.modelData.name
                                    maximumLineCount: 1
                                    elide: Text.ElideRight
                                }

                                StatusBadge {
                                    visible: deleteProfileRow.modelData.current
                                    text: qsTr("当前使用")
                                    icon: "\uE73E"
                                    statusColor: Theme.accent
                                }
                            }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 1
                                visible: deleteProfileRow.index
                                         < deleteProfileList.count - 1
                                color: Theme.materialStroke
                            }
                        }
                    }
                }
            }

            onAccepted: {
                if (profileDeleteDialog.selectedIds.length > 0
                        && !root.appContext.configurationPackages.deleteProfiles(
                            profileDeleteDialog.selectedIds)) {
                    root.configurationPackageErrorRequested = true;
                }
                root.removeRequested = false;
            }
            onRejected: root.removeRequested = false
            onClosed: root.removeRequested = false
        }
    }
}
