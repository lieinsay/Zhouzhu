---
name: zhouzhu-refactor-map-host
description: 地图宿主重构（2026-09-23 已落地，分支 refactor-map-host 7 个提交 093f03a..9e292f3，同日随切片七并入 main 并推送）——Expedition、GroundHost/MapHost、平板岛、feature_tour e2e 的坑
metadata:
  node_type: memory
  type: project
  originSessionId: bc59e48e-e3c2-4158-94c6-d8db4f504086
  modified: 2026-09-23T04:10:28.620Z
---

2026-09-23 在分支 refactor-map-host 上做完，提交 093f03a..9e292f3（纯算法 / 宿主 / 编排 / 平板岛 / 测试 / README / fmap），同日随切片七快进并入 main（6e9a505）并推送，分支已删。

- 编排只有一个 `Expedition`（Voyage、WorldScene、settlement_host.h 已删）；场景根配 `map` 指向 MapHost（main.tscn 的 Map=IslandMap，world_2051 的 World，flat_map.tscn 的 FlatMap）。
- 类名 `FlatMap`（Godot 节点）≠ `FlatIsland`（纯算法结构体），别混。
- `WorldVillage::is_edited()` 意思是「有没存的改动」，存盘后为 false。
- e2e 驱动 `game/tests/feature_tour.gd`，子类 world_features_e2e / flat_map_e2e 只给钩子；岛群那条靠地图点击瞬移——地图沿用上次缩放，点之前要先滚轮缩小。
- 主码头 #7 的停机坪在船坞坪靠海那侧：直走会顶上坞墩块或门架柱（细柱射线打不着，要用胶囊 shape query 查）；卡没卡住跟后台建块时机有关，时有时无。
- 行为变化清单写在 README「与重构前不一样的地方」；world_e2e 块数 178→182 是出生抬高 0.1 m 造成的加载时机差，不是回归。

相关：[[zhouzhu-refactor-world-2051]] [[zhouzhu-slice6-world-voyage]] [[commit-split-by-topic]]
