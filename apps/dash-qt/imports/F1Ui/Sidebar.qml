import QtQuick
import QtQuick.Layouts
import F1Theme

// 左侧导航。currentIndex 是当前页。
Rectangle {
    id: sideBarRoot
    property int currentIndex: 0
    property var pages: ["Live", "Car Setup", "Capture", "Replay", "Compare", "Forward", "VR Panels", "Settings"]

    implicitWidth: 170
    color: Theme.panel

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.gap
        spacing: 4

        Text {
            text: "LYTTON"
            color: Theme.accent
            font.family: Theme.fontText
            font.pixelSize: Theme.fsTitle
            font.bold: true
            Layout.bottomMargin: Theme.pad
            Layout.leftMargin: 6
        }

        Repeater {
            model: sideBarRoot.pages
            delegate: Rectangle {
                required property string modelData
                required property int index
                Layout.fillWidth: true
                height: 34
                radius: Theme.radius
                color: index === sideBarRoot.currentIndex ? Theme.panelHi : "transparent"
                Rectangle { visible: index === sideBarRoot.currentIndex; width: 3; height: parent.height - 12; anchors.verticalCenter: parent.verticalCenter; color: Theme.accent }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 14
                    text: modelData
                    color: index === sideBarRoot.currentIndex ? Theme.text : Theme.textDim
                    font.family: Theme.fontText
                    font.pixelSize: Theme.fsBody
                }
                MouseArea { anchors.fill: parent; onClicked: sideBarRoot.currentIndex = index }
            }
        }
        Item { Layout.fillHeight: true }
    }
}
