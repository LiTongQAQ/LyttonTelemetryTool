import QtQuick
import F1Data
import F1Native

// 实时仪表页 —— 卡片的位置和大小在这里（在 Design Studio 里拖这个文件）。
// 每张卡片是一个 ImCanvas，里面按原 ImGui 界面原样绘制；数值和原来 pageDashboard() 的布局一致：
// 上 六个数字卡 + 天气预报 | 左 车辆 + 最近 30 s | 中 地图 + 轮胎与车况 | 右 排名表。全屏地图时其余卡片浮在地图上。
Item {
    id: dash
    width: 1670
    height: 911
    property bool full: false

    readonly property real gap: 12
    readonly property real rest: height - 84 - gap - 8
    readonly property real leftW: Math.max(320, width * 0.215)
    readonly property real rightW: Math.max(460, width * 0.30)
    readonly property real midW: width - leftW - rightW - 2 * gap
    readonly property real carH: Math.min(480, rest * 0.6)
    readonly property real bottomH: Math.max(330, Math.min(400, rest * 0.47))

    ImCanvas {
        piece: "empty"
        anchors.fill: parent
        visible: !Race.hasData
    }

    Item {
        id: normalLayout
        anchors.fill: parent
        visible: Race.hasData && !dash.full

        ImCanvas { id: statsCard; piece: "stats"; width: parent.width; height: 84 }
        ImCanvas { id: carCard; piece: "car"; y: statsCard.height + dash.gap; width: dash.leftW; height: dash.carH }
        ImCanvas { piece: "recent"; y: carCard.y + carCard.height + 8; width: dash.leftW; height: dash.rest - dash.carH - 8 }
        ImCanvas { id: mapCard; piece: "map"; x: dash.leftW + dash.gap; y: carCard.y; width: dash.midW; height: dash.rest - dash.bottomH - 8 }
        ImCanvas { piece: "status"; x: mapCard.x; y: mapCard.y + mapCard.height + 8; width: dash.midW; height: dash.bottomH }
        ImCanvas { piece: "standings"; x: mapCard.x + mapCard.width + dash.gap; y: carCard.y; width: dash.rightW; height: dash.rest }
    }

    // full screen: the map over everything, the standings, car and condition floating over its edges
    Item {
        anchors.fill: parent
        visible: Race.hasData && dash.full
        readonly property real fm: 14
        readonly property real fTop: 48 + 66 + 6
        readonly property real fLeftW: Math.max(520, width * 0.31)
        readonly property real fCarW: Math.max(240, width * 0.15)
        readonly property real statusW: Math.max(520, width * 0.36)
        readonly property real statusH: Math.min(520, height * 0.36)

        ImCanvas { piece: "map"; anchors.fill: parent }
        ImCanvas { piece: "standings"; opacity: 0.94; x: parent.fm; y: parent.fTop; width: parent.fLeftW; height: parent.height - parent.fTop - parent.fm }
        ImCanvas { piece: "car"; opacity: 0.94; x: parent.width - parent.fm - parent.fCarW; y: 48; width: parent.fCarW
                   height: Math.max(300, parent.height - 48 - parent.fm - parent.statusH - dash.gap) }
        ImCanvas { piece: "status"; opacity: 0.94; x: parent.width - parent.fm - parent.statusW; y: parent.height - parent.statusH - parent.fm
                   width: parent.statusW; height: parent.statusH }
    }
}
