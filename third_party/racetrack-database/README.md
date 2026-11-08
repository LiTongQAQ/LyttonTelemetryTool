# racetrack-database（赛道中心线与宽度）

- 来源：https://github.com/TUMFTM/racetrack-database ，提交 e59595d1f3573b30d1ded6a08984935b957688e0
- 许可：LGPL-3.0（见 `LICENSE`）。中心线原始数据来自 OpenStreetMap，宽度由卫星图像提取。
- 只取了 F1 25 用到的 16 条赛道（`tracks/*.csv`：x_m, y_m, 右宽, 左宽）。
- 用法：`scripts/gen_tracks.py` 把它们转成 `apps/dash-app/tracks/track_data.inc` 编进程序，用于实时仪表的赛道俯视图。
- 注意：部分赛道是旧布局（如亚斯码头 2021 年改道前），对齐误差会偏大或对不上，程序会自动退回显示行车线。
