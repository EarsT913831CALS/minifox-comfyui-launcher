import QtQuick
import QtQuick.Layouts
import Minifox.Shared

MaterialPanel {
    id: root

    required property var appContext
    property bool detailed: true
    signal openConfiguration()
    signal openRuntime()
    signal launchRequested()

    readonly property bool configurationValid: appContext.configuration.valid
    readonly property int runtimeStatus: appContext.runtime.status
    readonly property bool serviceReady: appContext.runtime.serviceReady
    readonly property string stateTitle: {
        if (!configurationValid)
            return qsTr("配置需要完善");
        if (serviceReady)
            return qsTr("ComfyUI 已就绪");
        switch (runtimeStatus) {
        case 1: return qsTr("正在启动 ComfyUI");
        case 2: return qsTr("正在等待服务");
        case 3: return qsTr("正在停止 ComfyUI");
        case 4: return qsTr("启动失败");
        default: return qsTr("准备就绪");
        }
    }
    readonly property string stateDescription: {
        if (!configurationValid)
            return qsTr("修正启动配置后即可一键启动。");
        if (serviceReady)
            return qsTr("ComfyUI 服务运行正常。");
        switch (runtimeStatus) {
        case 1: return qsTr("进程已经创建，正在读取启动输出。");
        case 2: return qsTr("ComfyUI 正在运行，等待网页服务响应。");
        case 3: return qsTr("正在安全结束进程及其子进程。");
        case 4:
            return appContext.runtime.lastError.length > 0
                   ? appContext.runtime.lastError
                   : qsTr("查看控制台了解详细原因，然后重新启动。");
        default: return qsTr("当前配置已通过检查，随时可以启动。");
        }
    }
    readonly property color stateColor: !configurationValid || runtimeStatus === 4
                                        ? Theme.error
                                        : serviceReady
                                          ? Theme.success
                                          : runtimeStatus === 0
                                            ? Theme.accent
                                            : Theme.info
    readonly property string primaryText: {
        if (runtimeStatus === 4) return qsTr("重新启动");
        if (runtimeStatus === 1) return qsTr("正在启动");
        if (runtimeStatus === 2) return qsTr("等待服务");
        if (runtimeStatus === 3) return qsTr("正在停止");
        return qsTr("一键启动");
    }
    readonly property bool primaryEnabled: configurationValid
                                            && (runtimeStatus === 0
                                                || runtimeStatus === 4)
                                            && appContext.runtime.canStart

    padding: 0
    strong: true
    accented: false
    cornerRadius: Theme.radius
    clip: true

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 3
        color: root.stateColor
        opacity: 0.9
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingLg + 3
        anchors.rightMargin: Theme.spacingLg
        anchors.topMargin: Theme.spacingLg
        anchors.bottomMargin: Theme.spacingLg
        spacing: root.detailed ? Theme.spacingMd : Theme.spacingSm

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: -Theme.spacingSm
            spacing: Theme.spacingMd

            Rectangle {
                Layout.preferredWidth: 8
                Layout.preferredHeight: 8
                radius: 4
                color: root.stateColor
            }

            AppLabel {
                text: root.stateTitle
                font.pointSize: Theme.titleSize
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }

            StatusBadge {
                text: root.appContext.runtime.statusText
                icon: root.serviceReady ? "\uE73E" : "\uE711"
                statusColor: root.stateColor
            }
        }

        AppLabel {
            text: root.stateDescription
            color: Theme.foregroundSecondary
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.bottomMargin: Theme.spacingSm
        }

        Rectangle {
            visible: root.detailed
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.materialStroke
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMd

            AppLabel {
                visible: root.detailed
                text: qsTr("当前配置")
                color: Theme.foregroundSecondary
            }

            AppComboBox {
                Layout.fillWidth: true
                model: root.appContext.configuration.profileNames
                currentIndex: root.appContext.configuration.currentProfileIndex
                Accessible.name: qsTr("当前启动配置")
                onActivated: index => root.appContext.configurationPackages.switchProfile(index)
            }

            AppButton {
                text: qsTr("高级选项")
                onClicked: root.openConfiguration()
            }
        }

        ColumnLayout {
            visible: root.detailed
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            LaunchInformationRow {
                Layout.fillWidth: true
                label: qsTr("ComfyUI")
                value: root.appContext.configuration.comfyRoot
            }
            LaunchInformationRow {
                Layout.fillWidth: true
                label: qsTr("Python")
                value: root.appContext.configuration.pythonPath
            }
            LaunchInformationRow {
                Layout.fillWidth: true
                label: qsTr("服务地址")
                value: root.appContext.runtime.serviceUrl
            }
            LaunchInformationRow {
                Layout.fillWidth: true
                label: qsTr("CUDA 设备")
                value: root.appContext.runtime.acceleratorSummary.length > 0
                       ? root.appContext.runtime.acceleratorSummary
                       : root.appContext.hardware.summary
            }
        }

        Rectangle {
            visible: !root.configurationValid
            Layout.fillWidth: true
            implicitHeight: validationText.implicitHeight + Theme.spacingMd * 2
            radius: Theme.controlRadius
            color: Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, Theme.dark ? 0.14 : 0.08)
            border.width: 1
            border.color: Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.34)

            AppLabel {
                id: validationText
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                text: root.appContext.configuration.validationErrors.join(" · ")
                color: Theme.error
                wrapMode: Text.WordWrap
            }
        }

        Item {
            visible: root.detailed
            Layout.fillHeight: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Item { Layout.fillWidth: true }

            AppButton {
                visible: root.appContext.runtime.canStop
                text: qsTr("停止")
                destructive: true
                Layout.preferredHeight: Theme.prominentControlHeight
                onClicked: root.appContext.runtime.stop()
            }
            AppButton {
                visible: root.runtimeStatus !== 0 || root.appContext.runtime.logModel.count > 0
                text: qsTr("查看控制台")
                Layout.preferredHeight: Theme.prominentControlHeight
                onClicked: root.openRuntime()
            }
            AppButton {
                visible: !root.configurationValid
                text: qsTr("完善配置")
                Layout.preferredHeight: Theme.prominentControlHeight
                onClicked: root.openConfiguration()
            }
            AppButton {
                visible: !root.serviceReady
                text: root.primaryText
                accented: true
                prominent: true
                enabled: root.primaryEnabled
                Accessible.name: root.primaryText
                onClicked: root.launchRequested()
            }
        }
    }
}
