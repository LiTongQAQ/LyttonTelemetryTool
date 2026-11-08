import QtQuick
import F1Theme

// 顶部数字卡：标签 + 大号数值（可带小字后缀）
Rectangle {
    id: statCard
    property string label: "LABEL"
    property string value: "-"
    property string suffix: ""
    property color valueColor: Theme.text

    implicitWidth: 200
    implicitHeight: 84
    color: Theme.panel
    radius: Theme.radius
    border.color: Theme.line

    Text {
        x: Theme.pad; y: Theme.pad * 0.7
        text: statCard.label
        color: Theme.textDim
        font.family: Theme.fontText
        font.pixelSize: Theme.fsSmall
        font.weight: Font.DemiBold
        font.letterSpacing: 1.2
    }
    Row {
        x: Theme.pad
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.pad * 0.6
        spacing: 6
        Text {
            text: statCard.value
            color: statCard.valueColor
            font.family: Theme.fontNum
            font.pixelSize: Theme.fsBig - 2
            font.bold: true
        }
        Text {
            anchors.baseline: parent.children[0].baseline
            text: statCard.suffix
            visible: text.length > 0
            color: Theme.textDim
            font.family: Theme.fontNum
            font.pixelSize: Theme.fsBody
        }
    }
}
