pragma Singleton
import QtQuick

// 全局颜色 / 字体 / 间距。改这里，所有页面一起变。
QtObject {
    // 背景与面板
    readonly property color bg:        "#0d0f13"
    readonly property color panel:     "#161a20"
    readonly property color panelHi:   "#1e232b"
    readonly property color line:      "#2a303a"
    // 文字
    readonly property color text:      "#e8eaed"
    readonly property color textDim:   "#87909d"
    // 强调
    readonly property color accent:    "#e82020"
    readonly property color side:      "#111419"
    readonly property color blue:      "#40a0ff"
    readonly property color cyan:      "#3cdce6"
    readonly property color orange:    "#ff8c28"
    readonly property color pitBg:     "#1f6fff"   // 维修站：名次格蓝底白字
    readonly property color purple:    "#b266ff"   // 全场最快
    readonly property color green:     "#2ed678"   // 个人最快
    readonly property color yellow:    "#f5c518"
    readonly property color red:       "#ff4848"
    // 轮胎
    readonly property color soft:      "#e8343a"
    readonly property color medium:    "#f5d020"
    readonly property color hard:      "#f0f0f0"
    readonly property color inter:     "#3cb043"
    readonly property color wet:       "#2f7de1"

    // 字体（文字 Titillium Web，数字 JetBrains Mono）
    readonly property string fontText:  "Titillium Web"
    readonly property string fontNum:   "JetBrains Mono"
    readonly property int fsSmall:  12
    readonly property int fsBody:   15
    readonly property int fsTitle:  18
    readonly property int fsBig:    32

    // 间距 / 圆角
    readonly property int gap:     8
    readonly property int pad:     12
    readonly property int radius:  6
    readonly property int rowH:    26
}
