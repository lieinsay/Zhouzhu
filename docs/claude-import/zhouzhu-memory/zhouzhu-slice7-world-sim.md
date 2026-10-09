---
name: zhouzhu-slice7-world-sim
description: 切片七（全局世界时钟 + 远近分层村落模拟）已落地的要点与坑；2026-09-23 已快进并入 main 6e9a505 并推送
metadata:
  node_type: memory
  type: project
  originSessionId: dde837db-9016-4c21-b52e-c28287798257
  modified: 2026-09-23T07:44:44.170Z
---

2026-09-23 落地，分支 slice-7-world-sim，提交 252fff6..6e9a505，2026-09-23 已按用户要求快进并入 main 并推送 origin 与 mepro（一并带上切片六与地图宿主重构）。

- 钟：autoload `GameClock`（类 WorldClock，game/world_clock.tscn），一日 2400 s，物理优先级 −1000；Farm −10。F4 快进 ×30。村民节点不再有 day_seconds/start_hour/set_hour——测试拨钟用 `GameClock.set_hour`（只许往前）/`restart`。
- 拨钟一跳 >10 s，近处村子先按远处规则结到当下再摆开，所以人会「瞬移」到该在的地方：测试里跳钟后要清掉上一帧位置。
- 远近同源：village_sim::advance_village 一套规则；远处走路按直线 × 1.25 估时。账房 Hinterland 挂在 world_2051 场景 SiteStream 之后。
- 性能：Debug 下远处结算慢 ~10 倍，F4 ×30 时远处会落后几个钟头；预算数字要用 Release 量（`site_survey --sim 24`）。
- 坑：Python heredoc 改 C++ 时 `\\n` 会被吃成真换行（字符串字面量断行）——含 \n 的字符串一律用 Edit 工具改。

**Why:** 下个切片（存档、码头班次等）都会读这个钟和账本。
**How to apply:** 新系统要按时间推进就读 GameClock；要在远处跑就进 village_sim 规则，不另写一份。相关 [[zhouzhu-refactor-map-host]]、[[zhouzhu-slice5-villagers]]。
