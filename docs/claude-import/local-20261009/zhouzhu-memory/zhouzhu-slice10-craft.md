---
name: zhouzhu-slice10-craft
description: 切片十合成与摆放（MC 式 36 格背包 + 快捷栏、船舱容器、配方与设施、地上摆物件）2026-09-25 已快进并入 main（4536385）并推送；用户的三个拍板；E 的先后；窗口模式下真鼠标会盖掉测试的悬停
metadata:
  node_type: memory
  type: project
  originSessionId: 544aca92-32b6-4f23-a9d9-2b323fdedbd4
  modified: 2026-09-25T14:41:21.095Z
---

2026-09-25 在家里 Windows 上做完，分支 slice-10-craft（从 refactor-relief-2051 f873290 拉出），提交 0269ac5..4536385；2026-09-25 按用户要求快进并入 main（4536385）并推送 origin 与 mepro（一并带上切片九与 2051 真实起伏重做）。Mellos 页 `slice-10-craft`，fmap 链路 `craft`（expedition 长出 place-hand、ship-hold）。

用户拍板（AskUserQuestion）：**配方清单 + 设施**（随身 / 工作台 / 窑，不要九宫格）；**船舱单做一个容器**（每节储存区 12 格，人在船上 Tab 列在背包上面；货位照旧只摆货箱）；**只做地上摆物件**（工作台、窑、木箱、火把、篱笆；不做能垒的方块、不上船甲板）。

- 背包：ItemSlots 36 格、前 9 格快捷栏；新东西先并旧叠、再进主区、最后才占快捷栏，所以开局种子在主区、手上空着——farm_e2e / feature_tour 改成「Tab、D 挪到菽、按 1、Tab」。
- E 的先后：虚影绿着摆 > 看着木箱 / 工作台 / 窑打开 > 堆场、村仓（船上不让摆）。摆放让开资源点采掘面 / 堆场 3 m（堆场货箱没碰撞，物理查不到）。
- 坑：窗口模式下开背包放出鼠标后，真鼠标的移动事件会盖掉测试发的悬停位置——e2e 里「悬停 + 数字键」只留一处，其余用点击；窗口失焦时 Godot 会松开所有按着的动作（按住 E 采集会断）。
- 坑：Bash 的 `rm -f "$VAR"/*.png` 被安全检查拦下，截图目录换新名字就行。

**Why:** 下一步（存档、砖盖房、改船要料、卖原料）从这些约定接着做。
**How to apply:** 验证用 ctest（core 909 项 + 体检）+ 17 条 e2e（多了 craft_e2e）。见 [[zhouzhu-slice9-gather]]、[[zhouzhu-refactor-relief-2051]]、[[zhouzhu-slice2-farming]]。
