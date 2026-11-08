import QtQuick
import QtQuick.Layouts
import F1Theme

// 排名表的一行。字段名与 Race.standings 一致。
// 在维修站：名次格蓝底白字，其余照常显示成绩。被套圈：整行变淡。
Rectangle {
    id: standingsRow
    property int pos: 1
    property string code: "VER"
    property color team: "#3671C6"
    property string gap: "+0.000"
    property string last: "1:20.000"
    property color lastColor: Theme.text
    property string best: "1:20.000"
    property color bestColor: Theme.textDim
    property string tyre: "M"
    property color tyreColor: Theme.medium
    property int age: 0
    property bool inPit: false
    property bool you: false
    property bool lapped: false

    implicitWidth: 420
    implicitHeight: Theme.rowH
    color: you ? Qt.rgba(1, 1, 1, 0.10) : "transparent"
    radius: 3
    opacity: lapped ? 0.55 : 1.0

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 4
        anchors.rightMargin: 6
        spacing: 6

        PositionCell { pos: standingsRow.pos; inPit: standingsRow.inPit }
        Rectangle { implicitWidth: 4; Layout.preferredHeight: standingsRow.height - 8; color: standingsRow.team; radius: 1 }
        Text {
            text: standingsRow.code
            color: Theme.text
            font.family: Theme.fontText
            font.pixelSize: Theme.fsBody
            font.bold: standingsRow.you
            Layout.fillWidth: true
        }
        Text {
            text: standingsRow.gap
            color: standingsRow.you ? Theme.text : Theme.textDim
            font.family: Theme.fontNum
            font.pixelSize: Theme.fsSmall + 1
            horizontalAlignment: Text.AlignRight
            Layout.preferredWidth: 72
        }
        Text {
            text: standingsRow.last
            color: standingsRow.lastColor
            font.family: Theme.fontNum
            font.pixelSize: Theme.fsSmall + 1
            horizontalAlignment: Text.AlignRight
            Layout.preferredWidth: 72
        }
        Text {
            text: standingsRow.best
            color: standingsRow.bestColor
            font.family: Theme.fontNum
            font.pixelSize: Theme.fsSmall + 1
            horizontalAlignment: Text.AlignRight
            Layout.preferredWidth: 72
        }
        Rectangle {
            implicitWidth: 11; implicitHeight: 11; radius: 5.5
            color: standingsRow.tyreColor
            Layout.leftMargin: 6
        }
        Text {
            text: standingsRow.age
            color: Theme.textDim
            font.family: Theme.fontNum
            font.pixelSize: Theme.fsSmall
            horizontalAlignment: Text.AlignRight
            Layout.preferredWidth: 20
        }
    }
}
