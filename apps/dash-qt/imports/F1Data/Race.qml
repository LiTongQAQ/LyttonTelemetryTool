pragma Singleton
import QtQuick

// 设计用假数据。程序运行时由 C++ 的 Race（src/backend.hpp）替换，属性名和类型完全一致。
// 新增字段：先在 backend.hpp 里加，再在这里加同名假值。只放属性（Design Studio 不支持这里写函数）。
QtObject {
    // 数据源
    property bool hasData: true
    property int source: 1              // 0 实时 1 虚拟 2 回放
    property string sourceName: "SIMULATION"
    property string status: "Simulation running"
    property int port: 20700
    property real packetRate: 248
    property bool recording: false
    property string vrStatus: "SteamVR connected"
    property string version: "0.3.0"
    property int startPage: 0
    property int page: 0
    property bool mapFull: false
    property bool transport: false

    // 赛事
    property string trackName: "Melbourne"
    property string sessionName: "Race"
    property int lap: 23
    property int totalLaps: 58
    property int position: 3
    property int numCars: 20

    // 圈速
    property string currentLap: "0:42.118"
    property bool currentInvalid: false
    property string lastLap: "1:20.470"
    property color lastLapColor: "#2ed678"
    property string bestLap: "1:20.205"
    property color bestLapColor: "#2ed678"
    property string sessionBest: "1:20.112"
    property var sectors: [
        { label: "S1", time: "28.412", color: "#b266ff", done: true },
        { label: "S2", time: "21.903", color: "#2ed678", done: true },
        { label: "S3", time: "30.155", color: "#87909d", done: false }
    ]

    // 车辆
    property int speed: 287
    property string gear: "7"
    property int rpm: 11250
    property real revLights: 0.7
    property real throttle: 0.92
    property real brake: 0.0
    property real steer: -0.12
    property real ersStore: 0.64
    property bool ersFault: false
    property string ersMode: "MEDIUM"
    property int drsState: 2            // 0 关 1 可用 2 打开 3 故障
    property var fuel: ({ kg: "38.20 kg", laps: "+1.8 LAPS", lapsNegative: false, now: "31.4 g/s",
                          per100: "212 kg", thisLap: "0.84 kg", lastLap: "1.62 kg", avg: "1.60 kg", left: "23.9" })
    // FL FR RL RR
    property var tyres: [
        { inner: 98,  surface: 92,  wear: 18, brake: 620, innerColor: "#40ff60", wearColor: "#8cff40", brakeColor: "#40ff60" },
        { inner: 102, surface: 97,  wear: 21, brake: 655, innerColor: "#40ff60", wearColor: "#9cff40", brakeColor: "#40ff60" },
        { inner: 95,  surface: 90,  wear: 14, brake: 540, innerColor: "#40ff70", wearColor: "#70ff40", brakeColor: "#40ff60" },
        { inner: 97,  surface: 91,  wear: 15, brake: 560, innerColor: "#40ff60", wearColor: "#78ff40", brakeColor: "#40ff60" }
    ]
    property string compound: "M"
    property color compoundColor: "#fad228"
    property int tyreAge: 12

    // 排名表
    property int gapMode: 0             // 0 相对我 1 前车 2 领跑
    property ListModel standings: ListModel {
        ListElement { pos: 1; code: "VER"; team: "#3671C6"; gap: "Leader"; last: "1:20.512"; lastColor: "#e8eaed"; best: "1:20.301"; bestColor: "#87909d"; tyre: "M"; tyreColor: "#fad228"; age: 14; inPit: false; you: false; lapped: false }
        ListElement { pos: 2; code: "NOR"; team: "#FF8000"; gap: "+1.204"; last: "1:20.388"; lastColor: "#e8eaed"; best: "1:20.112"; bestColor: "#b266ff"; tyre: "M"; tyreColor: "#fad228"; age: 14; inPit: false; you: false; lapped: false }
        ListElement { pos: 3; code: "LTN"; team: "#27F4D2"; gap: "+3.877"; last: "1:20.470"; lastColor: "#e8eaed"; best: "1:20.205"; bestColor: "#87909d"; tyre: "M"; tyreColor: "#fad228"; age: 12; inPit: false; you: true; lapped: false }
        ListElement { pos: 4; code: "LEC"; team: "#E8002D"; gap: "+5.019"; last: "1:20.701"; lastColor: "#e8eaed"; best: "1:20.450"; bestColor: "#87909d"; tyre: "H"; tyreColor: "#ebebeb"; age: 3; inPit: false; you: false; lapped: false }
        ListElement { pos: 5; code: "PIA"; team: "#FF8000"; gap: "+6.330"; last: "1:20.655"; lastColor: "#e8eaed"; best: "1:20.398"; bestColor: "#87909d"; tyre: "M"; tyreColor: "#fad228"; age: 15; inPit: true; you: false; lapped: false }
        ListElement { pos: 6; code: "HAM"; team: "#E8002D"; gap: "+8.102"; last: "1:20.980"; lastColor: "#e8eaed"; best: "1:20.611"; bestColor: "#87909d"; tyre: "H"; tyreColor: "#ebebeb"; age: 4; inPit: false; you: false; lapped: false }
        ListElement { pos: 7; code: "RUS"; team: "#27F4D2"; gap: "+9.455"; last: "1:21.044"; lastColor: "#e8eaed"; best: "1:20.702"; bestColor: "#87909d"; tyre: "M"; tyreColor: "#fad228"; age: 13; inPit: false; you: false; lapped: false }
        ListElement { pos: 8; code: "ALO"; team: "#229971"; gap: "+12.870"; last: "1:21.230"; lastColor: "#e8eaed"; best: "1:20.904"; bestColor: "#87909d"; tyre: "H"; tyreColor: "#ebebeb"; age: 5; inPit: false; you: false; lapped: false }
        ListElement { pos: 9; code: "TSU"; team: "#3671C6"; gap: "+14.002"; last: "1:21.115"; lastColor: "#e8eaed"; best: "1:20.950"; bestColor: "#87909d"; tyre: "M"; tyreColor: "#fad228"; age: 16; inPit: false; you: false; lapped: false }
        ListElement { pos: 10; code: "GAS"; team: "#0093CC"; gap: "+15.640"; last: "1:21.402"; lastColor: "#e8eaed"; best: "1:21.011"; bestColor: "#87909d"; tyre: "S"; tyreColor: "#f03232"; age: 9; inPit: false; you: false; lapped: false }
        ListElement { pos: 11; code: "ALB"; team: "#64C4FF"; gap: "+17.221"; last: "1:21.300"; lastColor: "#e8eaed"; best: "1:21.020"; bestColor: "#87909d"; tyre: "M"; tyreColor: "#fad228"; age: 17; inPit: false; you: false; lapped: false }
        ListElement { pos: 12; code: "SAI"; team: "#64C4FF"; gap: "+18.090"; last: "1:21.512"; lastColor: "#e8eaed"; best: "1:21.100"; bestColor: "#87909d"; tyre: "H"; tyreColor: "#ebebeb"; age: 6; inPit: false; you: false; lapped: false }
        ListElement { pos: 13; code: "HUL"; team: "#52E252"; gap: "+20.455"; last: "1:21.601"; lastColor: "#e8eaed"; best: "1:21.230"; bestColor: "#87909d"; tyre: "M"; tyreColor: "#fad228"; age: 18; inPit: false; you: false; lapped: false }
        ListElement { pos: 14; code: "OCO"; team: "#B6BABD"; gap: "+22.310"; last: "1:21.777"; lastColor: "#e8eaed"; best: "1:21.300"; bestColor: "#87909d"; tyre: "H"; tyreColor: "#ebebeb"; age: 7; inPit: true; you: false; lapped: false }
        ListElement { pos: 15; code: "STR"; team: "#229971"; gap: "+24.009"; last: "1:21.690"; lastColor: "#e8eaed"; best: "1:21.401"; bestColor: "#87909d"; tyre: "M"; tyreColor: "#fad228"; age: 18; inPit: false; you: false; lapped: false }
        ListElement { pos: 16; code: "BEA"; team: "#B6BABD"; gap: "+26.880"; last: "1:21.902"; lastColor: "#e8eaed"; best: "1:21.512"; bestColor: "#87909d"; tyre: "M"; tyreColor: "#fad228"; age: 19; inPit: false; you: false; lapped: false }
        ListElement { pos: 17; code: "LAW"; team: "#6692FF"; gap: "+29.115"; last: "1:22.004"; lastColor: "#e8eaed"; best: "1:21.640"; bestColor: "#87909d"; tyre: "H"; tyreColor: "#ebebeb"; age: 8; inPit: false; you: false; lapped: false }
        ListElement { pos: 18; code: "ANT"; team: "#27F4D2"; gap: "+31.402"; last: "1:22.150"; lastColor: "#e8eaed"; best: "1:21.770"; bestColor: "#87909d"; tyre: "M"; tyreColor: "#fad228"; age: 20; inPit: false; you: false; lapped: false }
        ListElement { pos: 19; code: "BOR"; team: "#52E252"; gap: "+1 LAP"; last: "1:22.390"; lastColor: "#e8eaed"; best: "1:22.001"; bestColor: "#87909d"; tyre: "H"; tyreColor: "#ebebeb"; age: 9; inPit: false; you: false; lapped: true }
        ListElement { pos: 20; code: "DOO"; team: "#0093CC"; gap: "+1 LAP"; last: "1:22.511"; lastColor: "#e8eaed"; best: "1:22.130"; bestColor: "#87909d"; tyre: "M"; tyreColor: "#fad228"; age: 21; inPit: false; you: false; lapped: true }
    }
}
