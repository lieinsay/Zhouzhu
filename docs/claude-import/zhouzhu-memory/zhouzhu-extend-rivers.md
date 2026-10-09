---
name: zhouzhu-extend-rivers
description: 拓展「岛群的河」已落地的要点与坑；2026-09-23 已快进并入 main（fcb2b86）并推送
metadata:
  node_type: memory
  type: project
  originSessionId: 0e9b86a4-0656-4a7b-ae29-7742a433c7ca
  modified: 2026-09-23T09:41:45.444Z
---

2026-09-23 落地，分支 extend-rivers（从 main e8764d7 拉出），提交 f1abfe7..fcb2b86，2026-09-23 已快进并入 main（fcb2b86）并推送 origin 与 mepro。

- terrain.bin v2 = v1 + D8 流向码（0x10 = 流出岸缘）+ f32 汇流 km²；weather.tsv 是逐日天气。改导出后要重跑 `tools/export_skyisle.py`。
- 河网与水情都在 `src/world_river.*`（WorldTerrain 构造时建，shared_ptr 共用）；水面网格在 `src/river_mesh.*`；水位公式在 `river_stage` 和 `game/shaders/river.gdshader` 里各写了一份，两边必须一起改。
- 停机坪在河上要让给河：生成器爱把码头放在河口，坪压上去河就没了（踩过这个坑）。
- 调试用的 World 属性：`runoff_override`（≥0 时钉死径流）、`calendar_offset`；查询用 `get_river_at` / `find_river_near`。
- e2e 截图要等 `world.get_pending_count()==0` 再拍，不然粗块会盖住河道。河口从空中用 god 模式看（`player.set_god_mode(true)`）。
- 用户嫌河「像小水洼」后（2026-09-23）按游戏尺度放宽：宽 ×3、深 ×2，河床改平底，只有源头小溪断流，开局 calendar_offset=3 对准雨季。教训：按真实水文算出来的 3–9 m 的河，在第三人称下看不见；而且验收要按真实开局的钟点拍，不能钉住径流去拍大河。
- 村落金标因地形变动重记过（现为 0xd1230999ae5ee931）。

**Why:** 下次改地形 / 水 / 岛群导出时要知道这些约定。
**How to apply:** 动水位公式就两边一起改；换岛群时重跑导出，并用 `site_survey --rivers` 画图检查。相关 [[zhouzhu-refactor-world-2051]]、[[zhouzhu-slice7-world-sim]]。
