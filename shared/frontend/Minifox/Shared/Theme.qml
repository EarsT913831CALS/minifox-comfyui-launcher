pragma Singleton
import QtQuick

QtObject {
    property string themeMode: "system"
    property bool systemDark: false
    property string uiFontFamily: "Segoe UI"
    property real baseFontSize: 11
    property color accent: "#0067c0"
    property bool reducedMotion: false

    readonly property string iconFontFamily: Qt.fontFamilies().indexOf("Segoe Fluent Icons") >= 0 ? "Segoe Fluent Icons" : "Segoe MDL2 Assets"

    readonly property bool dark: themeMode === "dark" || (themeMode === "system" && systemDark)
    readonly property real accentLuminance: 0.2126 * accent.r + 0.7152 * accent.g + 0.0722 * accent.b

    readonly property color surface: dark ? "#202020" : "#f7f7f7"
    readonly property color surfaceRaised: dark ? "#2b2b2b" : "#ffffff"
    readonly property color surfaceSubtle: dark ? "#282828" : "#f3f3f3"
    // Keep foreground token names distinct from QML's `on...` handler naming.
    readonly property color foreground: dark ? "#ffffff" : "#1b1b1b"
    readonly property color foregroundSecondary: dark ? "#c5c5c5" : "#5d5d5d"
    readonly property color outline: dark ? "#454545" : "#d6d6d6"
    readonly property color shadow: dark ? "#000000" : "#707070"
    readonly property color success: dark ? "#6ccb5f" : "#0f7b0f"
    readonly property color warning: dark ? "#fce100" : "#9d5d00"
    readonly property color error: dark ? "#ff99a4" : "#c42b1c"
    readonly property color info: dark ? "#60cdff" : "#0067c0"
    readonly property color accentForeground: accentLuminance > 0.56 ? "#1b1b1b" : "#ffffff"

    readonly property color buttonFill: dark ? "#333333" : "#fbfbfb"
    readonly property color buttonFillHovered: dark ? "#3d3d3d" : "#f6f6f6"
    readonly property color buttonFillPressed: dark ? "#282828" : "#ededed"
    readonly property color buttonFillDisabled: dark ? "#292929" : "#f2f2f2"
    readonly property color buttonStroke: dark ? "#545454" : "#c9c9c9"
    readonly property color buttonStrokeHovered: dark ? "#707070" : "#ababab"
    readonly property color buttonStrokePressed: dark ? "#454545" : "#bdbdbd"
    readonly property color buttonBottomStroke: dark ? "#707070" : "#b5b5b5"
    readonly property color buttonTextDisabled: dark ? "#808080" : "#8f8f8f"
    readonly property color accentHovered: dark ? Qt.lighter(accent, 1.14) : Qt.lighter(accent, 1.06)
    readonly property color accentPressed: Qt.darker(accent, 1.14)
    readonly property color accentDisabled: Qt.rgba(accent.r, accent.g, accent.b, dark ? 0.38 : 0.46)
    readonly property color dangerFill: "#c42b1c"
    readonly property color dangerFillHovered: "#d13438"
    readonly property color dangerFillPressed: "#a4262c"
    readonly property color dangerForeground: "#ffffff"

    readonly property real captionSize: Math.max(7, baseFontSize * 0.84)
    readonly property real bodySize: baseFontSize
    readonly property real subtitleSize: baseFontSize * 1.2
    readonly property real titleSize: baseFontSize * 1.44
    readonly property real displaySize: baseFontSize * 1.73
    readonly property int controlMotionDuration: reducedMotion ? 0 : 120
    readonly property int motionDuration: reducedMotion ? 0 : 160

    readonly property int spacingXs: 4
    readonly property int spacingSm: 8
    readonly property int spacingMd: 12
    readonly property int spacingLg: 20
    readonly property int spacingXl: 32
    readonly property int radius: 8
    readonly property int controlRadius: 4
    readonly property int controlHeight: 36
}
