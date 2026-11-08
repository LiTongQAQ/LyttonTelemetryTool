import QtQuick
import QtQuick.Layouts
import F1Theme
import F1Data
import F1Native

// 地图 + 右侧竖排赛段；右上角缩放按钮。地图本身由 C++ 画（TrackMap）。
Item {
    id: mapViewRoot
    implicitWidth: 700
    implicitHeight: 460

    RowLayout {
        anchors.fill: parent
        spacing: Theme.gap * 1.5

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.bg
            radius: Theme.radius
            border.color: Theme.line
            clip: true

            TrackMap {
                id: trackMap
                anchors.fill: parent
            }
            Text {
                anchors { left: parent.left; top: parent.top; margins: Theme.pad }
                text: Race.trackName + (Race.totalLaps > 0 ? "  ·  Lap " + Race.lap + "/" + Race.totalLaps : "")
                color: Theme.text
                font.family: Theme.fontText
                font.pixelSize: Theme.fsTitle
            }
            Row {
                anchors { right: parent.right; top: parent.top; margins: Theme.gap }
                spacing: 4
                Repeater {
                    model: ["−", "⤢", "+"]
                    delegate: Rectangle {
                        required property string modelData
                        required property int index
                        width: 28; height: 26; radius: 4
                        color: zoomMouse.containsMouse ? Theme.panelHi : Theme.panel
                        border.color: Theme.line
                        Text { anchors.centerIn: parent; text: parent.modelData; color: Theme.text; font.pixelSize: Theme.fsBody }
                        MouseArea {
                            id: zoomMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: trackMap.zoom = parent.index === 0 ? trackMap.zoom / 1.5 : parent.index === 2 ? trackMap.zoom * 1.5 : 1
                        }
                    }
                }
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.NoButton
                onWheel: (w) => trackMap.zoom = w.angleDelta.y > 0 ? trackMap.zoom * 1.25 : trackMap.zoom / 1.25
            }
        }

        SectorStrip {
            vertical: true
            Layout.preferredWidth: 200
            Layout.maximumWidth: 200
            Layout.fillHeight: true
        }
    }
}
