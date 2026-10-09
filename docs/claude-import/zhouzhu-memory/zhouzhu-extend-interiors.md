---
name: zhouzhu-extend-interiors
description: 拓展「房子能进去 + 开发者房屋模板（室内布局）」2026-09-23 落地于分支 extend-interiors（已并入 main 并推送）；用户的决定、约定与坑
metadata:
  node_type: memory
  type: project
  originSessionId: 652ed1be-e1e3-4b9e-aa1b-60553933412b
  modified: 2026-09-23T08:38:42.272Z
---

2026-09-23 在分支 extend-interiors 上 7 个提交 78d9fd7..e8764d7（纯算法 / 七套模板 / 模板库+Settlement+相机 / 室内编辑 / e2e / README / fmap），2026-09-23 已按用户要求快进并入 main（e8764d7）并推送 origin 与 mepro。Mellos 页 extend-interiors。推翻了 [[zhouzhu-extend-scale]] 里「先只做摆设、进不去」。

用户拍板：游戏内室内编辑器 + 我先做几套；四种建筑都能进；**屋里不揭顶、保持第三人称**（用户说揭顶不好看）；家具只是摆设、村民照旧进门就藏。

约定：
- 外壳 = BuildingDef（占地不变，望楼 levels=3）；模板 = game/buildings/<外壳>/<款式>.txt；清单行 `kind/style@x,z,yaw°`，无款式按 building_id 哈希在 auto 模板里挑；另存的模板写 `auto no`。
- 模板库 autoload `BuildingTemplates`（类 TemplateLibrary），测试用 `write_dir` 写 user://。
- 天花板带碰撞 + Player 头顶 6 m 有东西就把弹簧臂收到 2.6 m（indoor_arm）；站在门口背对门时相机顺门洞留在门外——测试别在门口查相机。
- 键：布置模式 V/Z 换款式、T 开室内编辑；编辑器 Z/V 工具、Q/R 转、空格/Ctrl 换层、E 放（隔墙两点）、X 拆、N 另存、F5 存。

坑：
- 面朝梯子一走进爬梯区域就开始往上爬：e2e 要停在梯前 1.2 m 再「climb」。
- `--check-only --script` 不加载 autoload，引用 BuildingTemplates/GameClock 的测试脚本会报找不到——直接跑场景。
- Settlement 碰撞是每座一个 ConcavePolygonShape3D（开了两面碰撞）；fmap 不许父节点连自己的子节点。

**How to apply:** 村民进屋、家具可用、坡道算进占地是可能的下一步；验证用 ctest（661）+ 13 条 e2e（含 interior_e2e）。相关 [[zhouzhu-slice4-settlement]]、[[zhouzhu-slice5-villagers]]。
