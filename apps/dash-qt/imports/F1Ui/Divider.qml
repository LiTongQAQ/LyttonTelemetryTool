import QtQuick
import QtQuick.Layouts
import F1Theme

// 分割线。放进 Column / ColumnLayout 里是横线；vertical 勾上变竖线（放 Row 里）。
Rectangle {
    property bool vertical: false
    implicitWidth: vertical ? 1 : 100
    implicitHeight: vertical ? 100 : 1
    color: Theme.line
    Layout.fillWidth: !vertical
    Layout.fillHeight: vertical
}
