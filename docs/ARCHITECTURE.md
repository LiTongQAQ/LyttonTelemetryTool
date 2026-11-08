# 架构规划

状态：开发中。原 Python 回放软件弃用，全部用 C++ 重写。

## 目标

- VR（SteamVR）中显示方向盘仪表信息，**双人生涯（在线）也能用**。
- 统一接收 F1 25 UDP 遥测，录制、回放、转发给其他工具。
- 回放软件用 C++ 重写，与上述功能集成为一个 APP。

## 约束

- 在线模式开着 EA AntiCheat：不注入游戏、不改游戏文件，只读游戏发出的 UDP 遥测。
- Swofty 插件（`mods/`）只在离线单人可用；`swofty/telemetry.hpp` 同样只在离线可用。
- 数据源只用 EA 官方 UDP 规范（2025 格式 + 2026 赛季包）。字段按官方文档写，不凭记忆。

## 模块

```
apps/
  telemetry-core/   C++ 头文件库：按 EA 规范解析 UDP 包。所有模块共用
  dash-app/
    hub/            接收 + 转发 + 录制 + 回放
    timing/         排名、赛段、踏板等比赛状态（与界面无关，可单测）
    race/           f1-race.exe：ImGui + D3D11 + SteamVR 叠加层（OpenVR C 接口，运行时加载）
  analyzer/         赛后分析（Qt，待做）
third_party/        imgui 1.91.9（MIT）、openvr 2.5.1（BSD，openvr_capi.h 有一处本地补丁）、
                    racetrack-database（LGPL-3.0，赛道中心线和宽度）、f1-circuits（MIT，赛道中心线）
```

构建：CMake + Qt 自带的 llvm-mingw（clang 17 + UCRT，与 Swofty 插件同一套工具链）：

```sh
export PATH="/d/YzcWorkSpace/Qt/Tools/CMake_64/bin:/d/YzcWorkSpace/Qt/Tools/llvm-mingw1706_64/bin:$PATH"
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
ctest --test-dir build --output-on-failure
```

程序：

- `f1-race.exe`：比赛时运行。内含 hub（默认监听 20700、转发 20777、自动录制到 `recordings/`）、HMD 面板（SteamVR 叠加层）和桌面设置/预览窗口。`--demo` 演示数据，`--replay file.f1tr` 回放。设置保存在 exe 旁的 `f1-race.ini`。需要同目录的 `openvr_api.dll`（构建时自动复制）。
- `f1hub.exe`：命令行版 hub。`f1hub listen --port 20700 --forward 20777 --record race.f1tr` / `f1hub replay race.f1tr`

HMD 面板（共用一张纹理，每块是一个独立的 SteamVR 叠加层），全部**跟随头部**、约 1 m 远：

- 基准位置**贴视野边缘**，写在 `race/layout.cpp` 的 `BASE[]`：排名贴左上角，赛段贴右上角，油门刹车贴下边缘正中，各内缩 5°。左右边缘取双眼重叠区的边缘，上下取垂直视场边缘，换头显会自动贴到它自己的边缘。
- 启动时从 SteamVR 读取头显参数（每只眼的视场、屏幕外倾角、推荐渲染分辨率、型号）（大视场头显两侧边缘只有单眼可见，面板以双眼重叠区为界），并按像素/度自动提高面板纹理分辨率（1～2 倍）。读不到或想强制时，可在 `f1-race.ini` 里写 `hmd.renderW / hmd.renderH / hmd.fovH / hmd.fovV`（0 = 自动）。
- 背景和信息（文字、格子、图标）分两层绘制，透明度分别可调（`bgOpacity=` 默认 75%，`textOpacity=` 默认 100%）。
- 客户端只调相对基准的偏移（左右/上下/远近/转角/仰角/缩放），左右、上下按视角（度）移动并保持正对眼睛；自动保存或点“保存配置”写入 exe 旁的 `f1-race.ini`（`<面板>.place=` 一行）。

- 排名：精简 6 行（领跑、前两位、自己、后两位；领跑或末位时连续 6 位），差距默认相对自己（`gapMode=`），自己一行白底高亮，轮胎类型 + 使用圈数，全场最快圈标记
- 赛段：当前圈 S1/S2/S3（紫=全场最快，绿=个人最快，黄=较慢，红=无效）、各段个人最佳、上一圈/个人最佳/全场最佳圈速
- 油门刹车：一条连续进度条，刹车（红）从中线向左、油门（绿）向右点亮，斜切分段、上下导轨、刻度尺；两端上方显示百分比，中间梯形框内显示转速（颜色随换挡灯）

## Hub 转发

```
F1 25 遥测设置: 127.0.0.1:20700   ← 只有 hub 监听
        │
       hub ─┬─▶ 20777  写死 20777 的 Mod/工具（如 Wheel Dash）
            ├─▶ 20779  其他外部程序
            └─▶ 20780+ SimHub 等可配置端口的工具
```

- APP 内部模块（VR 仪表、回放界面）直接从 hub 取数据，不走 UDP。
- 转发目标写在配置文件里；可选"转发为广播"。
- 已知限制：游戏只输出一种格式，不做 2024 等旧格式转换；在线时他人数据受其遥测隐私设置限制。

## 录制格式（`.f1tr`，版本 1）

文件头 `"F1TR"` + u16 版本 + u16 保留，之后是记录 `{u64 距开始的纳秒, u16 长度, 原始 UDP 包}`，全部小端。
保存原始包而不是解析结果，以后解析器补全字段，旧录像也能用。

版本记录：软件见 [apps/README.md](../apps/README.md)，每个 Mod 见各自目录下的 `README.md`。

## 待办

- [x] telemetry-core：包头、CarTelemetry、CarStatus、LapData、CarDamage、Event（尺寸 static_assert，真实抓包测试）
- [x] hub：接收、转发、录制、回放 + `f1hub` 命令行
- [x] telemetry-core：Participants、SessionHistory
- [ ] telemetry-core：Session（安全车/VSC）、Motion 等
- [x] f1-race：排名、赛段、踏板三块 HMD 面板 + 桌面预览与摆放设置
- [ ] 在游戏电脑上实测 SteamVR 叠加层（这台电脑没有 SteamVR）
- [ ] 赛后分析程序（Qt）

## 参考

- 包结构依据 EA 官方 F1 25 UDP 规范，并与 MIT 许可的 Rust crate f1-game-library-models-25 0.5.0 逐项对照；测试用的真实抓包来自该 crate（见 `apps/telemetry-core/tests/data/LICENSE-*`）。

- Wheel Dash（ross.wheel-dash 0.11.0，Overtake.gg）：Swofty 插件，替换 `steering_lcd_display/lcd_display.idf`，在游戏进程内接收 UDP 并写入 `ui_menu_data.wd_*`。仅离线可用；其字段列表可作 vr-dash 显示内容参考。
