import QtQuick
import QtQuick.Layouts
import F1Theme
import F1Data

// 车辆：速度 / 档位 / 换挡灯 / 转速 + DRS / 油门刹车 / 转向 + ERS / 燃油
ColumnLayout {
    id: carCardRoot
    spacing: Theme.gap
    implicitWidth: 320
    implicitHeight: 460

    // 速度 + 档位
    RowLayout {
        Layout.fillWidth: true
        Text { text: Race.speed; color: Theme.text; font.family: Theme.fontNum; font.pixelSize: 56; font.bold: true }
        Text { text: "km/h"; color: Theme.textDim; font.family: Theme.fontText; font.pixelSize: Theme.fsSmall; Layout.alignment: Qt.AlignBottom; Layout.bottomMargin: 10 }
        Item { Layout.fillWidth: true }
        Rectangle {
            implicitWidth: 70; implicitHeight: 66; radius: 6
            color: Theme.panelHi
            Text { anchors.centerIn: parent; text: Race.gear; color: Theme.text; font.family: Theme.fontNum; font.pixelSize: 52; font.bold: true }
        }
    }

    // 换挡灯：5 绿 5 红 5 紫
    Row {
        id: leds
        Layout.fillWidth: true
        spacing: 4
        readonly property int lit: Math.round(Race.revLights * 15)
        Repeater {
            model: 15
            delegate: Rectangle {
                required property int index
                width: (leds.width - 14 * 4) / 15
                height: 8
                radius: 3
                readonly property color c: index < 5 ? Theme.green : index < 10 ? Theme.red : Theme.purple
                color: index < leds.lit ? c : Qt.rgba(c.r, c.g, c.b, 0.15)
            }
        }
    }

    // 转速 + DRS
    RowLayout {
        Layout.fillWidth: true
        Text { text: Race.rpm + " rpm"; color: Theme.textDim; font.family: Theme.fontNum; font.pixelSize: Theme.fsBody }
        Item { Layout.fillWidth: true }
        Rectangle {
            readonly property color dc: Race.drsState === 3 ? Theme.red : Race.drsState === 2 ? Theme.green
                                      : Race.drsState === 1 ? Theme.yellow : Theme.textDim
            implicitWidth: 52; implicitHeight: 22; radius: 4
            color: Race.drsState === 2 ? Qt.rgba(dc.r, dc.g, dc.b, 0.25) : "transparent"
            border.color: dc
            Text { anchors.centerIn: parent; text: "DRS"; color: parent.dc; font.family: Theme.fontText; font.bold: true; font.pixelSize: Theme.fsSmall }
        }
    }

    // 油门 / 刹车
    Repeater {
        model: [ { n: "THR", c: Theme.green }, { n: "BRK", c: Theme.red } ]
        delegate: Rectangle {
            required property var modelData
            required property int index
            readonly property real v: index === 0 ? Race.throttle : Race.brake
            Layout.fillWidth: true
            implicitHeight: 20
            radius: 3
            color: Theme.panelHi
            Rectangle { width: parent.width * parent.v; height: parent.height; radius: 3; color: Qt.rgba(modelData.c.r, modelData.c.g, modelData.c.b, 0.85) }
            Text { x: 6; anchors.verticalCenter: parent.verticalCenter; text: modelData.n + "  " + Math.round(parent.v * 100) + "%"; color: Theme.text; font.family: Theme.fontNum; font.pixelSize: Theme.fsSmall }
        }
    }

    // 转向 + ERS
    RowLayout {
        Layout.fillWidth: true
        spacing: 10
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 20; radius: 3; color: Theme.panelHi
            readonly property real mid: width / 2
            Rectangle {
                x: Math.min(parent.mid, parent.mid + Race.steer * parent.mid)
                width: Math.abs(Race.steer) * parent.mid
                height: parent.height; radius: 3
                color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.55)
            }
            Rectangle { x: parent.mid; width: 1; height: parent.height; color: Theme.textDim }
            Text { x: 6; anchors.verticalCenter: parent.verticalCenter; text: "STEER"; color: Theme.text; font.family: Theme.fontText; font.pixelSize: Theme.fsSmall }
        }
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 20; radius: 3; color: Theme.panelHi
            Rectangle {
                width: parent.width * Race.ersStore; height: parent.height; radius: 3
                color: Race.ersFault ? Qt.rgba(Theme.red.r, Theme.red.g, Theme.red.b, 0.7) : Qt.rgba(Theme.yellow.r, Theme.yellow.g, Theme.yellow.b, 0.75)
            }
            Text { x: 6; anchors.verticalCenter: parent.verticalCenter; text: Race.ersFault ? "ERS FAULT" : "ERS " + Math.round(Race.ersStore * 100) + "%"; color: Theme.text; font.family: Theme.fontNum; font.pixelSize: Theme.fsSmall }
        }
    }

    Divider {}

    // 燃油
    RowLayout {
        Layout.fillWidth: true
        Text { text: "FUEL"; color: Theme.textDim; font.family: Theme.fontText; font.pixelSize: Theme.fsSmall }
        Text { text: Race.fuel.kg || ""; color: Theme.text; font.family: Theme.fontNum; font.pixelSize: Theme.fsBody }
        Item { Layout.fillWidth: true }
        Text { text: Race.fuel.laps || ""; color: Race.fuel.lapsNegative ? Theme.red : Theme.green; font.family: Theme.fontNum; font.pixelSize: Theme.fsBody }
    }
    GridLayout {
        Layout.fillWidth: true
        columns: 3
        rowSpacing: 4
        Repeater {
            model: [ ["NOW", "now"], ["PER 100 KM", "per100"], ["THIS LAP", "thisLap"],
                     ["LAST LAP", "lastLap"], ["AVG / LAP", "avg"], ["LAPS LEFT", "left"] ]
            delegate: Column {
                required property var modelData
                required property int index
                Layout.fillWidth: true
                Text { text: modelData[0]; color: Theme.textDim; font.family: Theme.fontText; font.pixelSize: Theme.fsSmall - 1 }
                Text { text: Race.fuel[modelData[1]] || "-"; color: index === 5 ? Theme.green : Theme.text; font.family: Theme.fontNum; font.pixelSize: Theme.fsBody }
            }
        }
    }
    Item { Layout.fillHeight: true }
}
