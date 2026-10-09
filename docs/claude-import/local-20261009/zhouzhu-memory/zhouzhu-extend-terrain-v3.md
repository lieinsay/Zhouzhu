---
name: zhouzhu-extend-terrain-v3
description: 拓展「按新版生成器数据优化岛群地形」（terrain.bin v3、厚岛体、云海 −3800、safe_margin 1 cm）；2026-09-24 已快进并入 main（64bed89）并推送 origin 与 mepro
metadata:
  node_type: memory
  type: project
  originSessionId: 1f147649-13f1-4283-9a55-6244c97a27d2
  modified: 2026-09-24T10:43:11.125Z
---

2026-09-24 落地，分支 extend-terrain-v3（从 main 43f265b 拉出），提交 0a82976..64bed89，2026-09-24 已快进并入 main（64bed89）并推送 origin 与 mepro。

- skyisle-gen 38a1491 起岛群生成器的 D8 走在「路由面」（填平面 + 弯曲噪声 + 岸缘微倾）上、河床已下切；在导出高程上重算流向会整片改道。导出改为按生成器汇流反推流向（best fit），terrain.bin 写生成器自己的汇流。v3 多河宽 / 水深（u16 dm / cm）与地形区（u8）。
- 生成器已取消码头改泊场（tiers.py）；导出用 `--terrain-only` 沿用旧 world.json 的村 / 码头。泊场与资源进游戏还没做。
- 用户拍板：岛体加厚、**云海下移露出悬空**。龙骨 25·√最深岸距（主岛 3.3 km），崖壁 30–160 m；云海在 world_2051.tscn 的 CloudSea y −3800，ctest 断言所有岛底在它之上 100 m。
- 坑：离原点 16 km 时 float32 精度约 1–2 mm，默认 safe_margin 1 mm 让人卡在斜面上原地不动（跳板）；player.tscn 已设 0.01。症状是 real velocity 0、只报一处地板碰撞。别再往几何上找原因（木楔、凸盒子都试过，没用）。
- 岛底起伏大了以后，隔 8 格铺的岛底块间会裂缝见天；现隔 4 格、翘边贴插值线。
- GDScript 里 `for d in [..]` 的循环变量是 Variant，`var x := ... * d` 推断不了类型 → 解析错误，e2e 空转到超时。
- 目视用 `game/tools/terrain_shots.tscn -- --shots=<dir>`（非 headless）。

**Why:** 下次换岛群、动岛体或云海时要知道这些约定。
**How to apply:** 改导出后重跑 `tools/export_skyisle.py --terrain-only`；金标重记；见 [[zhouzhu-extend-rivers]]、[[zhouzhu-refactor-world-2051]]、[[planet-generator-split-out]]。
