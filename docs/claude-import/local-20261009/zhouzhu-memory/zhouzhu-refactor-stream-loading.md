---
name: zhouzhu-refactor-stream-loading
description: 重构「流式加载不卡」（后台任务池、村子简模/细模/村民三层分帧挂、瞬移只挂落点跟前、散布着色器 LOD + 远处林子、共用材质）2026-09-26 在 main 上 5bc7bbc..48c1104，已推送 origin 与 mepro；卡点是村子不是植被；Bash 吞反斜杠要用 Write 写补丁脚本
metadata:
  node_type: memory
  type: project
  originSessionId: 18744680-b1a7-411e-aab2-b6af8725cdad
  modified: 2026-09-26T12:22:47.214Z
---

2026-09-26 在家里 Windows 上直接在 main 上做（[[work-on-main]]），提交 5bc7bbc..48c1104，2026-09-26 用户说「推送」后已推送 origin 与 mepro。Mellos 页 `refactor-stream-loading`；fmap 事件 e43–e49。

用户原话「快速移动时加载植被、单位等数据导致卡住 1 秒，需要正确的加载方式，渲染也需要 LOD；优化资源加载」。实测（stream_perf + World 的分项计时）：卡的是**村子**——走近邑治那一帧主线程铺 410 座房子 242 ms、当场开账 569 ms、算导航格 337 ms；植被本身不卡。瞬移也卡 1–1.3 s。

- 架构：`JobPool`（World 持有，4 线程，优先级 = 离焦点米数）收了地面块、水面、散布、村子备料（VILLAGE）、一格细模（CELL）、导航格（NAV）、远处林子（FAR_TREES）。SiteStream：6 km 挂简模 WorldVillage（Settlement deferred，只有数据）、格子 96 m 离焦点 300 m 后台备细模一座座挂、900 m 账本（Hinterland::request）+ 导航格齐了才招人、每帧 3 ms。瞬移 ensure_near 只当场挂落点 150 m 内的村、120 m 内格子。
- 散布：`game/shaders/scatter.gdshader` 按实例距离互补抖动淡入淡出（实例参数 fade，eye 每帧设）；树 200 m 细模→低模→800 m 远处林子（`ResourceScatter::far_trees`）→4 km。
- 结果（Debug，窗口）：150 m/s 最慢 368 → 26 ms；瞬移那一帧村子 1.0–1.3 s → 1–2 ms。
- 坑：叶块碰撞 `CollisionShape3D::set_shape` 与 `set_faces` 各几毫秒 → 碰撞切段分帧挂；4.4 的 Shape3D 没有 debug_color。
- 坑：Bash 工具吞 `\`（`\n` 变真换行），改 C++ 的 Python 补丁脚本一律用 Write 写到 scratchpad 再跑；仓库 autocrlf=true、多数源文件 CRLF，补丁脚本按原换行写回。
- 没做：账房单帧 6～11 ms（远处结算读主线程农田、写认领表，挪后台有竞争）。

**Why:** 以后动流式加载、村子、散布、Settlement 时要知道谁在后台、谁在主线程、预算在哪。
**How to apply:** 验证 = ctest（core 1025 项 + 体检）+ 17 条 e2e + `game/tools/stream_perf.tscn`（看慢帧分项）+ `game/tools/village_shots.tscn` 目视。改前先跑 stream_perf 量。见 [[zhouzhu-extend-cover-blend]]、[[zhouzhu-slice7-world-sim]]。
