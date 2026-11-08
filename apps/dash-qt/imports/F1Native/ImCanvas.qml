import QtQuick

// 设计用占位：程序里这里是原 ImGui 界面的一块（src/imcanvas.cpp），显示 piece 名字方便排版。
Rectangle {
    property string piece: ""
    property int pageIndex: 0
    property size padding: Qt.size(0, 0)
    color: "#161a20"
    border.color: "#2a303a"
    radius: 6
    Text { anchors.centerIn: parent; text: parent.piece + (parent.piece === "page" ? " " + parent.pageIndex : ""); color: "#87909d" }
}
