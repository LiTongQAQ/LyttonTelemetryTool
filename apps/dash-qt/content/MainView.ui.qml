import QtQuick
import F1Theme
import F1Data
import F1Native

// 主界面（在 Design Studio 里可以打开调整）：按 1920 x 1009 设计尺寸排版，整体等比缩放到父项大小。
// 侧边栏 / 顶栏 / 各页内容都是 ImCanvas（原 ImGui 界面原样绘制）；实时仪表页的卡片位置在 DashboardPage.ui.qml。
Rectangle {
    id: mainView
    width: 1920
    height: 1009
    color: Theme.bg

    readonly property bool full: Race.page === 0 && Race.mapFull

    Item {
        id: design
        width: 1920
        height: 1009
        scale: Math.min(mainView.width / width, mainView.height / height)
        transformOrigin: Item.TopLeft
        x: Math.floor((mainView.width - width * scale) / 2)
        y: Math.floor((mainView.height - height * scale) / 2)

        ImCanvas {
            id: sideBar
            piece: "sidebar"
            visible: !mainView.full
            width: 210
            height: parent.height
        }

        Item {
            id: contentArea
            x: mainView.full ? 0 : sideBar.width
            width: parent.width - x
            height: parent.height - (Race.transport ? transportBar.height : 0)

            ImCanvas {
                id: topBar
                x: 20
                y: 0
                width: 1670
                piece: "topbar"
                visible: !mainView.full
                padding: Qt.size(20, 16)
                height: 103
            }

            Item {
                id: pageArea
                x: mainView.full ? 8 : 20
                y: mainView.full ? 8 : topBar.height
                width: parent.width - 2 * x
                height: parent.height - y - (mainView.full ? 8 : 16)

                DashboardPage {
                    anchors.fill: parent
                    anchors.leftMargin: 0
                    anchors.rightMargin: 0
                    anchors.topMargin: 8
                    anchors.bottomMargin: -8
                    visible: Race.page === 0
                    full: mainView.full
                }
                ImCanvas {
                    anchors.fill: parent
                    piece: "page"
                    pageIndex: 1
                    visible: Race.page === 1
                }
                ImCanvas {
                    anchors.fill: parent
                    piece: "page"
                    pageIndex: 2
                    visible: Race.page === 2
                }
                ImCanvas {
                    anchors.fill: parent
                    piece: "page"
                    pageIndex: 3
                    visible: Race.page === 3
                }
                ImCanvas {
                    anchors.fill: parent
                    piece: "page"
                    pageIndex: 4
                    visible: Race.page === 4
                }
                ImCanvas {
                    anchors.fill: parent
                    piece: "page"
                    pageIndex: 5
                    visible: Race.page === 5
                }
                ImCanvas {
                    anchors.fill: parent
                    piece: "page"
                    pageIndex: 6
                    visible: Race.page === 6
                }
                ImCanvas {
                    anchors.fill: parent
                    piece: "page"
                    pageIndex: 7
                    visible: Race.page === 7
                }
                ImCanvas {
                    anchors.fill: parent
                    piece: "page"
                    pageIndex: 8
                    visible: Race.page === 8
                }
            }
        }

        ImCanvas {
            id: transportBar
            piece: "transport"
            visible: Race.transport
            x: contentArea.x
            y: parent.height - height
            width: contentArea.width
            height: 58
        }
    }
}
