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
    property string installedSearch: ""
    property string availableSearch: ""
    property string extensionUrl: ""
    property string pendingRemovalPath: ""

    padding: Theme.spacingLg

    function matches(value, query) {
        return query.length === 0
                || String(value).toLowerCase().indexOf(query.toLowerCase()) >= 0;
    }

    function refreshCurrentTab() {
        if (selectedTab === 0)
            versions.refreshCore();
        else if (selectedTab === 1)
            versions.refreshInstalledExtensions();
        else
            versions.refreshAvailableExtensions();
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingMd

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMd

            PageHeader {
                title: qsTr("版本管理")
                description: qsTr("管理 ComfyUI 内核版本、已安装扩展和可安装扩展。")
                icon: "\uE81C"
                Layout.fillWidth: true
            }

            AppButton {
                text: root.versions.busy ? qsTr("刷新中…") : qsTr("刷新当前页")
                enabled: !root.versions.busy
                onClicked: root.refreshCurrentTab()
            }

            AppButton {
                visible: root.selectedTab !== 2
                text: root.versions.updating ? qsTr("更新中…") : qsTr("一键更新")
                accented: true
                enabled: root.selectedTab === 0
                         ? root.versions.canUpdate
                         : !root.versions.busy
                           && root.versions.installedExtensions.length > 0
                onClicked: {
                    if (root.selectedTab === 0)
                        root.versions.updateComfyUi(root.coreChannel);
                    else
                        root.versions.updateAllExtensions();
                }
            }
        }

        TabBar {
            id: versionTabs
            Layout.fillWidth: true
            currentIndex: root.selectedTab
            onCurrentIndexChanged: root.selectedTab = currentIndex

            TabButton { text: qsTr("ComfyUI 内核") }
            TabButton { text: qsTr("已安装扩展") }
            TabButton { text: qsTr("安装新扩展") }
        }

        Frame {
            visible: root.versions.statusMessage.length > 0
                     || root.versions.lastError.length > 0
            Layout.fillWidth: true
            padding: Theme.spacingSm

            AppLabel {
                width: parent.width
                text: root.versions.lastError.length > 0
                      ? root.versions.lastError : root.versions.statusMessage
                color: root.versions.lastError.length > 0
                       ? Theme.error : Theme.foregroundSecondary
                wrapMode: Text.Wrap
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.selectedTab

            ColumnLayout {
                spacing: Theme.spacingMd

                Frame {
                    Layout.fillWidth: true
                    padding: Theme.spacingMd

                    GridLayout {
                        anchors.fill: parent
                        columns: 2
                        columnSpacing: Theme.spacingMd
                        rowSpacing: Theme.spacingXs

                        AppLabel { text: qsTr("远程地址") }
                        AppLabel {
                            text: root.versions.remoteUrl || "—"
                            color: Theme.foregroundSecondary
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                        AppLabel { text: qsTr("当前分支") }
                        AppLabel {
                            text: root.versions.branch || "—"
                            color: Theme.foregroundSecondary
                        }
                        AppLabel { text: qsTr("当前版本") }
                        AppLabel {
                            text: (root.versions.commit || "—")
                                  + (root.versions.commitDate.length > 0
                                     ? "  (" + root.versions.commitDate + ")" : "")
                            color: Theme.foregroundSecondary
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true

                    AppLabel { text: qsTr("版本通道") }
                    AppComboBox {
                        Layout.preferredWidth: 180
                        model: [qsTr("稳定版"), qsTr("开发版")]
                        currentIndex: root.coreChannel
                        onActivated: index => root.coreChannel = index
                    }
                    Item { Layout.fillWidth: true }
                }

                ListView {
                    id: coreVersionList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: Theme.spacingXs
                    model: root.coreChannel === 0
                           ? root.versions.stableVersions
                           : root.versions.coreVersions
                    ScrollBar.vertical: ScrollBar {}

                    delegate: Frame {
                        id: coreDelegate
                        required property var modelData
                        width: ListView.view.width
                        padding: Theme.spacingSm

                        RowLayout {
                            anchors.fill: parent
                            spacing: Theme.spacingMd

                            AppLabel {
                                text: coreDelegate.modelData.shortCommit
                                color: Theme.info
                                font.family: root.appContext.settings.consoleFontFamily
                            }
                            AppLabel {
                                text: coreDelegate.modelData.subject
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            AppLabel {
                                text: coreDelegate.modelData.date
                                color: Theme.foregroundSecondary
                            }
                            StatusBadge {
                                visible: coreDelegate.modelData.current
                                text: qsTr("当前")
                                statusColor: Theme.success
                            }
                            AppButton {
                                text: qsTr("切换")
                                enabled: !coreDelegate.modelData.current
                                         && !root.versions.busy
                                onClicked: root.versions.switchCoreVersion(
                                    coreDelegate.modelData.commit, root.coreChannel)
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                spacing: Theme.spacingMd

                AppTextField {
                    Layout.fillWidth: true
                    placeholderText: qsTr("搜索已安装扩展")
                    text: root.installedSearch
                    onTextChanged: root.installedSearch = text
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: Theme.spacingXs
                    model: root.versions.installedExtensions
                    ScrollBar.vertical: ScrollBar {}

                    delegate: Frame {
                        id: installedDelegate
                        required property var modelData
                        width: ListView.view.width
                        visible: root.matches(modelData.name, root.installedSearch)
                        height: visible ? implicitHeight : 0
                        padding: visible ? Theme.spacingSm : 0

                        RowLayout {
                            anchors.fill: parent
                            spacing: Theme.spacingSm

                            AppSwitch {
                                checked: installedDelegate.modelData.enabled
                                enabled: !root.versions.busy
                                Accessible.name: qsTr("启用 %1").arg(installedDelegate.modelData.name)
                                onToggled: root.versions.setExtensionEnabled(
                                    installedDelegate.modelData.path, checked)
                            }
                            AppLabel {
                                text: installedDelegate.modelData.name
                                Layout.preferredWidth: 250
                                elide: Text.ElideRight
                            }
                            AppLabel {
                                text: installedDelegate.modelData.remote || qsTr("非 Git 扩展")
                                color: Theme.foregroundSecondary
                                Layout.fillWidth: true
                                elide: Text.ElideMiddle
                            }
                            StatusBadge {
                                text: installedDelegate.modelData.status === "outdated"
                                      ? qsTr("可更新")
                                      : installedDelegate.modelData.status === "checking"
                                        ? qsTr("检测中") : qsTr("最新")
                                statusColor: installedDelegate.modelData.status === "outdated"
                                             ? Theme.error
                                             : installedDelegate.modelData.status === "checking"
                                               ? Theme.warning : Theme.success
                            }
                            AppButton {
                                text: qsTr("更新")
                                enabled: installedDelegate.modelData.repository
                                         && !root.versions.busy
                                onClicked: root.versions.updateExtension(
                                    installedDelegate.modelData.path)
                            }
                            AppButton {
                                text: qsTr("卸载")
                                destructive: true
                                enabled: !root.versions.busy
                                onClicked: {
                                    root.pendingRemovalPath = installedDelegate.modelData.path;
                                    removeExtensionDialog.open();
                                }
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                spacing: Theme.spacingMd

                AppTextField {
                    Layout.fillWidth: true
                    placeholderText: qsTr("搜索新扩展")
                    text: root.availableSearch
                    onTextChanged: root.availableSearch = text
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: Theme.spacingXs
                    model: root.versions.availableExtensions
                    ScrollBar.vertical: ScrollBar {}

                    delegate: Frame {
                        id: availableDelegate
                        required property var modelData
                        width: ListView.view.width
                        visible: root.matches(modelData.name, root.availableSearch)
                                 || root.matches(modelData.description, root.availableSearch)
                        height: visible ? implicitHeight : 0
                        padding: visible ? Theme.spacingSm : 0

                        RowLayout {
                            anchors.fill: parent
                            spacing: Theme.spacingMd

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingXs
                                AppLabel {
                                    text: availableDelegate.modelData.name
                                    color: Theme.info
                                    font.weight: Font.DemiBold
                                }
                                AppLabel {
                                    text: availableDelegate.modelData.description
                                    color: Theme.foregroundSecondary
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                            }
                            AppButton {
                                text: availableDelegate.modelData.installed
                                      ? qsTr("已安装") : qsTr("安装")
                                enabled: !availableDelegate.modelData.installed
                                         && !root.versions.busy
                                onClicked: root.versions.installExtension(
                                    availableDelegate.modelData.remote)
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    AppTextField {
                        Layout.fillWidth: true
                        placeholderText: qsTr("扩展 Git URL")
                        text: root.extensionUrl
                        onTextChanged: root.extensionUrl = text
                    }
                    AppButton {
                        text: qsTr("安装")
                        accented: true
                        enabled: root.extensionUrl.trim().length > 0
                                 && !root.versions.busy
                        onClicked: root.versions.installExtension(
                            root.extensionUrl.trim())
                    }
                }
            }
        }
    }

    AppDialog {
        id: removeExtensionDialog
        title: qsTr("卸载扩展")
        modal: true
        destructiveAccept: true
        acceptText: qsTr("卸载")
        rejectText: qsTr("取消")
        standardButtons: Dialog.Yes | Dialog.Cancel
        closePolicy: Popup.CloseOnEscape

        AppLabel {
            width: removeExtensionDialog.availableWidth
            text: qsTr("确定卸载此扩展吗？扩展目录将被删除。")
            wrapMode: Text.Wrap
        }

        onAccepted: {
            root.versions.removeExtension(root.pendingRemovalPath);
            root.pendingRemovalPath = "";
        }
        onRejected: root.pendingRemovalPath = ""
    }
}
