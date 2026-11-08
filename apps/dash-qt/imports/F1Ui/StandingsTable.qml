import QtQuick
import QtQuick.Layouts
import F1Theme
import F1Data

// 排名表：差距模式按钮 + 表头 + Race.standings 列表
ColumnLayout {
    id: standingsTable
    spacing: 4
    implicitWidth: 430
    implicitHeight: 22 * Theme.rowH

    // 差距模式
    Row {
        Layout.alignment: Qt.AlignRight
        spacing: 2
        Repeater {
            model: ["TO ME", "INTERVAL", "TO LEADER"]
            delegate: Rectangle {
                required property string modelData
                required property int index
                width: modeLabel.implicitWidth + 14
                height: 20
                radius: 3
                color: Race.gapMode === index ? Theme.panelHi : "transparent"
                border.color: Theme.line
                Text {
                    id: modeLabel
                    anchors.centerIn: parent
                    text: parent.modelData
                    color: Race.gapMode === parent.index ? Theme.text : Theme.textDim
                    font.family: Theme.fontText
                    font.pixelSize: Theme.fsSmall - 1
                }
                MouseArea { anchors.fill: parent; onClicked: Race.gapMode = parent.index }
            }
        }
    }

    // 表头
    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 4
        Layout.rightMargin: 6
        spacing: 6
        component Head: Text {
            color: Theme.textDim
            font.family: Theme.fontText
            font.pixelSize: Theme.fsSmall - 1
            horizontalAlignment: Text.AlignRight
        }
        Head { text: "P"; horizontalAlignment: Text.AlignHCenter; Layout.preferredWidth: 30 }
        Head { text: "DRIVER"; horizontalAlignment: Text.AlignLeft; Layout.fillWidth: true; Layout.leftMargin: 10 }
        Head { text: "GAP"; Layout.preferredWidth: 72 }
        Head { text: "LAST"; Layout.preferredWidth: 72 }
        Head { text: "BEST"; Layout.preferredWidth: 72 }
        Head { text: "TYRE"; Layout.preferredWidth: 43 }
    }
    Divider {}

    ListView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: Race.standings
        delegate: StandingsRow {
            required property var model
            width: ListView.view.width
            pos: model.pos
            code: model.code
            team: model.team
            gap: model.gap
            last: model.last
            lastColor: model.lastColor
            best: model.best
            bestColor: model.bestColor
            tyre: model.tyre
            tyreColor: model.tyreColor
            age: model.age
            inPit: model.inPit
            you: model.you
            lapped: model.lapped
        }
    }
}
