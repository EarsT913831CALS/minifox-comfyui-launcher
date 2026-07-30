pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Minifox.Shared

Pane {
    id: root

    required property var appContext
    signal launchRequested()
    property int selectedCategoryIndex: 0
    property bool removeRequested: false
    property bool commandPromptErrorRequested: false
    property string searchQuery: ""
    readonly property var categoryModel: appContext.configuration.categories
    readonly property var selectedCategory: categoryModel[selectedCategoryIndex]
    readonly property string consoleFontFamily: appContext.settings.consoleFontFamily
    readonly property real consoleFontSize: appContext.settings.consoleFontSize
    readonly property bool consoleWordWrap: appContext.settings.consoleWordWrap
    readonly property bool compactCategories: width < 1040

    padding: Theme.spacingLg

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
                    if (!root.appContext.runtime.preflightReady) {
                        root.appContext.runtime.openCommandPrompt();
                        return;
                    }
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

        AppTextField {
            Layout.fillWidth: true
            placeholderText: qsTr("搜索参数、分类或命令行标志")
            text: root.searchQuery
            Accessible.name: qsTr("搜索高级选项")
            onTextChanged: root.searchQuery = text
        }

        MaterialPanel {
            Layout.fillWidth: true
            padding: Theme.spacingMd

            ColumnLayout {
                anchors.fill: parent
                spacing: Theme.spacingSm

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    AppLabel {
                        text: qsTr("配置")
                    }

                    AppComboBox {
                        Layout.preferredWidth: Math.min(240, Math.max(170, root.width * 0.24))
                        model: root.appContext.configuration.profileNames
                        currentIndex: root.appContext.configuration.currentProfileIndex
                        onActivated: index => root.appContext.configuration.currentProfileIndex = index
                    }

                    AppTextField {
                        Layout.fillWidth: true
                        text: root.appContext.configuration.currentProfileName
                        placeholderText: qsTr("配置名称")
                        onEditingFinished: root.appContext.configuration.currentProfileName = text
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    Item {
                        Layout.fillWidth: true
                    }

                    AppButton {
                        text: qsTr("新建")
                        accented: true
                        onClicked: root.appContext.configuration.addProfile()
                    }

                    AppButton {
                        text: qsTr("复制")
                        onClicked: root.appContext.configuration.duplicateCurrentProfile()
                    }

                    AppToolButton {
                        text: "\uE74D"
                        destructive: true
                        font.family: Theme.iconFontFamily
                        Accessible.name: qsTr("删除当前配置")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("删除当前配置")
                        onClicked: root.removeRequested = true
                    }
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
                        active: root.searchQuery.trim().length === 0 && root.selectedCategory.key === "device"
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
                    onActiveFocusChanged: {
                        if (!activeFocus)
                            root.appContext.configuration.customArguments = text;
                    }
                }
            }
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
            title: qsTr("删除启动配置")
            modal: true
            destructiveAccept: true
            acceptText: qsTr("删除")
            rejectText: qsTr("取消")
            standardButtons: Dialog.Yes | Dialog.Cancel
            closePolicy: Popup.CloseOnEscape
            Component.onCompleted: open()

            AppLabel {
                text: qsTr("确定删除“%1”吗？此操作无法撤销。").arg(root.appContext.configuration.currentProfileName)
                wrapMode: Text.Wrap
            }

            onAccepted: {
                root.appContext.configuration.removeCurrentProfile();
                root.removeRequested = false;
            }
            onRejected: root.removeRequested = false
        }
    }
}
