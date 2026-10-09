---
name: zhouzhu-extend-ship-cabins
description: 拓展「船舱能进去、船上摆工作台等设备、舱里算屋里」2026-09-28 已快进并入 main（70be1f4）并推送 origin 与 mepro；替用户定的与坑
metadata:
  node_type: memory
  type: project
  originSessionId: 5d13745b-dd1d-4cc7-95ab-3205ee415a24
  modified: 2026-09-28T09:00:45.358Z
---

2026-09-28 从 refactor-refit-drag（0cf8c30）切出分支 **extend-ship-cabins**，提交 f4502f1..70be1f4 共 7 个（纯算法 / 空艇 / 人 / 玩法接线 / e2e / README / fmap），2026-09-28 用户要求后已快进并入 main（70be1f4）并推送 origin 与 mepro（连同 refactor-refit-drag）。验收时 30 条 e2e、ctest 5 项全过。Mellos 页 extend-ship-cabins；fmap 事件 e119（extend ship）。

用户原话：「让可以进入的船舱能进入，并且可与在船上放置工作台等设备，在船舱旧相当于室内」。

替用户定的（README「拓展：船舱能进去」里也写了）：
- 能进的舱 = 被盖住的储存区 / 双层货舱 / 空节 / 宽空节（`ModuleDef::enterable`）；浮石舱封死。
- 船上只在空节、宽空节上能摆（`ModuleDef::furnish`），开放甲板与舱里都行；舱里的货位也够得着（hold_slots.open 变了）。
- 船上摆件不存档（船布局本来也不存）；摆件挡着的改船直接拒绝，不自动收回。

约定：
- 纯算法 `src/ship_cabins.*`：cell_space / cell_side（OPEN/DOOR/WALL/WINDOW/RAIL）/ in_cabin / ShipFloor / ship_prop_problem / ship_props_problem；门窗墙栏尺寸与 LADDER_ALONG、GATE_HALF 只在这里一份，ship.cpp 引用。
- 船上的账 `Expedition::ship_works`（船局部坐标）；人在船上时 `works_at` 返回它、frame = Ship；Ship 用 `set_furniture` 画（样子挂 Visual，碰撞形状直接挂船身）。
- **Player 的 SpringArm3D mask 从 1 改成 3（也撞船）**：舱是空心的，站在甲板上相机会穿进舱墙只看见墙。

坑：
- 开局背包的东西进主区（第 9 格起），e2e 得开背包挪到快捷栏（cabin_e2e 的 `_stock_hotbar`）。
- `RefitBench::aim_spot` 要用过滤后的亮格（压着摆件的落点不亮），不然测试瞄点会瞄到别的探测格。
- Bash heredoc 里嵌 Python 仍会坏（见 [[zhouzhu-refactor-refit-drag]]）：写成文件、用 `<<'PYEOF'` 写到 scratchpad 再跑。

**How to apply:** 验证 = ctest（1706）+ cabin_e2e（开窗 `-- --shots=<dir>`）+ shipyard_e2e / freight_e2e / craft_e2e。相关 [[zhouzhu-refactor-refit-drag]]、[[zhouzhu-extend-interiors]]。
