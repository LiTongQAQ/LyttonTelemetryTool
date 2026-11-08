import QtQuick
import F1Theme

// 轮胎胎种圆点
Rectangle {
    property string compound: "M"
    implicitWidth: 12
    implicitHeight: 12
    radius: width / 2
    color: compound === "S" ? Theme.soft : compound === "M" ? Theme.medium : compound === "H" ? Theme.hard : compound === "I" ? Theme.inter : compound === "W" ? Theme.wet : Theme.textDim
    border.color: Qt.darker(color, 1.6)
}
