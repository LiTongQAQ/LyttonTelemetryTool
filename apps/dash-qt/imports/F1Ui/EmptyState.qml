import QtQuick
import QtQuick.Layouts
import F1Theme
import F1Data

// 没有遥测时的提示卡：开始虚拟比赛 / 打开录像
Rectangle {
    implicitWidth: 480
    implicitHeight: 230
    color: Theme.panel
    radius: Theme.radius
    border.color: Theme.line

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.pad * 1.5
        spacing: Theme.gap
        Text { text: "No telemetry yet"; color: Theme.text; font.family: Theme.fontText; font.pixelSize: Theme.fsTitle + 2; font.bold: true }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: Race.source === 0
                  ? "Listening on UDP " + Race.port + ". In F1 25's telemetry settings turn UDP on: IP 127.0.0.1, port " + Race.port + ", format 2025."
                  : Race.status
            color: Theme.textDim; font.family: Theme.fontText; font.pixelSize: Theme.fsBody
        }
        Text { text: "No game on this PC? The simulation shows every feature."; color: Theme.textDim; font.family: Theme.fontText; font.pixelSize: Theme.fsBody }
        Item { Layout.fillHeight: true }
        RowLayout {
            spacing: Theme.gap
            ActionButton { text: "Start Simulation"; accent: Theme.blue; onClicked: Race.useSim() }
            ActionButton { text: "Open Recording..."; onClicked: Race.openRecording() }
        }
    }
}
