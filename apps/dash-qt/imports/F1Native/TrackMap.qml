import QtQuick

// 设计用占位：程序里是 C++ 画的赛道（src/trackmap.cpp）。
Item {
    property real zoom: 1
    Rectangle {
        anchors.centerIn: parent
        width: parent.width * 0.6; height: parent.height * 0.55
        radius: height / 2
        color: "transparent"
        border.color: "#3a404c"
        border.width: 10
    }
    Text { anchors.centerIn: parent; text: "TRACK MAP"; color: "#87909d" }
}
