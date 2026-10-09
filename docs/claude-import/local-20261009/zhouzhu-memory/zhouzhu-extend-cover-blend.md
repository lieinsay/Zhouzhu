---
name: zhouzhu-extend-cover-blend
description: 拓展「地貌交界的过渡」（地表过渡场 CoverBlend，地皮与散布按同一份额渐变）2026-09-26 已快进并入 main（a7a9055）并推送 origin 与 mepro；软最近格而非 B 样条；river_e2e 原有 NaN 报错
metadata:
  node_type: memory
  type: project
  originSessionId: ee40d672-b3d8-4335-a3dd-a9417375c584
  modified: 2026-09-26T09:52:28.113Z
---

2026-09-26 在家里 Windows 上做完，分支 extend-cover-blend（从 extend-resource-fields bca5336 拉出），提交 3db153e..a7a9055，2026-09-26 用户说「并入main并推送」后已快进并入 main（a7a9055，连同 extend-resource-fields 的 9 个提交）并推送 origin 与 mepro。Mellos 页 `extend-cover-blend`；fmap 事件 e40–e42（world-terrain 长出 world-terrain-cover，terrain-tile 长出 terrain-tile-ground，resource-scatter 长出 resource-scatter-ecotone）。

用户原话「地貌缺少过渡表现效果……是硬性过渡，实现正确的过渡效果」。诊断：散布按扭过的最近格一刀切（林缘齐得像剪过），地皮是 100 m 双线性糊开，两者对不上。

- `src/cover_blend.*`：两层扭（350 m ±50、90 m ±40）→ **软最近格**（4×4 格 exp(−d²/τ)，一成到九成 40 m）→ 对数份额加碎噪声 ±20 m；footprint（顶点间距）大时换 B 样条平滑。WorldTerrain::cover 给地皮与散布共用；World.get_cover_at 给脚本。**不动高度**（金标、村落不受影响）。
- 先试的 B 样条 + p 次方收窄被否：孤立一格份额峰值只有 0.44，收窄后整片消失（浮石块 150 → 61）。软最近格一半处就是最近格的边，面积守恒。
- 有河网时地表类「河道」画成滩草（原来是水色，按份额涂开成岸上蓝灰斑）。
- 散布：林缘灌丛带 +0.3×edge、林缘树矮三成；每块 Release 0.58 → 0.86 ms；试过「周围全同类就跳过」的快路，没提速，撤了。
- 目视：`game/tools/cover_shots.tscn -- --shots=<目录>`（自己找九对地表交界）。
- river_e2e 后半段人位置变 NaN、刷 282 条 `!v.is_finite()`——改动前同样，原有问题，未修。小岛崖面一道白竖缝也是原有的。
- 已知：`WorldGround::fertile` 仍按未扭的最近格判；远处无树（LOD 未做）。

**Why:** 下次动地皮、散布或换生成器数据时要知道份额从哪来、哪些报错是原有的。
**How to apply:** 验证 = ctest（core 973 项 + 体检）+ 17 条 e2e + cover_shots 目视。以后直接在 main 上做（[[work-on-main]]）。见 [[zhouzhu-extend-resource-fields]]、[[zhouzhu-extend-terrain-v3]]。
