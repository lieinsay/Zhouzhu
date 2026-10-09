---
name: zhouzhu-plan-float
description: 浮高计划 docs/PLAN-FLOAT.md 2026-09-27 完成并已推送：同群岛按岛龄高低错落、岛底 ≥ 云带顶 + 1000 m、云带与海下移 1200 m、索桥只在 100 m 内；Mellos 页 float-islands
metadata:
  node_type: memory
  type: project
  originSessionId: e854470c-c3d8-45a6-8052-1ce4de69478a
  modified: 2026-09-27T13:03:19.928Z
---

2026-09-27 用户在游戏里看到「所有岛地面一样高」「海离岛挺近」。查明：生成器所有岛同一基准，2051 群 32 座岛台面 251–382 m；
从岛上看到的「海」其实是云带顶（−3800），离 2051 最低岛底只有 335 m；4684、4981、4855 这几个巨岛群的浮山岛底已穿进云带（最深 −5152）。
用户说「肯定选B」后又说「你的建议没问题，开工吧」，决定写在计划第二节 F1–F6：
- 主岛 δ = 0，其余岛整座平移 −500…+1500 m，按岛龄（年轻往上、老往下）+ 随机，岸缘 + δ ≥ 20 m；做在生成器第三层，两个后端逐位相同。
- 游戏：云带顶 −5000、底 −6200、海面 −6800（相对海面云带不变，等于岛抬高 1.2 km），浮山龙骨夹到岛底 ≥ 云带顶 + 1000 m（夹龙骨是我补的，光下移不够）。
- 分期：G（skyisle-gen）与 Z1（Zhouzhu 垂直结构 + 夹龙骨）并行派子任务；Z2 合流（升子模块、重导 2051/2050 细版与行星包、e2e 金标跟上）。

同一轮之前的小修（已提交未推送）：god 模式不算坠落、往下飞也提速、停在海面之上 2 m、HUD 报云带里/云底下（cf79f6d、a06e32e、9d468bd，新 e2e god_sea_e2e）。
- **结果（2026-09-27 当天做完）**：skyisle-gen cea1566..8a2f12b（G，含 F7 索桥：用户说「100m内有桥才合理」→ 岸距 ≤ 0.1 km、高差 ≤ 30 m，全行星索桥 75,768 → 64）；Zhouzhu da7fd8c..b4135cf（Z1）、90046a0..fd56c10（Z2）。全行星 |δ| 中位 405 m、p90 约 1 km、最高 +1474、七成往上；2051 岸缘 22–1554 m，目的码头号 #26 → #28，村 211 → 206。Z2 顺手修：龙骨不再与生成器 keel_m 取大（keel_m 是崖脚高程、会随浮高涨）。ctest 5/5、24 条 e2e 过。
- **已推送**（用户说「都推送吧」）：skyisle-gen → origin 8a2f12b，Zhouzhu main → origin 与 mepro fd56c10（god 修复 9d468bd 在 20:40 已被推过，非我所推）。
- **待用户**：跑货运费要不要算爬升；本机存档 worlds/seed42 的旧群（43 个 b96b83a）与旧账 2026-09-28 按用户说的删了，留 world.json 与新群 1828。

**Why:** 这轮会改 seed42 世界（码头、村子、金标都会变），剧情内容要等它落地。
**How to apply:** 接着做时先读 docs/PLAN-FLOAT.md 第五节进度与 Mellos 页 float-islands；Z2 重导 config.tsv 时 engine.backend 必须是 cpp。见 [[zhouzhu-plan-planet]]、[[zhouzhu-extend-terrain-v3]]。
