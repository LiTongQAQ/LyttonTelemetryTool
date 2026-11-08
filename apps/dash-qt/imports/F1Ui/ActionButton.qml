import QtQuick
import F1Theme

// 按钮：accent 设颜色就是实心强调按钮，不设是普通边框按钮。
Rectangle {
    id: actionButton
    property string text: "Button"
    property color accent: "transparent"
    signal clicked()

    readonly property bool filled: accent.a > 0
    implicitWidth: actionLabel.implicitWidth + 28
    implicitHeight: 34
    radius: 4
    color: filled ? (actionMouse.containsMouse ? Qt.lighter(accent, 1.15) : accent) : (actionMouse.containsMouse ? Theme.panelHi : Theme.panel)
    border.color: filled ? accent : Theme.line

    Text {
        id: actionLabel
        anchors.centerIn: parent
        text: actionButton.text
        color: actionButton.filled ? "white" : Theme.text
        font.family: Theme.fontText
        font.pixelSize: Theme.fsBody
        font.bold: actionButton.filled
    }
    MouseArea {
        id: actionMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: actionButton.clicked()
    }
}
