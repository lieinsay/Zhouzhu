---
name: zhouzhu-slice9-gather
description: 切片九采集（资源点从 skyisle-gen 落地、野泊、按住 E 采、堆场打箱装船）2026-09-25 在分支 slice-9-gather 上 9 个提交未推送；用户拍板野泊不用船台；家里跑生成器要给 resolved toml 的中文键加引号
metadata:
  node_type: memory
  type: project
  originSessionId: 0c851cf3-bbc3-4a12-986e-658b6172a519
  modified: 2026-09-25T09:42:34.212Z
---

2026-09-25 在家里 Windows 上做完，分支 slice-9-gather（从 main 17ffb99 拉出），提交 84cbdc3..ee58bf3，2026-09-25 已随切片十快进并入 main 并推送 origin 与 mepro。Mellos 页 `slice-9-gather`，fmap 链路 `gather`。

用户拍板（AskUserQuestion）：**野泊**——船在平地上方悬停落地、自带舷梯，不给资源点配船台；存量随全局钟回满；做到装船为止（货箱能在堆场 / 村仓拆回背包，卖给谁以后再说）。

- 数据：`tools/export_skyisle.py --resources <岛群目录> game/worlds/2051` 只写 resources.tsv（944 处）。家里的 skyisle-gen out/ 是这次现跑的（`run --seed 42` 约 3 分钟 + `island 2051`），重跑的 terrain.bin 与仓库逐字节一致。
- 坑：skyisle-gen 写的 out/seed42/config.resolved.toml 里 `[island.resources.ore_gain]` 的中文键没加引号，`island` 子命令读它会报 TOMLDecodeError——只在 out/ 里给键加引号就能跑（没改生成器的代码）。
- 野泊的规矩：船 ≤ 8 m/s、甲板比落定处高不过 40 m 才提；码头 / 船坞 / 船台在 45 m 内先靠它们；野泊不了不拦着离舵。舷梯挑朝 200 m 内资源点的那一舷。
- 坑：梯板往甲板里多伸一截会顺着坡翘出坎、把人绊在舷边——梯顶必须齐着甲板边。`ship.velocity` 是飞行时的旧值，判断船停没停用 `get_speed()`。
- e2e 坑：村152 的民居离船台都不到 40 m，船悬在房子上空时提示是「落上船台」，野泊的拒绝原因只能查 `get_wild_note()`；开地图要发 InputEventAction（`Input.action_press` 不进 `_unhandled_input`）。

**Why:** 下一步（卖原料、拿原料盖房、泊场）从这些约定接着做。
**How to apply:** 全套 16 条 e2e + ctest 在这台机器上 2026-09-25 全绿；以后直接在 main 上做（[[work-on-main]]）。见 [[zhouzhu-slice8-freight]]、[[zhouzhu-extend-terrain-v3]]、[[feedback-no-long-carry-walks]]。
