---
name: zhouzhu-refactor-refit-drag
description: 重构「船节点改装」（拖节点、亮光提示、选中拆除、节点规矩与复合节）2026-09-28 已随 extend-ship-cabins 并入 main（70be1f4）并推送；用户的要求、替用户定的、约定与坑
metadata:
  node_type: memory
  type: project
  originSessionId: 5d13745b-dd1d-4cc7-95ab-3205ee415a24
  modified: 2026-09-28T08:49:33.618Z
---

2026-09-28 从 main 8de6eab 切出分支 **refactor-refit-drag**，提交 80e5b62..0cf8c30 共 7 个（纯算法 / 空艇拼装 / 改装界面 / e2e / 小修 / README / fmap e118），后随 [[zhouzhu-extend-ship-cabins]] 快进并入 main（70be1f4）并推送 origin 与 mepro。验收时 29 条 e2e、ctest 5 项全过。Mellos 页 refactor-refit-drag。
推翻了 [[zhouzhu-extend-ship-3d]] 的键盘光标改装（W/S/A/D/空格/Shift、1/2/3、E 放 / 换）。

用户原话要的：从船节点模块界面拖节点到船上、船亮光提示能放处；能选中已放节点拆除；节点有规矩（帆只能在最上面）；有复合节（前后双节、上下双节等）；允许无功能的空节点，「一般是玩家放建筑等物品的」。

替用户定的（README「替用户定的」里也写了）：八种节点——驾驶区 / 储存区 / 帆 / 浮石舱 / 空节（单节）、双桅帆（前后）、双层货舱（上下）、宽空节（左右）；旧「动力区」拆成帆（只能在最上面，数值照旧→开局 32 m/s 不变）与浮石舱（只能在最下面一层）；12 节上限按节数算；复合节不转向；点一下卡片也能「拿着」；右键拖转视角。在空节上摆东西这一轮没做，随后由 [[zhouzhu-extend-ship-cabins]] 做了（连同能进的舱）。

约定：
- 布局条目 `x,y,z:kind` 的 x,y,z 是锚点（最小角）；`power` 读成 `sail`。规矩位都在 `ModuleDef`，layout_problem 只按表说原因。`ShipLayout::size()` 去掉了，用 cell_count / node_count。
- 纯算法 `refit_pick`：亮格优先（只有船身挡亮格），没指着亮格才用不亮的空格说原因——否则从斜上方指远处亮格会先穿过「驾驶区顶上」那格被挡。
- 测试瞄点：Expedition 的 `get_refit_aim(anchor, kind)`（kind 空串 = 选节点）、`get_refit_card_center`、`get_refit_remove_center`；e2e 要先设 `root.size = 1280×720`。
- 门架在 `Shipyard::GANTRY_RENDER_LAYER`，装配相机不画；相机横移让开左边面板。

坑：右上信息栏里某一行空了会让「拆除」钮跳位、点不着——布局要固定行位；Bash heredoc 里嵌 Python 带引号时又被搞坏过，写成文件再跑（[[zhouzhu-slice6-world-voyage]] 记过）。

**How to apply:** 验证 = ctest（1664）+ shipyard_e2e（开窗 `-- --shots=<scratchpad>` 看 REFIT_* 截图）+ world_features / flat_map。相关 [[zhouzhu-slice3-shipyard]]、[[commit-split-by-topic]]。
