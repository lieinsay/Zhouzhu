---
name: zhouzhu-slice6-world-voyage
description: "切片六（岛群 #2051 跑船、码头与村落按生成器全自动落地）已落地；分支、用户的决定、测试与坑"
metadata:
  node_type: memory
  type: project
  originSessionId: 61944fb7-f763-4c54-9494-cf2b758f5cce
  modified: 2026-09-23T02:26:02.549Z
---

2026-09-23 在分支 slice-6-world-voyage 上 15 个提交（77ad0fb 之后）；同日随切片七快进并入 main（6e9a505）并推送，分支已删。

用户拍板：出生改在主码头 #7 旁（邑治 8.5 km 外照样盖、有人）；速度要分档（泊/巡/疾，R/F）；一户一座民居、村民每村封顶 40；91 码头 / 180 村 / 191 散户全做、按距离生成；目的码头 #2（30 km）。
用户明确：**不要手摆村落，要靠谱的工具**，场景会变大 → 全程序化（plan_dock / plan_village）+ 离线体检工具 zhouzhu_site_survey（ctest 也跑）。

**Why:** 以后换更大的岛群只重新导出数据、跑体检，不改代码。
**How to apply:** 新点位字段走 tools/export_skyisle.py → sites.tsv（纯 C++ 读），不要让游戏读 world.json 以外的 skyisle-gen 产物；聚落/村民挂在 SettlementHost 下（Island、WorldVillage）。

坑：
- Bash heredoc 里含 `\n` 或某些引号的 Python 常被搞坏 → 写成 scratchpad 文件再跑。
- GDScript 对无类型数组元素用 `:=` 会解析失败，而 Godot 此时不退出、空转到 timeout → 先 `--check-only --script` 再跑 e2e。
- voyage_2051_e2e 无头约 6–7 分钟墙钟（模拟 ~390 s），跑时输出写文件再监视。
- 测试里当前场景是驱动器节点，别用 get_current_scene 判断场景类型（已改用 world_scene 组）。

相关：[[zhouzhu-refactor-world-2051]]、[[zhouzhu-slice5-villagers]]、[[commit-split-by-topic]]
