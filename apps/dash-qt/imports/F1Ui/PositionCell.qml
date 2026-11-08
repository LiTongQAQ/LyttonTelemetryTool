import QtQuick
import F1Theme

// 名次格：在维修站时蓝底白字，其余时候透明底。
Rectangle {
    property int pos: 1
    property bool inPit: false

    implicitWidth: 30
    implicitHeight: Theme.rowH - 4
    radius: 3
    color: inPit ? Theme.pitBg : "transparent"

    Text {
        anchors.centerIn: parent
        text: parent.pos
        color: parent.inPit ? "white" : Theme.text
        font.family: Theme.fontNum
        font.pixelSize: Theme.fsBody
        font.bold: true
    }
}
