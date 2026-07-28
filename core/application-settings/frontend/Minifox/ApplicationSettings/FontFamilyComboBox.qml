pragma ComponentBehavior: Bound

import QtQuick
import Minifox.Shared

AppComboBox {
    id: control

    property string selectedFamily
    readonly property var familyValues: [""].concat(Qt.fontFamilies())
    readonly property string systemDefaultText: qsTr("系统默认")
    signal familySelected(string family)

    model: familyValues
    currentIndex: Math.max(0, familyValues.indexOf(selectedFamily))
    displayText: currentIndex === 0
                 ? systemDefaultText
                 : familyValues[currentIndex]
    font.family: selectedFamily.length > 0
                 ? selectedFamily
                 : Theme.uiFontFamily
    onActivated: index => familySelected(familyValues[index])

    delegate: AppItemDelegate {
        required property int index
        required property string modelData

        width: ListView.view
               ? ListView.view.width - ListView.view.rightMargin
               : control.width
        implicitHeight: Math.max(Theme.controlHeight,
                                 implicitContentHeight + topPadding + bottomPadding)
        leftPadding: Theme.spacingMd
        rightPadding: Theme.spacingMd
        topPadding: Theme.spacingSm
        bottomPadding: Theme.spacingSm
        text: index === 0 ? control.systemDefaultText : modelData
        font.family: index === 0 ? Theme.uiFontFamily : modelData
        font.pointSize: Math.max(11, Theme.bodySize)
        highlighted: control.highlightedIndex === index
        hoverEnabled: control.hoverEnabled
    }
}
