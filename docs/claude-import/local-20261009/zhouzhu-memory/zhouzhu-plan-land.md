---
name: zhouzhu-plan-land
description: "地貌与聚落计划 docs/PLAN-LAND.md 这一轮 2026-09-30 做完（范围是生成器，游戏只做 P0 开局 #6329）；都没推送；游戏以后跟生成器要按 skyisle-gen out/p3–p7-handoff.md 改导出；Mellos 页 plan-land"
metadata:
  node_type: memory
  type: project
  originSessionId: 1daf9b58-cfc1-4851-9572-e6c83f9b8aab
  modified: 2026-09-29T19:08:49.349Z
---

2026-09-29 用户说「根据 PLAN-LAND 完成所有计划任务」，09-30 做完这一轮。用户开工后定的（都写进了计划第三节 / 第六节）：
- **范围**：计划针对生成器；游戏只做 P0（开局换位置、地图改名）；P1、P2（游戏）不做；P3–P7 只做生成器那半，游戏子模块这一轮不挪（仍 8a2f12b）。先后：先做生成器（[[feedback-generator-before-game]]）。
- L4 `world_seed42`；L5 开局 **#6329**、`calendar_start = 85`、邻群 #6327（候选对比网页 https://claude.ai/artifact/5mpQy1CaAkrhdWdpiRxWjC ）；L29 荒地有主、领照开垦；L26 中转站只在生成器。
- 看过结果后定的：P4 谷收拢「中」3 km、多核占汇聚带大主岛三成（对照网页 https://claude.ai/artifact/L6Amqf4Qooki4nnTU6XBne ）；地形雨按本岛起伏（不按全群台面，浮得高只冷不湿）；P5 ≥ 30 km² 的岛保底一个村（20 户）——用户一度以为「人变少了」，其实总人口不变、只是住处变了，要拿「人住在哪」的表说；P7 #5766 邑治在港口小岛「照现在」（靠大泊场的赢）。
- 两个仓库都在 main 上做。

提交（2026-09-30 用户说「做完就提交推送、然后关机」后已推送：skyisle-gen main 与 island-rivers-resources-settle → origin 5659acd；Zhouzhu main → origin 与 mepro 2505ec2）：
- skyisle-gen main：PS 3bd8604；P3 2a0824a..27d27c2；P5 273d1dd..a416d5c；P4 b28513e..1c8f2c5（在工作树做、变基接在 P5 后，工作树已删，探针数据在 out/p4）；P6 6c59e2b..9405e56；P7 5d44088..8841499；04d4372；调试台修显示 274f399、200898f；**P6 修订 dc0dfbb..5659acd（L30 有水利就有人维护：每处水利记管它的村、≤ 2 km，圩田成田、圩村；L31 原始地表 landcover_natural 与人工改造 landuse）**——副作用：#6329 的邑治换到岛 2 的村125，以后重导开局包要重看出生码头。DESIGN-NOTES 四点三十四（P3）/ 三十五（P4）/ 三十六（P5）/ 三十七（P6）/ 三十八（P7）。pytest 252 全过（本机 numpy 2.5.2，原先记的 16 个环境失败没了）。
- Zhouzhu main：P0 a8cc413..f0a38c4（见 [[zhouzhu-plan-land-p0]]），计划文档若干。
- 量剖面：skyisle-gen `docs/probes/transect.py`（直接读生成器产物）。**Zhouzhu 现在的导出认不得 P3 之后的产物**（新资源亚类、`Link::bridge` 删了、`[island.works]` / `[island.market]` 新段、⑥ 邻边），游戏以后跟生成器时照 skyisle-gen `out/p3-handoff.md`…`out/p7-handoff.md` 改 `tools/export_skyisle.py` 与 `src/gen_export.cpp`、重导 config.tsv 与开局包。
- 子模块 `extern/skyisle-gen` 的工作副本 09-29 切回 gitlink 8a2f12b（停在 a6145f5 时 core 单测 gen_seed42 的 planet.bin 头提交号对不上）。

**Why:** 下次用户说推送、或者让游戏跟上生成器时，从这里知道改了什么、交接在哪。
**How to apply:** 推送前按 [[feedback-skyisle-main-follows]] 与各仓库规矩；游戏跟进时先读计划第六节与各期交接说明。
