import QtQuick
import QtQuick.Layouts
import F1Theme
import F1Data

// 本圈三个赛段：已完成的按紫 / 绿 / 黄着色，未完成的显示上一圈（灰）。
// vertical 勾上变竖排（地图旁边），默认横排。
GridLayout {
    id: sectorStrip
    property bool vertical: false
    columns: vertical ? 1 : 3
    rowSpacing: Theme.gap
    columnSpacing: Theme.gap
    implicitWidth: vertical ? 200 : 600
    implicitHeight: vertical ? 220 : 56

    Repeater {
        model: Race.sectors
        delegate: Rectangle {
            required property var modelData
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 50
            radius: Theme.radius
            color: Theme.panelHi
            border.width: modelData.done ? 2 : 1
            border.color: modelData.done ? modelData.color : Theme.line
            Column {
                anchors.centerIn: parent
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: modelData.label
                    color: Theme.textDim
                    font.family: Theme.fontText
                    font.pixelSize: Theme.fsSmall
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: modelData.time
                    color: modelData.done ? modelData.color : Theme.textDim
                    font.family: Theme.fontNum
                    font.pixelSize: Theme.fsTitle
                }
            }
        }
    }
}
