# core/ 工作约定（Claude 上下文）

这是游戏本体的后端：`zhouzhu_core`，一个**零引擎依赖**的 C++20 纯逻辑库。
**先读本文件，再读 `docs/PLAN-CORE.md`（两条轴、底层清单、引擎分界线、口径表）。**

上游：`../docs/10-技术选型.md`（选型与理由）、`../docs/01-设计铁律.md`（硬约束，优先于一切）、
`../docs/06-社交系统.md`（账本 / 棱镜 / 处境的需求）、`../docs/07-核心循环.md`（三层空间、网格建造、物流）、
`../docs/09-首版切片.md`（范围与验证目标）、`../docs/DECISIONS.md` D73–D76。
架构讨论的来由见 `docs/HANDOFF-core-2026-09-19.md`。

> **状态：全部是设计稿，一行代码都还没写。** 86 条待拍板口径散在六份稿子里（见下），
> 按项目惯例每条都有默认值，可以整份「按默认」。拍完才动手。

## 两条轴（最要紧的一条约定）

**`zhouzhu_core` ≠ 底层。** 这两条轴是正交的，混着说必出错：

| | 轴一：引擎依赖 | 轴二：底层 / 业务 |
|---|---|---|
| 分什么 | 换引擎时要不要重写 | 会不会随设计与内容改变 |
| 两端 | `zhouzhu_core` ↔ 前端（UE / 终端） | L0/L1 机制 ↔ L2/L3 业务 |
| 判据 | 输出进不进存档、影不影响重放 | **域无关、永不变** → 底层 |
| 守门的 | `CORE-boundary`（`client_term`） | `CORE-layer`（独立测试 + 源码扫描） |

账本就是例子：不依赖引擎（轴一 core 侧），但它是业务（轴二 L2）。

> **底层由「这是一台什么机器」决定，不由「这台机器现在跑什么内容」决定。**
> 别再拿 `docs/06` / `07` / `09` 去推底层——那是业务需求文档，只能推出「当前内容需要什么」。

## 分层

```
L4  facade/       intent / query / patch 窄口
L3  src/world/    World 组装、intent 分派、系统之间的接线
L2  src/rules/    账本 · 处境 · 田块 · 货舱 · locale 来源 · 文化特征表 …
L1  src/mech/     时间 · 存储 · 日志 · 事件 · 空间 · 数值池 · 随机 · 渲染分派 · 场查询
L0  src/prim/     Day · EntityId · 定点数
```

- **依赖只许向下。L1 不知道 L2 存在。**
- L1 只认：**天、序号、句柄、整数 `kind`、整数格、整数量、位掩码**。
- 污染的八个反例见 `docs/PLAN-CORE.md` §4.3。典型：`Clock` 里出现 `ledger.decay()`、
  `EventKind` 枚举放进 L1、`Space::can_place` 里判「床要靠墙」、`Pool::apply` 里有抗性公式、
  `Render` 里硬写「斗 / 石」。

## 设计稿速查

| 稿子 | 覆盖 | 口径 |
|---|---|---|
| `docs/PLAN-CORE.md` | 两条轴 · 分层 · 底层清单十系统 · 业务清单 · 引擎分界线 C1–C8 · facade · 两条流 · 存档与随机 · `CORE-*` 八条 | 21 |
| `docs/PLAN-ENTITY.md` | L1 #2 存储：`EntityId` · `Identity` · `Store<T>` · 物品的量与个体 | 15 |
| `docs/PLAN-CLOCK.md` | L1 #1 时间 · #3 日志 · #4 事件：`Clock` · `Timeline` · `Journal<T>` · `EventBus` | 16 |
| `docs/PLAN-SPACE.md` | L1 #5 空间：占用 · 通行 · 放置事实 · 连通性 | 10 |
| `docs/PLAN-POOL.md` | L1 #6 数值池（伤害的底层形态） | 11 |
| `docs/PLAN-PRISM.md` | L1 #8 渲染分派 · #9 场查询 | 13 |

L1 #7 随机与 #10 存档在 `PLAN-CORE` 第十一节，没有独立稿。

## 写代码时必须遵守

1. **确定性三纪律**（`PLAN-CORE` §11）：
   - 不遍历 `std::unordered_map` / `unordered_set`——顺序是实现定义的，**静默**分叉重放
   - 不用 `<random>` 的分布——跨标准库实现结果不同；用 `entity_rng(seed, stream, key)` + 自写分布
   - 进存档的量全是整数或定点；浮点只留表现层
2. **中性数据禁令**（D75）：量 / 日期 / 地名 / 物名**不许以成品字符串存下来或写进 UI**。
   唯一产出字符串的地方是 `render`，而它的语序与量词来自内容模板，不来自代码。
3. **L1 不得 include `../rules/`**，不得出现业务词表里的标识符。
4. **索引都是派生品**，不进存档，加载后重建（占用表、空间索引、`recent()` 环形缓冲）。
5. **facade 只收发 POD + 字符串**，不得出现内部类型、引擎类型、文件路径与句柄。
6. `query` 一律带 `observer`——没有 observer 的查询等于绕过棱镜。
7. 新增口径：写进对应设计稿的口径表并给默认值，别散在代码注释里。

## 依赖

只两行：`nlohmann/json`（单头）+ `doctest`（单头）。
沿用 generator「不引入 scipy/networkx/pandas」的纪律——**加新依赖要有理由**。
构建 CMake + MSVC 先跑 Windows，但从第一天保持 clang 能编（纯标准 C++，不用 MSVC 扩展）。

## 计划中的命令

```
zhouzhu_check          CORE-* / CLK-* / ENT-* / SPC-* / POOL-* / PRM-* 全部校验，进 CI
zhouzhu_srv            stdio JSON 服务（调试与工具）
client_term            终端客户端 —— 它同时是引擎边界的自动校验器
```

⚠️ `client_term` 不是省事的原型替代品，**是架构的一部分**：它没有碰撞体、没有 navmesh、
没有骨骼、没有 UI 框架，所以**一条规则在它里面跑不出正确结果，就说明已经漏到前端去了**。

## 与 generator 的接口

`slice/` 是 generator 导出的两个节点（地形与聚落 · 逐日天气 · 人口与政治 · 航线成本 · 九格表）。

- ⚠️ **generator 是 Python，别让 C++ 读 npz**——Python 侧导出 JSON + 裸二进制栅格（带小 header）。
- ⚠️ **slice 在一台机器上导出一次并提交，不要各机各导。** 同机重跑逐字节稳定，
  但 Windows 与 Linux 之间少数 float64 数组有极小差异（`check` 全过、`world.json` 一致）。
  各导一份会让重放一致性被**内容层面**的差异污染，这种 bug 极难定位。
- 文化场值在导出时**量化成定点整数**（`PLAN-PRISM` 口径 j），C++ 侧永不见浮点。
- slice 要带 generator 的 seed 与 `STAGE_VERSIONS`，加载时对不上直接报错。

## 两个判据，别混为一谈

**「底层活了」**——在一个没有任何游戏概念的测试里跑通：造钟、造格、造池、造日志 →
推进一千天 → 排期级联与取消 → 放置与连通 → 一串带来源的增减 → 压缩后 fold 不变 →
存档往返逐字节一致 → 换个编译器重放结果一样。

**「业务活了」**——在终端客户端里跑通：装货起航 → 过 9 天 → 到邻邦 → 铁匠提起你上次答应会再来 →
第三幕开关一拨 → 同一袋谷的显示从「三石七斗」变成官定说法，括号里的换算消失。

**底层活了要先于业务活了。** 反过来做，底层就是从业务里刮出来的，一定是脏的。

## 提交约定

提交信息用中文（沿用 generator 的习惯）。`build/`、`out/` 不提交。
实施开始后，踩坑与实测数字写进 `docs/DESIGN-NOTES.md`（照 `generator/docs/DESIGN-NOTES.md` 的格式）。
