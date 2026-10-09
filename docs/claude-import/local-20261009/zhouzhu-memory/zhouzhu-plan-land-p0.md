---
name: zhouzhu-plan-land-p0
description: "PLAN-LAND P0 开局换到 #6329（world_seed42）已落地的进度与要记住的坑"
metadata:
  node_type: memory
  type: project
  originSessionId: 1daf9b58-cfc1-4851-9572-e6c83f9b8aab
  modified: 2026-09-29T15:21:15.456Z
---

2026-09-29 在 Zhouzhu main 上做完 P0，未推送：a8cc413、c432101、3dbd913、b7a5afe、e5a4720、c8cc13d、f2788ce、f0a38c4。

- 开局配置只在 `src/story_start.h`（出生群 6329、钟的群 6329、calendar_start 85、邻群 6327）；GDScript 用 `StoryStart.get_spawn_node()` 等静态方法。
- 仓库里的细版在 `game/planet/seed42/groups/<节点>`；2051、2050 不再带。500 m 粗版那一圈用 `zhouzhu_gen near-lods --near 6329 --near-radius 320`（C++ 路）。
- 场景 `world_seed42.tscn`；ctest 体检叫 `site_survey_start`；e2e `voyage_start_e2e`（原 voyage_2051）+ 新 `voyage_2051_e2e`（开船去 #2051）。
- 顺手修的崩溃：下船那一帧 HUD 读已拆掉那一群的 FarmHand/PieceHand 指针（e5a4720）。

**How to apply:** 用户嫌游戏反复启动、窗口开关：e2e 一律无头、成批一条一条顺序跑；开窗口的截图、量帧最后集中做一次。
Release DLL 与 Debug 同名覆盖：量完 Release 要删掉 game/bin 的 dll 再编 Debug，否则 Debug 不重新链接。
