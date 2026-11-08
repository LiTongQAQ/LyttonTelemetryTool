import QtQuick
import QtQuick.Layouts
import F1Theme

// 带标题的面板。内容放进 Card { ... } 里即可（default property）。
// collapsible: 标题栏右侧出现 ▾ 按钮，点击标题栏收起 / 展开（在属性面板勾选即可）。
Rectangle {
    id: cardRoot
    property string title: "TITLE"
    property bool collapsible: false
    property bool collapsed: false
    property bool showDivider: true          // 标题下的分割线
    default property alias content: cardBody.data

    readonly property real headerH: cardHeader.height + Theme.pad * 1.2

    color: Theme.panel
    radius: Theme.radius
    border.color: Theme.line
    implicitWidth: 300
    implicitHeight: 200
    clip: true

    // 收起时只剩标题栏（在 Layout 里也生效）
    Layout.maximumHeight: collapsed ? headerH : Number.POSITIVE_INFINITY
    Behavior on height { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }

    Text {
        id: cardHeader
        x: Theme.pad; y: Theme.pad * 0.6
        text: cardRoot.title
        color: Theme.textDim
        font.family: Theme.fontText
        font.pixelSize: Theme.fsSmall
        font.weight: Font.DemiBold
        font.letterSpacing: 1.2
    }

    Text {
        visible: cardRoot.collapsible
        anchors { right: parent.right; rightMargin: Theme.pad; verticalCenter: cardHeader.verticalCenter }
        text: cardRoot.collapsed ? "▸" : "▾"
        color: Theme.textDim
        font.pixelSize: Theme.fsBody
    }

    MouseArea {
        enabled: cardRoot.collapsible
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: cardRoot.headerH
        cursorShape: cardRoot.collapsible ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: cardRoot.collapsed = !cardRoot.collapsed
    }

    Divider {
        visible: cardRoot.showDivider
        anchors { left: parent.left; right: parent.right; top: cardHeader.bottom; topMargin: Theme.gap * 0.6
                  leftMargin: Theme.pad; rightMargin: Theme.pad }
    }

    Item {
        id: cardBody
        visible: !cardRoot.collapsed
        anchors {
            left: parent.left; right: parent.right; bottom: parent.bottom
            top: cardHeader.bottom
            margins: Theme.pad; topMargin: Theme.gap * 1.6
        }
    }
}
