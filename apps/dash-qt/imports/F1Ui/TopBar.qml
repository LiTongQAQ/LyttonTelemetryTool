import QtQuick
import QtQuick.Layouts
import F1Theme
import F1Data

// 页面顶部：标题 + 说明，右侧数据源状态、收包速率、录制按钮
Rectangle {
    id: topBarRoot
    property string title: "Live Dash"
    property string hint: ""

    implicitHeight: 64
    color: Theme.bg

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.pad * 1.5
        anchors.rightMargin: Theme.pad * 1.5
        spacing: Theme.pad

        Column {
            Text { text: topBarRoot.title; color: Theme.text; font.family: Theme.fontText; font.pixelSize: Theme.fsTitle + 4; font.bold: true }
            Text { text: topBarRoot.hint; color: Theme.textDim; font.family: Theme.fontText; font.pixelSize: Theme.fsSmall }
        }
        Item { Layout.fillWidth: true }

        // 数据源
        Rectangle {
            readonly property color sc: Race.source === 0 ? Theme.green : Race.source === 1 ? Theme.blue : Theme.yellow
            implicitWidth: srcRow.implicitWidth + 20
            implicitHeight: 30
            radius: 15
            color: Qt.rgba(sc.r, sc.g, sc.b, 0.12)
            border.color: sc
            Row {
                id: srcRow
                anchors.centerIn: parent
                spacing: 8
                Rectangle { width: 8; height: 8; radius: 4; color: parent.parent.sc; anchors.verticalCenter: parent.verticalCenter }
                Text { text: Race.sourceName; color: Theme.text; font.family: Theme.fontText; font.pixelSize: Theme.fsSmall; font.bold: true }
                Text { text: Math.round(Race.packetRate) + " pkt/s"; color: Theme.textDim; font.family: Theme.fontNum; font.pixelSize: Theme.fsSmall }
            }
        }

        // 录制
        Rectangle {
            visible: Race.source !== 2
            implicitWidth: recRow.implicitWidth + 24
            implicitHeight: 30
            radius: 4
            color: Race.recording ? Qt.rgba(Theme.red.r, Theme.red.g, Theme.red.b, 0.25) : Theme.panel
            border.color: Race.recording ? Theme.red : Theme.line
            Row {
                id: recRow
                anchors.centerIn: parent
                spacing: 6
                Rectangle { width: 10; height: 10; radius: 5; color: Theme.red; anchors.verticalCenter: parent.verticalCenter }
                Text { text: Race.recording ? "STOP" : "REC"; color: Theme.text; font.family: Theme.fontText; font.pixelSize: Theme.fsSmall; font.bold: true }
            }
            MouseArea { anchors.fill: parent; onClicked: Race.toggleRecording() }
        }
    }
}
