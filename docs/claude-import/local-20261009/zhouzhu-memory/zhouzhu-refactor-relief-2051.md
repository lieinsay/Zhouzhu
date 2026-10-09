---
name: zhouzhu-refactor-relief-2051
description: "重构「按生成器台面口径的真实起伏重做 2051」2026-09-25 在 refactor-relief-2051 上 8 个提交未推送；terrain.bin v4（f32 高程）；码头从泊场推（主码头 #1、目的 #26）；资源点要够得着船；e2e 的新村号与门架坑"
metadata:
  node_type: memory
  type: project
  originSessionId: cdd37658-2992-49ea-be12-bc01f481417b
  modified: 2026-09-25T12:46:41.900Z
---

2026-09-25 在家里 Windows 上做完，分支 refactor-relief-2051（从 slice-9-gather ee58bf3 拉出），提交 f4c5cc8..f873290，2026-09-25 已随切片十快进并入 main 并推送 origin 与 mepro。Mellos 页 `refactor-relief-2051`；fmap 用 extend 记（导出 → 码头从泊场推，资源点 → 够得着船）。

- 数据源：skyisle-gen 的台面口径（height_m = 主岛陆地中位数，峰 2,027 m、有常年河）改动**还在它的工作区没提交**（分支 island-rivers-resources-settle）；用的是 out/seed42/islands/2051（19:50 那次跑的）。岸线与旧版逐格一致。
- **terrain.bin v4**：高程 f32。v3 的 u16 × 100 封顶 655.35 m——第一次导出时山全被削成平顶，截图里「最高处」只有 667 m 才发现。
- 用户拍板：**码头按新泊场重排**。导出：离岸 1 km 内的泊场 → 同岛最近岸缘格，2 km 内并一个，主泊场（仓场）= 主码头 #1（邑治北 400 m）；67 个码头。专业聚落当村（村号 161–215），镇的市户并进镇所在的村（邑治 409 户、412 座）。
- 资源点规划加「够得着船」：采掘面 60 m 内开局的船野泊得下（resource_reach，先 landing_corners_ok），不行就 150 m 内往外挪、再试 24 处；并行找。降细节噪声倍率对落地率**没用**（卡在生成器本身的陡坡）。覆盖 97.3%。
- 新测试对：跑货 村031 → 窑村37；远近分层 村 A 村031、远处 村004；跑船去村004（8.5 km）再飞 #26（23 km）；world_features 用村031；金标换成 村126/40/120 + 散户175。
- e2e 坑：主码头船坞门架柱 20 m 高、离泊位近，出港要先升过门架再转向；邑治一大片房子，贴房顶飞会挂住；**黏土坑是平坑，船能落**——测「石头挡着」要挑浮石崖（find_resource_near 的 kind 参数）。
- 坑（工具）：Bash 工具会把命令里的 `\\` 吞成 `\`（heredoc 里 Python 的 `"\\n"` 变成真换行、`grep '\\'` 报 Trailing backslash）——含反斜杠的 C/GDScript 改动用 Edit 工具，或在脚本里用 chr(92)。
- 远处账房 1,206 处：单线程开账 17.8 s（Release），游戏里后台线程由近及远开。

**Why:** 下一步（并入 main、再换生成器版本、泊场玩法）要知道这些约定与坑。
**How to apply:** 再换生成器数据就重跑 `tools/export_skyisle.py <岛群目录> game/worlds/2051` + site_survey + ctest + 16 条 e2e；以后直接在 main 上做（[[work-on-main]]）。见 [[zhouzhu-extend-terrain-v3]]、[[zhouzhu-slice9-gather]]、[[planet-generator-split-out]]。
