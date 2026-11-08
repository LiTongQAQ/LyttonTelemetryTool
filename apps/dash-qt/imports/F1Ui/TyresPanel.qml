import QtQuick
import QtQuick.Layouts
import F1Theme
import F1Data

// 四轮 2x2 + 胎种 / 胎龄 / ERS 模式
ColumnLayout {
    spacing: Theme.gap
    implicitWidth: 300
    implicitHeight: 300

    GridLayout {
        columns: 4
        columnSpacing: Theme.pad * 3
        rowSpacing: Theme.gap
        Layout.alignment: Qt.AlignHCenter
        Repeater {
            model: Race.tyres
            delegate: TyreBlock {
                required property var modelData
                required property int index
                label: ["FL", "FR", "RL", "RR"][index]
                inner: modelData.inner
                surface: modelData.surface
                wear: modelData.wear
                brake: modelData.brake
                innerColor: modelData.innerColor
                wearColor: modelData.wearColor
                brakeColor: modelData.brakeColor
            }
        }
    }
    Item { Layout.fillHeight: true }
    RowLayout {
        Layout.fillWidth: true
        Rectangle { implicitWidth: 12; implicitHeight: 12; radius: 6; color: Race.compoundColor }
        Text { text: Race.compound + "  ·  " + Race.tyreAge + " laps"; color: Theme.textDim; font.family: Theme.fontText; font.pixelSize: Theme.fsSmall }
        Item { Layout.fillWidth: true }
        Text { text: "ERS " + Race.ersMode; color: Theme.textDim; font.family: Theme.fontText; font.pixelSize: Theme.fsSmall }
    }
}
