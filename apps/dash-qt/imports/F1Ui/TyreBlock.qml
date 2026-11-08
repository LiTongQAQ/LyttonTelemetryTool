import QtQuick
import F1Theme

// 单个轮胎：方块颜色 = 内部温度（按胎种工作窗口渐变，C++ 算好），下面磨损（渐变色）和刹车温度。
Column {
    id: tyreBlock
    property string label: "FL"
    property real inner: 95
    property real surface: 90
    property real wear: 10
    property real brake: 600
    property color innerColor: Theme.green
    property color wearColor: Theme.green
    property color brakeColor: Theme.green

    spacing: 2
    width: 72

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        text: tyreBlock.label
        color: Theme.textDim
        font.family: Theme.fontText
        font.pixelSize: Theme.fsSmall - 1
    }
    Rectangle {
        width: tyreBlock.width; height: 78
        radius: 10
        color: tyreBlock.innerColor
        border.color: Qt.darker(tyreBlock.innerColor, 1.4)
        border.width: 2
        Column {
            anchors.centerIn: parent
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: Math.round(tyreBlock.inner) + "°"
                color: "#101214"
                font.family: Theme.fontNum
                font.pixelSize: Theme.fsTitle
                font.bold: true
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "surf " + Math.round(tyreBlock.surface) + "°"
                color: "#202428"
                font.family: Theme.fontNum
                font.pixelSize: Theme.fsSmall - 2
            }
        }
    }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        text: Math.round(tyreBlock.wear) + "%"
        color: tyreBlock.wearColor
        font.family: Theme.fontNum
        font.pixelSize: Theme.fsBody
        font.bold: true
    }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        text: "BRK " + Math.round(tyreBlock.brake) + "°"
        color: tyreBlock.brakeColor
        font.family: Theme.fontNum
        font.pixelSize: Theme.fsSmall
    }
}
