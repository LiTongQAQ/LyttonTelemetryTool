# f1-circuits（F1 赛道中心线）

- 来源：https://github.com/bacinger/f1-circuits ，提交 8094e957b8d6aa8b9f4510c93968556cccd30e02
- 许可：MIT（见 `LICENSE`）。
- 只取了 racetrack-database 没有的 8 条：摩纳哥、新加坡、巴库、伊莫拉、吉达、迈阿密、拉斯维加斯、卢赛尔（`circuits/*.geojson`，经纬度）。
- 只有中心线、没有宽度，`scripts/gen_tracks.py` 按每条赛道的典型宽度补上。
