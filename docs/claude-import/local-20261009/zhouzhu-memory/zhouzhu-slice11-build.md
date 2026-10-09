---
name: zhouzhu-slice11-build
description: 切片十一改地形与搭建（方格台阶挖坑垒土、搭建件吸附拼完整的屋、整座样式、按群存 works/<群>.txt）2026-09-28 在 main 上做完未推送；用户四个拍板；地面块留洞+补丁的做法；e2e 的坑（码头边土薄、门口相机、开着的门扇、人不上坎）
metadata:
  node_type: memory
  type: project
  originSessionId: 09da0b1a-ac2b-4d13-83e5-d0f7131c4e9d
  modified: 2026-09-27T19:28:51.346Z
---

2026-09-28 在家里 Windows 上直接在 main 上做（[[work-on-main]]），提交 87dce0a..9187ed0（纯算法 5 个、补齐、营造账接进岛群、玩法、e2e、README、fmap），**未推送**。Mellos 页 `slice-11-build`；fmap 链路 `build`（world-node / terrain-tile / world-terrain / scatter-stream / group-stream / expedition 各长出子节点）。

用户拍板（AskUserQuestion，全选了推荐项）：**方格台阶**（1 m 格、一级 0.5 m、竖直的坎、不打横洞）；**搭建件吸附**（2 m 格、层高 3 m、坡升 1 m，十一种件）；**要料**（挖出土 / 石料、垒扣土、件由配方合，god 不扣不给）；**存进世界存档**（改地形 + 搭建件 + 切片十的摆件，按群 works/<群>.txt）。
另说了没反对：整座样式（仓库 game/structures 草房、民居 + user://structures 另存）；岛群能改地形，甲乙两岛 / 平板岛只搭建摆放（编排自带一份账，不存档）。

- 地面：叶块按「改过的 16 m 块」留洞，补丁按叶块粗格插值（同一条对角线→没改过的格逐位接得上）；洞变了当场重建那一个叶块（一块头一回改 / 改回原样），不变只重画补丁。坎归高的那格的块，隔壁块没改动归这一块；叶块边上让 1 cm（邻块裙边）。
- `WorldTerrain::surface()` 叠改动（线程安全：锁护指针、原子外包框）；散布与采场规划必须用 `surface_natural()`，否则散布的键会随改动变。
- 摆件从 PlaceHand 挪进营造账（宿主局部坐标，实物在 World/Works 下，跟着群走、换锚不挪）；测试查摆件实物用 `Expedition.get_prop_node`。
- E 的先后：门 / 搭建 / 摆放 > 跑货 > 画样台 > 舵 > 挖垒（拿锹镐土时；面前是采掘面或对口的散布物才让给采集）> 采集 > 农事。拿着木锹在河边按 E 现在是挖不是垦。
- 人不会自己上坎（CharacterBody3D 没有 step-up）：地基台基 ≥ 0.45 m，0 层门墙外自动铺台阶 + 斜板（门墙的 bottom 记门外地面，摆的那一刻按地面定）。
- 坑（e2e）：主码头 140 m 内全是崖缘（土层 0.5 m）/ 海，挖坑的地要往里找（到 230 m）；站在门口背对门时相机顺门洞留在门外，Atmosphere 的「屋里」按相机往上打——要往里走、面朝门站；
  门开着时射线穿过门洞打不到东西，要对着过梁；开着的门扇原来点不着（外包盒只罩墙）已修；C++ 单测里 check(被测调用, "…" + 结果) 实参求值次序不定（同 [[zhouzhu-plan-planet]] 记过的）——先调用存结果再拼消息。
- 坑（工具）：`sed -i` 会把 CRLF 文件整个改成 LF（改用 Edit / Python 按原换行写回）；Bash heredoc 里 `\\n` 照旧被吞。
- 已知：粗块（人一两百米外）不画改动；村民导航格不知道后挖的坑；背包不存档（原来就不存）。

**Why:** 下一步（村民住进玩家的屋、楼梯 / 歇山顶、改地形在远处也画、背包存档）从这些约定接着做。
**How to apply:** 验证 = ctest 5/5（core 1585 项）+ 27 条 e2e（多了 earthworks、house）；动地面块、散布、营造账前先读本条。见 [[zhouzhu-slice10-craft]]、[[zhouzhu-refactor-stream-loading]]、[[zhouzhu-plan-planet]]。
