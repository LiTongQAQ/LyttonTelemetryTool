import QtQuick
import F1Theme
import F1Data

// 程序窗口：只是外壳。界面在 MainView.ui.qml（可在 Design Studio 里打开）。
Window {
    id: win
    width: 1680
    height: 1000
    visible: true
    visibility: Window.Maximized
    title: "Lytton Telemetry Tool v" + Race.version
    color: Theme.bg

    // Esc 退出全屏地图
    Shortcut { sequence: "Esc"; enabled: Race.page === 0 && Race.mapFull; onActivated: Race.mapFull = false }

    MainView { anchors.fill: parent }
}
