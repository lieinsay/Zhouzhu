---
name: zhouzhu-core-architecture
description: (作废存档，2026-09-20 推倒重开) Zhouzhu 游戏底层曾拍板：前后端分离、纯 C++ core、UE 前端、整数 Placement
metadata:
  type: project
---

⚠️ **2026-09-20：仓库已推倒重开，下面这些属于被作废的那一版设计，引用前先跟用户确认。**
用户的原话是项目里「存在了很多不可控的设计」，core/ 的六份设计稿（含 PLAN-CORE）连同
docs/ 的全部设定一起删掉了，新仓库只剩一段项目描述（见 [[zhouzhu-repo-restarted]]）。
下面记的是 2026-09-18 拍板的那一版游戏本体架构，存档在 github.com/lieinsay/Zhouzhu：

- **前后端分离**，分离靠 facade 的窄口（intent / query / patch）保证，不靠进程边界；传输是第二层包装（进程内直链 / stdio JSON / GDExtension / UE plugin）。
- **core 是纯 C++**，零引擎依赖：不许出现 `UObject` / `AActor` / `UActorComponent` / `FTransform`（UHT + GC 在 UE 外编译不了，会毁掉快速测试与 CI）。抄组合式建模的**形**，不抄类型。依赖只 nlohmann/json + doctest。
- **3D**，前端 UE。理由：生成器已出高程/地表/坡度栅格，3D 地形近乎免费；贵的是角色与建筑，靠风格化压。这**推翻了 `docs/10-技术选型.md` 的 2D 结论**。
- **位置用整数 `Placement`**（岛号 + 格 + 定点格内偏移 + 八向 facing + layer），不用浮点 transform。两级网格：岛屿 100 m（来自生成器）、地点局部 1 m（主家/前哨/船内，供网格建造）。连续坐标只在前端，不进存档。
- **硬线**：后端不许有「速度」和「碰撞」两个概念，也不做寻路；NPC 日程是语义位置，中间那段路由前端补间。
- 存档 = 种子 + intent 流 + 周期快照，可重放。确定性三纪律：不遍历 `unordered_map`、不用 `<random>` 的分布、进存档的量一律整数/定点。
- 棱镜渲染**在后端**：量/日期/物名中性存储，输出 `{text, raw, unit, gloss}`；第三幕的度量/文字/历法统一 = 换 locale 表。

**为什么**：用户明确要引擎可换（先 Godot 后 UE），且不想让后端代码依赖引擎。
**怎么用**：写 PLAN-CORE.md 时按此展开；同时要改 `docs/10-技术选型.md`（2D 作废）与 `DECISIONS.md`（加前后端分离 + 3D）。

相关：[[zhouzhu-repo-restarted]]、[[zhouzhu-mepro-dev]]
