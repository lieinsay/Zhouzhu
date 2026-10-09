---
name: zhouzhu-extend-resource-fields
description: 拓展「资源按生成器的点 / 片 / 散长出来」（满山树、灌丛、石块，一株一块都能采）2026-09-26 已随地貌过渡快进并入 main（a7a9055）并推送 origin 与 mepro；用户四个拍板；ScatterStream、泊地、看着什么采什么；e2e 的新路线与坑
metadata:
  node_type: memory
  type: project
  originSessionId: 906a9252-2f72-4c8a-ae6f-bb816c17ce0d
  modified: 2026-09-25T19:39:13.467Z
---

2026-09-26 在家里 Windows 上做完，分支 extend-resource-fields（从 main 4536385），提交 d65e7d1..bca5336，2026-09-26 已随 extend-cover-blend 快进并入 main（a7a9055）并推送 origin 与 mepro。Mellos 页 `extend-resource-fields`；fmap 事件 e36–e39（resource-sites 长出 resource-scatter / scatter-ledger / resource-landmarks，新平级 scatter-stream），gather、craft 两条链路重写。

用户拍板（AskUserQuestion）：**全量重导**（聚落层也跟上生成器新版）；**每一株每一块都能采**（采空倒、按全局钟长回；生成器的采场照旧是带堆场的采集点）；**树干挡人挡船、矮的不挡**；**灌丛出新原料「柴」**。

- 数据源：skyisle-gen 工作区里**未提交**的三形态资源（resources.py 重写，out/seed42/islands/2051 是 2026-09-26 01:49 跑的）。terrain.bin v5 = v4 + res_field 6 层 u8 + patch_id+1 u16；resources.tsv 三种行 deposit / occurrence / working。专业聚落 51（村号 161–211）、码头 60、邑治 407 户、跑货 村031 → 烧炭营43（203）。点可以落在林木片里（不互斥）。
- 散布：`resource_scatter` 按 128 m 块、格点抖动后落哪块归哪块（否则一株会溢出到隔壁块，按人周围几米找块会漏）；采场的**泊地**（规划时找到的野泊位姿）周围 17 m 不长树，所以「够得着船」在游戏里成立。村子只按估的半径让开，ScatterStream 另按铺出来的房子占地藏掉（66 处村子比估的大 12 m 以上）。
- 采集挑哪一株：先试过「离边最近」和「背后加罚」都被脚边灌丛抢走——最后是**面前 ±45° 按偏离视线的角度挑**。
- 坑：GDExtension 里 `static Ref<…>` 缓存会在渲染 / 物理服务器关掉后才析构，退出刷一屏泄漏——缓存放进节点成员。`GameClock.set_hour` 一次最多往前一天，跨 48 h 要连拨。
- 坑：GDScript `:=` 接循环里的 Variant 会解析失败，headless e2e 空转到超时；跑之前用 `Godot --headless --path game --check-only --script res://tests/x.gd` 先查语法。
- 用户中途说「游戏进程卡住了」：当时没有 Godot 进程在跑，多半是 scatter_shots 的非 headless 窗口（每个机位要停几秒）；scatter_perf 实测帧时正常。没有复现到真卡死，用户没再补充。

**Why:** 下一步（并入 main、远处的树 LOD、村民砍树采石、存档）要从这些约定接着做。
**How to apply:** 验证 = ctest（core 945 项 + 体检）+ 17 条 e2e；换生成器数据重跑 `tools/export_skyisle.py <岛群目录> game/worlds/2051` 后查 e2e 里的村号 / 码头号。以后直接在 main 上做（[[work-on-main]]）。见 [[zhouzhu-slice9-gather]]、[[zhouzhu-refactor-relief-2051]]、[[zhouzhu-slice10-craft]]、[[planet-generator-split-out]]。
