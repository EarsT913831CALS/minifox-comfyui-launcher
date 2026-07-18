pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Minifox.Shared

Pane {
    id: root

    required property var appContext
    property int selectedCategoryIndex: 0
    property bool removeRequested: false
    readonly property var categoryModel: appContext.configuration.categories
    readonly property var selectedCategory: categoryModel[selectedCategoryIndex]
    readonly property string consoleFontFamily: appContext.settings.consoleFontFamily
    readonly property real consoleFontSize: appContext.settings.consoleFontSize
    readonly property bool consoleWordWrap: appContext.settings.consoleWordWrap

    padding: Theme.spacingLg

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingMd

        PageHeader {
            title: qsTr("启动配置")
            description: qsTr("保存多套 ComfyUI 启动方式；所有可视化选项都直接映射到当前 ComfyUI 命令行参数。")
            icon: "\uE713"
            Layout.fillWidth: true
        }

        Frame {
            Layout.fillWidth: true
            padding: Theme.spacingMd

            RowLayout {
                anchors.fill: parent
                spacing: Theme.spacingSm

                AppLabel {
                    text: qsTr("配置")
                }

                AppComboBox {
                    Layout.preferredWidth: 220
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

        Frame {
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

        Frame {
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

            Frame {
                Layout.fillHeight: true
                Layout.preferredWidth: 230
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
                            anchors.leftMargin: Theme.spacingMd
                            anchors.rightMargin: Theme.spacingMd
                            spacing: Theme.spacingMd

                            IconLabel {
                                glyph: categoryDelegate.modelData.icon
                                iconPointSize: Theme.bodySize
                                color: Theme.foreground
                            }

                            AppLabel {
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

                ColumnLayout {
                    width: optionScroll.availableWidth
                    spacing: Theme.spacingMd

                    PageHeader {
                        title: root.selectedCategory.title
                        description: root.selectedCategory.description
                        icon: root.selectedCategory.icon
                        Layout.fillWidth: true
                    }

                    Repeater {
                        model: {
                            root.appContext.configuration.parameterRevision;
                            return root.appContext.configuration.parametersForCategory(root.selectedCategory.key);
                        }

                        delegate: ParameterEditor {
                            required property var modelData
                            appContext: root.appContext
                            parameter: modelData
                        }
                    }

                    Loader {
                        active: root.selectedCategory.key === "environment"
                        sourceComponent: environmentEditorComponent
                        Layout.fillWidth: true
                    }

                    Loader {
                        active: root.selectedCategory.key === "custom"
                        sourceComponent: customArgumentsComponent
                        Layout.fillWidth: true
                    }

                    Item {
                        Layout.fillHeight: true
                        Layout.minimumHeight: Theme.spacingLg
                    }
                }
            }
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

        Frame {
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
