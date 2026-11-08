# Track N Race 赛道地图

来源：[Track N Race](https://github.com/NoGoat/Track-N-Race)（作者 NoGoat），GPL-3.0，许可证见同目录 [LICENSE](LICENSE)，所用提交见 [SOURCE](SOURCE)。

`maps/track_<赛道编号>.json` 原样取自上游 `electron-frontend/src/renderer/src/assets/maps/`：Track N Race 用自己的录像生成的各条 F1 25 赛道走线（车辆走线的中位数）、赛段分界、DRS 区、直线模式区、起终点、测速点，按 1000×1000 画布布局，并保存了从游戏坐标到画布的换算参数。

本程序的用法：`scripts/gen_tnr_tracks.py` 按换算参数把它们还原成游戏坐标，生成 `apps/dash-app/tracks/tnr_data.inc`：

- 没有自己扫描地图的赛道，用这条走线直接定位地图（宽度为估计值）；
- DRS 区投影到地图上显示（扫描地图也显示）。
- 只用 F1 25 的内容：2026 赛季包的赛道（马德林，编号 42）和直线模式区不生成。

跟随上游更新：`python scripts/update_tnr.py`（最新提交）或 `python scripts/update_tnr.py <提交>`，然后重新编译。
