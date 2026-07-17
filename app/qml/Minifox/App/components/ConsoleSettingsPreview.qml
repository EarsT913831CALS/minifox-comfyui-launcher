import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root

    required property var appContext
    readonly property bool darkConsole: appContext.settings.consoleTheme === "dark" || (appContext.settings.consoleTheme === "system" && Theme.dark)
    readonly property color foreground: darkConsole ? "#f2f2f2" : "#1b1b1b"
    readonly property color secondary: darkConsole ? "#a6a6a6" : "#6b6b6b"

    implicitHeight: previewColumn.implicitHeight + Theme.spacingMd * 2
    color: darkConsole ? "#0c0c0c" : "#ffffff"
    radius: Theme.radius
    border.width: 1
    border.color: Theme.outline
    clip: true

    ColumnLayout {
        id: previewColumn
        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingXs

        AppLabel {
            text: qsTr("实时预览")
            color: root.secondary
            font.family: root.appContext.settings.consoleFontFamily
            font.pointSize: root.appContext.settings.consoleFontSize
            font.weight: Font.DemiBold
        }

        AppLabel {
            Layout.fillWidth: true
            Layout.maximumWidth: 620
            text: (root.appContext.settings.showTimestamps ? "[12:34:56]  " : "") + qsTr("SYS  控制台主题、字体、字号与时间戳会立即显示在这里；这是一条用于检查自动换行是否生效的较长示例消息。")
            textFormat: Text.PlainText
            color: root.foreground
            font.family: root.appContext.settings.consoleFontFamily
            font.pointSize: root.appContext.settings.consoleFontSize
            wrapMode: root.appContext.settings.consoleWordWrap ? Text.WrapAnywhere : Text.NoWrap
            elide: root.appContext.settings.consoleWordWrap ? Text.ElideNone : Text.ElideRight
            maximumLineCount: root.appContext.settings.consoleWordWrap ? 3 : 1
        }
    }
}
