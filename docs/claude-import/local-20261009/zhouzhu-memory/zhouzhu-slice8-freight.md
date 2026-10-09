---
name: zhouzhu-slice8-freight
description: 切片八跑货 + 拓展船台（落地靠港）已落地在分支 slice-8-freight，未推送；每个村都有船台、没有港口村；e2e 全程不瞬移
metadata:
  node_type: memory
  type: project
  originSessionId: 886100a2-d105-4731-8537-59e2396184f4
  modified: 2026-09-24T03:12:01.591Z
---

2026-09-24：切片八（村152 打包 → 装船 → 飞到村127 交货）与拓展「船台」都在分支 slice-8-freight 上，未推送、未并入 main。

- 用户否掉了「港口村」：船随便哪儿都能停，船台是每个村（非散户）的平常基础设施；委托开到别的岛上最近的村（村心到村心）。
- 船台 = 建筑表 `berth`（非房屋 `house=false`：无模板/屋里，村民不串门）；纯算法 land_berth.*；Settlement 进 "settlements" 组，编排在 400 m 内找船台当泊位。
- 村仓在船台跳板脚前（POST_Z）；ship_problem 拒绝宽于一列 / 半长 > 22 m 的船。
- 有意的变化：邑治 273 → 274 座；分桶 40 → 60 m；金标「岛群远离主码头的村」重录；开局船 20 m/s。
- 提交 1c821d2..43f265b。船慢到 20 m/s 后：e2e 驾船要推到 30 m 内（靠惯性滑不进 40 m 泊位范围）；world_sim 在乙岛只待到 11 点（回程近 5 个钟头）。
- e2e 坑：HUD 在编排的物理帧里刷新、比测试节点晚一帧，按 E 前等提示跟上。

**Why:** 用户嫌「抱着箱子走一公里」太扯，见 [[feedback-no-long-carry-walks]]。
**How to apply:** 新的跑货/靠港玩法以船台为落点；相关的 [[zhouzhu-slice7-world-sim]] 账本仓就是村仓。
