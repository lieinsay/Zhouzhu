# 世界钟与事件日志设计稿：只认天、序号和整数的一层

> 状态：**设计稿，未实施**（2026-09-19）。上游 `PLAN-CORE.md` 第 4.1 / 4.7 节与第八节（两条流）。
> 与 `PLAN-ENTITY.md` 同属 **L1 机制层**（见第二节分层）。
>
> 写给一段时间没看项目的人：**先读二、三两节就够决定**——第二节是分层与防污染的硬规则，
> 第三节是口径表；第四节起是设计与验收。
>
> **本稿的全部要点可以压成一句话**：这一层只认天、序号、句柄和整数 `kind`，
> 它不知道什么是账本、什么是承诺、什么是收成、什么是第三幕。

---

## 一、要解决什么

`PLAN-CORE` 的原语一与原语二是整个 core 的中枢：

> 世界钟 —— 作物、账本腐坏、处境时间线、风评传播、改制事件**全是订阅者**。玩家不在场时照推。
> 只追加事件日志 + fold —— 账本、处境、风评、改制**共用一种存储**。

「全是订阅者」「共用一种存储」这两句话的意思是：**这两个模块会被所有业务系统调用。**
也正因如此，它们是整个 core 里**最容易被业务污染**的地方，而且污染之后最难拆——
等到账本、处境、农事、改制四个系统都往世界钟里塞了自己的东西，再想换实现就是重写。

所以本稿的任务有两个，第二个比第一个重要：

1. 把世界钟、排期器、日志、事件总线设计出来。
2. **定一条防线，让业务概念在编译期就进不来。**

---

## 二、分层与防污染

### 2.1 五层

```
L4  facade        intent / query / patch 窄口
L3  world         World 组装、intent 分派、系统之间的接线
L2  rules         Ledger · Predicament · Prism · Culture · Field · Cargo …
L1  mech          Clock · Scheduler · Journal<T> · EventBus · Store<T>
L0  prim          Day · EntityId · 定点数 · 基本容器约定
```

- **依赖只许向下。** L1 只能 include L0。
- **L1 不知道 L2 存在。** 这是本稿的核心约束。
- `Store<T>`（组件存储）已在 `PLAN-ENTITY` §3.3 定义，同属 L1，本稿不重复。

### 2.2 硬规则

> **L1 的任何头文件、任何实现文件，不得出现 L2 及以上的标识符。**
>
> L1 只认四样东西：**天（`Day`）、序号（`uint64_t`）、句柄（不透明整数）、整数 `kind`**。

### 2.3 什么叫污染（反例）

这些写法一眼合理，全是污染：

| 反例 | 为什么是污染 | 正确做法 |
|---|---|---|
| `Clock::advance()` 里调 `ledger.decay()` | 世界钟知道了账本 | 账本自己排期，世界钟只派发句柄 |
| `enum EventKind { PROMISE_BROKEN, HARVEST, … }` 放在 L1 | 总线认识了「承诺」 | `kind` 是 `uint32_t`；枚举住在 L2 |
| `Journal::is_promise(entry)` | 日志认识了条目语义 | 日志只知道「有日期的一行」，判断归 L2 |
| `Scheduler` 里有 `PRIORITY_HARVEST` 常量 | 排期器知道了农事 | 同日顺序由 `(day, seq)` 决定，没有优先级概念 |
| `Clock` 里有 `bool act3_started` | 世界钟知道了幕 | 幕是 L2 的状态，它自己订阅世界钟 |
| `EventBus` 给某些 kind 特殊处理 | 总线有了业务分支 | 总线对所有 kind 一视同仁 |

### 2.4 这条防线是自动校验的

和 `PLAN-CORE` §5.5 的 `client_term` 是同一个思路——**让边界有东西守着，不靠自觉**：

> **正面校验**：L1 有一组只 include L0 的独立测试。里面造一个 `Clock` + `Scheduler` +
> `Journal<int>` + `EventBus`，推进一千天，跑排期、级联、取消、压缩、订阅。
> **这组测试不 include 任何 L2 头文件，也不出现任何游戏概念。**
> 它跑不起来，就说明 L1 已经离不开业务了。
>
> **反面校验**：源码扫描 `src/mech/` 与 `src/prim/`，不得 include `../rules/`，
> 不得出现业务词表里的标识符（`ledger` / `promise` / `crop` / `culture` / `ship` …）。

两条合起来是第五节的 `CORE-layer`。

---

## 三、需要拍板的口径（有默认值，可直接说「按默认」）

### 世界钟与排期

| # | 问题 | 默认 | 说明 |
|---|---|---|---|
| a | `advance(n)` 怎么实现 | **逐日循环**，`advance(n)` 严格等于 n 次 `advance(1)`。**不做任何快进优化** | `CORE-time` 要的就是这个等式。任何「这十天没事，跳过去」的优化都是它破裂的地方 |
| b | 同日到期的派发顺序 | **按 `(day, seq)`**，`seq` 是排期时取的单调序号 | 堆里同键的顺序是实现定义的，必须自己给全序。**这是本模块最可能出确定性问题的一处** |
| c | 同日级联（回调里又排了当天的事） | **允许**，drain 到空；**深度上限 64，超限断言** | 不设上限就是死循环挂死；断言让它变成可定位的错误 |
| d | 取消已排期 | **墓碑**（惰性删除），派发时跳过 | 从堆里摘除麻烦且易错。处境时间线分支会频繁取消 |
| e | 历法参数从哪来 | **slice 的 `planet.json`**（336 日 = 4 季 × 3 月 × 28 日）；`Clock` 只做换算 | 历法是行星常数，不是代码常数。`almanac.py` 已经算好，别在 C++ 里重推 |
| f | 日内时间 | **世界钟不推进日内时间**，最小刻度是 1 日 | 「辰时 / 午时」只是 NPC 日程纯函数的参数（`PLAN-CORE` 口径 j / t） |
| g | 时间能不能回退 | **不能**。没有 rollback，读档 = 从快照重建 | 有回退就要给每个系统写反向操作，那是另一个数量级的复杂度，且与重放模型冲突 |

### 日志与事件

| # | 问题 | 默认 | 说明 |
|---|---|---|---|
| h | `Journal` 与「事件日志」是不是一回事 | **不是，是两样东西**（见 4.3 / 4.4）：`Journal<T>` 是**状态**、进存档；`EventBus` 的环形缓冲是**派生品**、不进存档 | `PLAN-CORE` §八 的两条流在这里落地 |
| i | `Journal` 能不能乱序追加 | **不能**，`day` 必须非降序，违反即断言 | 只追加的前提就是时间不倒流；乱序会让 fold 与压缩都失去意义 |
| j | 条目 id 从哪来 | **一个全局单调计数器，与 `EntityId` 分开**，进存档 | `Provenance.entry`（`PLAN-ENTITY` §3.6）要能无歧义地指向一条条目。条目不是实体，不该占 `EntityId` 号段 |
| k | fold 谁来写 | **业务给折叠函数**，`Journal` 不知道折叠出什么类型 | `Journal<T>` 连 `T` 是什么都不该知道 |
| l | 压缩策略归谁 | **业务给**；`Journal` 只保证「压缩后 `day` 仍非降序、id 仍单调」，并**提供 fold 不变量的校验夹具** | `CORE-fold` 验的是「策略 + fold」这一对，不是 `Journal` 本身。但夹具该由 L1 提供 |
| m | 事件的形状 | **定长 POD**：`{day, kind, subject, object, a, b}`，不用 JSON、不用变长 payload | 一局几十万条。变长 payload 会带来分配、序列化与确定性三重麻烦；两个 `int64` 够用，不够就说明该记成 `Journal` 条目 |
| n | `kind` 枚举住哪 | **L2**。L1 只见 `uint32_t` | 2.3 第二条 |
| o | 订阅者派发顺序 | **注册顺序**；注册全部发生在世界构造期，顺序由构造代码固定 | 不用优先级——优先级会诱使人往 L1 塞业务常量（2.3 第四条） |
| p | 事件进不进存档 | **不进**。环形缓冲容量固定（默认 4096 条），只为调试与 `patch` 推送 | 派生品，重放即可重建 |

---

## 四、设计

### 4.1 `Clock`：一个计数器加一张历表

```cpp
struct Day { int32_t v = 0; auto operator<=>(const Day&) const = default; };

struct Calendar {            // 从 slice 的 planet.json 读入，不是代码常数
    int32_t days_per_month;  // 28
    int32_t months_per_season;
    int32_t seasons_per_year;
};

struct YMD { int32_t year, season, month, day_of_month; };

class Clock {
public:
    Day  now() const;
    YMD  to_ymd(Day) const;        // 纯函数
    Day  from_ymd(YMD) const;      // 纯函数
private:
    Day       now_;
    Calendar  cal_;
};
```

**`Clock` 里没有 `advance`。** 推进是 `Timeline` 的事（4.2）——
因为推进必然伴随派发，而派发需要排期器；让 `Clock` 保持成一个可以随便拷贝的纯值。

⚠️ 历法**不反推任何东西**（沿用 `almanac.py` 的纪律）。它只是换算。

### 4.2 `Timeline`：钟 + 排期器 + 逐日循环

```cpp
using Handle = uint64_t;       // 不透明。L1 不知道它意味着什么

class Dispatcher {             // 由 L3 实现，L1 只见这个接口
public:
    virtual ~Dispatcher() = default;
    virtual void fire(Handle, Day) = 0;
};

class Timeline {
public:
    Day  now() const;
    Handle schedule(Day when, Handle what);   // 返回取消用的票
    void   cancel(Handle ticket);
    void   advance(int32_t days, Dispatcher&);
private:
    Clock    clock_;
    uint64_t seq_ = 0;                        // 同日全序（口径 b）
    std::vector<Entry> heap_;                 // 键 = (day, seq)
    std::vector<uint64_t> cancelled_;         // 墓碑，有序（口径 d）
};
```

`advance` 的实现**只有一个形状**，不许有第二种：

```
advance(n):
    重复 n 次:
        now_ += 1
        depth = 0
        while 堆顶.day <= now_:
            取出堆顶；是墓碑就丢弃，continue
            dispatcher.fire(handle, now_)
            if ++depth > 64: 断言失败      // 口径 c
```

三处要命的地方，每一处都有对应的校验：

- **逐日循环不许优化**（口径 a）→ `CLK-daily`
- **同日顺序靠 `seq` 而不是堆的稳定性**（口径 b）→ `CLK-order`
- **级联有上限**（口径 c）→ `CLK-cascade`

> `Timeline` 不知道 `Handle` 是账本的腐坏、处境的下一节点，还是第三幕的第二件改制。
> 它只是把一个整数在正确的那一天交还给 `Dispatcher`。**这就是「足够底层」的意思。**

### 4.3 `Journal<T>`：只追加、带日期、可折叠

账本、处境、改制共用的那个存储（原语二）。**是状态，进存档。**

```cpp
template <class T>
class Journal {
public:
    uint64_t append(Day, const T&);            // day 非降序，违反即断言（口径 i）
    std::span<const T> since(Day) const;       // 二分，day 有序
    template <class Acc, class F>
    Acc fold(Acc init, F&& f) const;           // f 由业务给（口径 k）
    template <class Policy>
    void compact(Policy&&);                    // 策略由业务给（口径 l）
private:
    std::vector<int32_t>  day_;                // 非降序
    std::vector<uint64_t> id_;                 // 单调
    std::vector<T>        entry_;
};
```

三个 `vector` 平行，不是 `vector<struct>`——因为 `day_` 要能直接二分，
而且序列化时三个定型数组比一堆对象干净。

**`Journal` 对 `T` 一无所知**：不知道它是恩情还是承诺，不知道轻重，不知道该不该腐坏。
腐坏是业务写在 `fold` 的折叠函数里的事。

#### 压缩与 `CORE-fold` 的夹具

`CORE-fold` 要的是「压缩前后 fold 结果不变」。这是**策略与折叠函数这一对**的性质，
`Journal` 保证不了——但它该提供验这件事的夹具，因为这是机制层的事：

```cpp
template <class T> template <class Acc, class F, class Policy>
bool Journal<T>::fold_invariant_holds(Acc init, F&& f, Policy&& p) const {
    Acc before = fold(init, f);
    Journal copy = *this; copy.compact(p);
    return copy.fold(init, f) == before;      // 逐字节比较
}
```

业务只要把自己的 `fold` 与 `compact` 递进来，`CORE-fold` 就自动覆盖到它。
**这是 L1 能给 L2 的最有价值的东西：不是替它做事，是给它一把尺子。**

### 4.4 `EventBus`：广播，不是账本

```cpp
struct Event {
    int32_t  day;
    uint32_t kind;             // L2 定义，L1 不认识（口径 n）
    EntityId subject, object;  // 谁做的、对谁
    int64_t  a, b;             // 含义由 kind 决定（口径 m）
};

class Listener { public: virtual void on(const Event&) = 0; };

class EventBus {
public:
    void subscribe(Listener*);         // 构造期注册，顺序即派发顺序（口径 o）
    void publish(const Event&);        // 同步派发 + 写进环形缓冲
    std::span<const Event> recent() const;   // 调试与 patch 推送，不进存档（口径 p）
};
```

**和 `Journal` 的区别（口径 h，也是 `PLAN-CORE` §八那张表的落地）：**

| | `Journal<T>` | `EventBus` |
|---|---|---|
| 是什么 | 某个系统自己的、带日期的条目数组 | 世界里刚发生的事的广播 |
| 进存档 | **是**，它是状态 | **否**，重放即重建 |
| 生命期 | 整局 | 环形缓冲 4096 条 |
| 谁读 | 它的主人（fold、压缩、渲染） | 订阅者（风评、对白触发、调试、patch） |
| 条目数 | 账本几千条 | 一局几十万条 |

> 一件事发生时，通常**两样都写**：`EventBus.publish` 让关心的人知道，
> 相关系统在自己的 `Journal` 里 `append` 一条。**但这是 L2 的编排，不是 L1 的规则。**
> `EventBus` 不会自动往任何 `Journal` 写东西——那就是污染。

### 4.5 `Handle` 与 `kind` 怎么不变成污染

L1 收整数，L2 得有地方把整数和意义对起来。做法是**在 L2 侧建表，不在 L1 侧**：

```cpp
// L2: src/rules/kinds.hpp —— L1 从不 include 它
enum class EventKind : uint32_t { PromiseMade = 1, PromiseBroken, GiftGiven, /* … */ };
```

调试输出要人话时，L3 在启动时把一张 `kind → 名字键` 表交给调试工具——
**交给工具，不是交给总线**。总线从头到尾只见 `uint32_t`。

`Handle` 同理：L3 维护「句柄 → 哪个系统的哪个回调」的表，`Timeline` 只负责把它按时还回来。

---

## 五、校验

| id | 内容 | 硬 / 软 |
|---|---|---|
| `CLK-daily` | `advance(n)` 与 n 次 `advance(1)` 产生**完全相同的派发序列**，且最终状态逐字节一致（这就是 `CORE-time` 的机制层版本） | 硬 |
| `CLK-order` | 同日到期的条目按 `(day, seq)` 派发；打乱排期顺序重跑，派发序列按 `seq` 复现 | 硬 |
| `CLK-cascade` | 同日级联超过 64 层时断言失败，**不静默、不死循环** | 硬 |
| `CLK-cancel` | 取消过的句柄永不派发；墓碑不改变其余条目的相对顺序 | 硬 |
| `CLK-cal` | `to_ymd` / `from_ymd` 往返一致；与 generator `planet.json.calendar` 的值对得上 | 硬 |
| `LOG-append` | `Journal` 的 `day` 始终非降序，id 始终单调；乱序追加被断言挡住 | 硬 |
| `LOG-fold` | 对每个业务 `Journal`，`fold_invariant_holds` 为真（`CORE-fold` 由此覆盖） | 硬 |
| `LOG-nostate` | 存档里不含任何 `Event`；加载后 `recent()` 为空且不影响任何派生状态 | 硬 |
| `CORE-layer` | ① L1 的独立测试只 include L0 且不含任何游戏概念，能跑通全部机制；② 源码扫描：`src/mech/` 与 `src/prim/` 不 include `../rules/`，不出现业务词表里的标识符 | 硬 |

`CORE-layer` 是本稿最重要的一条。它和 `PLAN-CORE` 的 `CORE-boundary`（`client_term` 守引擎边界）
是同一种手法：**边界不靠纪律维持，靠一个会报错的东西守着。**

---

## 六、实施步骤

| 步 | 内容 | 做完能看到什么 |
|---|---|---|
| 1 | `Day` · `Calendar` · `Clock` · 历法换算 · 从 slice 读参数 | `CLK-cal` 转绿；终端里能把日序打印成「第2年 秋 12」 |
| 2 | `Timeline`：排期、逐日循环、同日全序、级联上限、取消 | `CLK-daily` / `CLK-order` / `CLK-cascade` / `CLK-cancel` 转绿 |
| 3 | `Journal<T>` · fold · 压缩 · `fold_invariant_holds` 夹具 | `LOG-append` 转绿；`LOG-fold` 的夹具就位，等 L2 递函数进来 |
| 4 | `EventBus` · 环形缓冲 · 订阅 | `LOG-nostate` 转绿 |
| 5 | **L1 独立测试套件 + 源码扫描** 进 `zhouzhu_check` | `CORE-layer` 转绿——从此业务进不来 |

⚠️ **第 5 步不要往后挪。** 它在第 1 步之后的任何时刻都能建起来，
而每晚一天，L1 里就多一分被污染的机会。建议第 1 步做完就把扫描先跑起来（哪怕词表只有三个词）。

---

## 七、风险

- **口径 a 会被优化冲动反复挑战。** 「离场三个月，逐日循环九十次很傻」——
  不傻，一天的派发是微秒级，九十次也是微秒级。真正贵的是 L2 的回调，
  而**快进不会让回调变少，只会让它们挤在一起并改变顺序**。`CLK-daily` 挡着，但要在注释里写明原因。
- **同日顺序是最隐蔽的坑。** 堆的同键顺序、墓碑的跳过、级联插入的新条目，三者交织。
  `CLK-order` 要用打乱排期顺序的随机测试，不能只测顺序插入。
- **污染会从 `Handle` 的语义渗进来。** 一旦有人想「让排期器按类型排序，收成优先于腐坏」，
  L1 就要认识类型了。**答案永远是：顺序由 `seq` 决定，业务想控顺序就控制自己的排期时机。**
- **`Event` 的两个 `int64` 会不够用。** 到时的诱惑是加变长 payload。
  正确反应是问「这条是不是本该记进某个 `Journal`」——`EventBus` 是广播，不是存储。
- **`Journal` 的压缩策略写出来之前，`LOG-fold` 就要存在**（与 `PLAN-CORE` 风险节同一条）。
  否则夹具会被事后迁就成「压缩后 fold 也变了，那就改验收」。
- **历法参数漂移。** slice 重导时 `planet.json` 若变了，存档里的日序含义就变了。
  slice 要带 generator 的 seed 与版本（`PLAN-CORE` 风险节），`CLK-cal` 加载时对不上直接报错。
