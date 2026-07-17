pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts

RowLayout {
    id: root

    required property var appContext
    property string pathValue
    property bool folderMode: false
    property bool executableMode: false
    property bool dialogRequested: false
    signal pathEdited(string value)

    spacing: Theme.spacingSm

    AppTextField {
        id: pathInput
        Layout.fillWidth: true
        text: root.pathValue
        placeholderText: root.folderMode ? qsTr("选择文件夹") : qsTr("选择文件")
        font.family: root.appContext.settings.consoleFontFamily
        font.pointSize: root.appContext.settings.consoleFontSize
        selectByMouse: true
        onEditingFinished: root.pathEdited(text)
    }

    AppButton {
        text: qsTr("浏览…")
        Accessible.name: root.folderMode ? qsTr("浏览文件夹") : qsTr("浏览文件")
        onClicked: root.dialogRequested = true
    }

    Loader {
        active: root.dialogRequested
        sourceComponent: root.folderMode ? folderDialogComponent : fileDialogComponent
    }

    Component {
        id: folderDialogComponent

        FolderDialog {
            title: qsTr("选择文件夹")
            Component.onCompleted: open()
            onAccepted: {
                root.pathEdited(selectedFolder.toString());
                root.dialogRequested = false;
            }
            onRejected: root.dialogRequested = false
        }
    }

    Component {
        id: fileDialogComponent

        FileDialog {
            title: qsTr("选择文件")
            fileMode: FileDialog.OpenFile
            Component.onCompleted: open()
            nameFilters: root.executableMode ? [qsTr("可执行文件 (*.exe)"), qsTr("所有文件 (*)")] : [qsTr("所有文件 (*)")]
            onAccepted: {
                root.pathEdited(selectedFile.toString());
                root.dialogRequested = false;
            }
            onRejected: root.dialogRequested = false
        }
    }
}
