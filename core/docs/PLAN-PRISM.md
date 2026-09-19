# 渲染分派与场查询设计稿：一台只会查表的机器

> 状态：**设计稿，未实施**（2026-09-19）。上游 `PLAN-CORE.md` 第五节底层清单 **#8 渲染分派、#9 场查询**。
> 与 `PLAN-CLOCK` / `PLAN-ENTITY` / `PLAN-SPACE` / `PLAN-POOL` 同属 **L1 机制层**（分层规则见 `PLAN-CORE` 第四节）。
>
> 写给一段时间没看项目的人：**先读一、二两节就够决定**。
>
> **本稿的全部要点**：
>
> > 这两个系统是同一台机器的两面——**L1 只会「拿着一张表取值」，选哪张表永远是 L2 的事。**
> > **第三幕的全部机制，就是 L2 换了表。**

---

## 一、要解决什么

这两个是四原语里的第三、第四个，也是**整个项目的主题所系**：

> `docs/05`：便利与失去是同一件事。
> `docs/06` §三：**主题的载体是 UI 的简化**——须知提示消失、换算括号归零。
> `DECISIONS` D32 / D75：第三幕的官秤重校、文字统一、历法统一，**在代码里就是换一张 locale 表**。

所以它们必须是底层：**如果渲染机制本身混在业务里，「换一张表」就没有一个稳定的东西去换。**
上一版把它们归进 L2 是错的，2026-09-19 改正（`PLAN-CORE` §5.1）。

它们要回答的问题：

| 问题 | 归谁 |
|---|---|
| 「34 单位的谷」在这个人眼里显示成什么 | #8 渲染分派 |
| 「第 2 年第 197 日」在这个地方叫什么 | #8 |
| 「此地一石，约合家中九斗七升」括号里那半句 | #8 |
| 他对这地方的规矩有几分把握，措辞该多含糊 | #8（透传） |
| 「白麻为丧」这条特征在这个地点有多强 | #9 场查询 |
| 第三幕把连续场一刀切平之后呢 | #9（L2 换表） |

---

## 二、需要拍板的口径（有默认值，可直接说「按默认」）

### 渲染分派

| # | 问题 | 默认 | 说明 |
|---|---|---|---|
| a | **`render` 的签名** | **`render(值, const Locale&, Confidence)`**——**不带观察者**。由 L2 从（观察者、地点、时间、知识）解析出 `Locale` 再递进来 | **修订 `PLAN-CORE` 口径 i。** 带观察者意味着 L1 要认识「知识」「把握度」这些业务概念；换成递一张表进来，L1 就退回成纯函数。「棱镜」这个整体 = L2 的 `resolve_locale` + L1 的 `render` |
| b | 中性值的形状 | **四种标签联合**：`Quantity{kind, amount}` · `DateVal{day}` · `NameRef{kind, key}` · `TagRef{tag}` | 量、日期、名字、标签——任何游戏都有这四样，域无关 |
| c | 量怎么渲染 | **单位阶梯**：降序的 `(unit_key, factor)` 列表 + 模板键。L1 只做整数除法，得到一串 `(unit_key, count)` | L1 不知道「石」是什么，只知道 factor 是 100 |
| d | **字怎么拼** | **模板来自内容**，L1 只做填空。**不得自己决定语序，不得自己加量词** | 语序与量词是语言的事，会随 locale 变。L1 一旦拼字符串，就把一种语言的规则写死进底层了 |
| e | gloss（换算括号）怎么来 | **`render_with_gloss(值, 本地 locale, 家乡 locale)`**；两者相同则 **gloss 为空** | ⚠️ **这就是第三幕**：统一度量衡之后两张表变成一张，括号自动消失。见 §3.4 |
| f | 错的值怎么渲染 | **L1 不知道对错。** L2 递一张错的 `Locale` 进来，渲染出的数就是错的 | `docs/06` §三 / D30：二手消息可能过时甚至错误。这是它的实现，而且**不需要 L1 配合** |
| g | 把握度 | **不进 L1 的判断**，只作为 `Confidence`（定点整数）透传到输出，供 UI 选措辞——**而措辞也是模板键** | 解掉 `DECISIONS` Q12：把握度既不是 UI 问题也不是签名里的特殊参数，它是「选哪个模板」的输入 |
| h | 谁负责「不留旧值」 | **没有缓存。** 每次显示都重新 `render` | 缓存成品字符串正是禁令要禁的东西（`PLAN-CORE` 风险节）。规模上完全不需要缓存 |

### 场查询

| # | 问题 | 默认 | 说明 |
|---|---|---|---|
| i | 场怎么存 | **按节点的定点整数数组**：`FieldSet{ field_id → vector<int32_t>(按节点) }` | 节点 = 岛群 = 一邑（D65），群内文化均匀，所以是查表不是插值 |
| j | **场值要不要量化** | **要。** slice 导出时量化成 `0..10000` 的定点整数，**C++ 侧永不见浮点** | ⚠️ 两个理由叠加：①确定性纪律第 3 条；②**Windows 与 Linux 之间少数 float64 数组有极小差异**（`generator/CLAUDE.md` 与跨机实测）。场值会影响判断 → 影响状态 → 影响重放。量化把这条路彻底堵死 |
| k | 采样接口 | **`sample(const FieldSet&, field_id, node) → int32_t`**——同样不带观察者、不带「现在是第几幕」 | 与口径 a 同一条纪律 |
| l | 第三幕把场切平怎么做 | **L2 换一张 `FieldSet`**，L1 一行不改 | 和换 locale 是同一个动作。见 §3.5 |
| m | 铁律五怎么守 | **L1 只有 `sample` 一个读口，没有 argmax、没有分类、没有 flood fill** | `PLAN-CORE` 第三节。想要「这地方主流是什么」的人得自己在 L2 写，而且会被 P1b 挡住 |

---

## 三、设计

### 3.1 一句话的架构

```
        L2                                   L1
┌────────────────────┐           ┌──────────────────────┐
│ resolve_locale(    │  Locale&  │ render(值, Locale,   │
│   观察者, 地点,     │ ────────► │        Confidence)   │ ──► {text, raw,
│   时间, 知识)       │           │  纯函数，无状态       │      unit, gloss,
│                    │           │                      │      confidence}
│ pick_fieldset(幕)  │ FieldSet& │ sample(FieldSet,     │ ──► int32_t
└────────────────────┘           │        field, node)  │
                                 └──────────────────────┘
```

**L1 这一侧没有任何业务概念**：不知道什么是观察者、什么是把握度、什么是第几幕、
什么是斗、什么是白麻。它只有「一张表 + 一个键 → 一个值」。

### 3.2 中性值与渲染

```cpp
struct Quantity { uint32_t kind; int64_t amount; };   // 基准单位的整数
struct DateVal  { int32_t  day;  };                   // 世界钟日序
struct NameRef  { uint32_t kind; uint32_t key; };     // 物名 / 地名 / 称呼
struct TagRef   { uint32_t tag;  };                   // 行为标签
using  Value = std::variant<Quantity, DateVal, NameRef, TagRef>;

using Confidence = int32_t;                           // 定点 0..10000

struct Rendered {
    std::string text;      // 唯一允许出现成品字符串的地方
    int64_t     raw;       // 中性值，原样带出，供排序与比较
    uint32_t    unit;      // 主单位键，UI 可能要单独排版
    std::string gloss;     // 换算括号；相同 locale 时为空
    Confidence  confidence;
};

Rendered render(const Value&, const Locale&, Confidence);
Rendered render_with_gloss(const Value&, const Locale& here, const Locale& home, Confidence);
```

⚠️ **`raw` 必须原样带出。** 否则 UI 要排序、要比大小时，唯一的办法是去 parse `text`——
那等于把渲染结果当成数据，禁令就破了。

### 3.3 `Locale` 是数据，不是代码

```cpp
struct UnitStep { uint32_t unit_key; int64_t factor; };   // 降序

struct Locale {
    // 量：每种物品一条单位阶梯（口径 c）
    std::vector<uint32_t>               qty_kind;        // 升序
    std::vector<std::vector<UnitStep>>  qty_ladder;

    // 日期：纪元偏移 + 各分量的名字键
    int32_t   epoch_day;
    uint32_t  year_key, season_key, month_key, day_key;

    // 名字：键 → 键的改写（物名、地名、称呼在这里换说法）
    std::vector<uint32_t> name_from, name_to;            // 平行，from 升序

    // 标签解读：标签 → (褒贬, 说法键)
    std::vector<uint32_t> tag_id;                        // 升序
    std::vector<int8_t>   tag_valence;
    std::vector<uint32_t> tag_key;

    // 模板：把上面查出来的碎片拼成一句话（口径 d）
    std::vector<uint32_t>    tmpl_id;                    // 升序
    std::vector<std::string> tmpl;                       // "{0}{u0}{1}{u1}"
};
```

全部是有序数组，二分查。**没有一个字段是代码。**

`render` 做的事，全部内容：

```
量:  按阶梯整数除 → [(unit_key, count) …]（余数进下一级，末级留余）
     取模板 → 填空 → text；raw = amount；unit = 首个非零的 unit_key
日期: day - epoch_day → 按 Calendar 除 → (年, 季, 月, 日)
     取模板 → 填空
名字: name_from 里二分，命中则换成 name_to，未命中原样
标签: tag_id 里二分 → (valence, key) → 取模板
```

**L1 从不决定语序，从不插入量词，从不做单复数，从不做千分位。** 全在模板里。

> ⚠️ 模板缺失时**报错**，不许回退到「把碎片按顺序连起来」。
> 那种回退在中文下看着几乎是对的，于是没人会发现有一半的文案根本没写——
> 直到换 locale 时全体露馅。`PRM-tmpl` 验这条。

### 3.4 gloss：主题的机制形态

`docs/06` §三 的第三种承载方式：

> 「此地一石，约合家中九斗七升」「七月初三（家中：六月三十）」，偏移量随距离连续变大。
> **统一后：偏移量归零，括号消失。**

机制上就是一句话：

```cpp
Rendered render_with_gloss(v, here, home, conf) {
    auto r = render(v, here, conf);
    if (&here == &home || here.same_shape_for(v, home)) return r;   // gloss 空
    r.gloss = render(v, home, conf).text;
    return r;
}
```

**第三幕的官秤重校 = `here` 和 `home` 变成同一张表 ⇒ `gloss` 自动为空 ⇒ 括号消失。**
不需要改一行代码，不需要遍历任何界面。

> 这是整个项目要验证的那件事（`docs/09` 的验证目标）在底层的形态。
> 也正因如此，**这段逻辑绝不能住在业务层**——它必须是一个不会变的东西。

### 3.5 场查询

```cpp
struct FieldSet {
    std::vector<uint32_t>              field_id;   // 升序
    std::vector<std::vector<int32_t>>  value;      // 按节点，定点 0..10000（口径 j）
    std::vector<uint16_t>              node_of;    // 节点 id → 下标
};

int32_t sample(const FieldSet&, uint32_t field, uint16_t node);
```

来源是 generator 第 ⑧ 步的 `fields.npz`（`share[T,N]`）与 `iso.npz`，
**在 slice 导出时量化成整数**（口径 j）。

第三幕的切平（口径 l）：L2 手里有两张 `FieldSet`（旧场、新朝统一之后的场），
按当前幕挑一张递给 `sample`。**L1 不知道有两张。**

⚠️ **铁律五在这里的落法**（口径 m）：L1 只提供 `sample`。
没有 `dominant_trait()`、没有 `classify_region()`、没有任何从场里派生标签的东西。
想要那种东西的人得在 L2 自己写，而且会被 P1b 与代码评审挡住。

### 3.6 为什么把握度不是特例（口径 g）

`DECISIONS` Q12 挂了很久：「把握度如何影响 UI 呈现（措辞含糊度？显示与否？）」。

答案是它**根本不需要特殊机制**，因为三件事各自已经有归属：

| 把握度的表现 | 谁做 |
|---|---|
| 数值可能是错的 | L2 递一张错的 `Locale`（口径 f）。L1 无感 |
| 措辞含糊（「大概」「听说」） | 选哪个模板键——模板本来就是内容 |
| 要不要显示 | UI 拿 `confidence` 自己决定（B 类） |

`render` 只需把 `Confidence` 透传出去。**签名里不多一个概念。**

---

## 四、校验

| id | 内容 | 硬 / 软 |
|---|---|---|
| `PRM-pure` | `render` / `sample` 是纯函数：同输入同输出，不持有任何状态，不读全局 | 硬 |
| `PRM-nolit` | **源码扫描**：L1 的 `prism` / `field` 模块里没有任何单位名、物名、量词、语序常量、褒贬词；没有字符串字面量（模板全部来自数据） | 硬 |
| `PRM-tmpl` | 模板缺失时报错，**不回退到拼接**；每个用到的模板键在内容表里存在（并进 `CORE-content`） | 硬 |
| `PRM-gloss` | `here == home` ⇒ `gloss` 为空；⇒ 第三幕切表后全库 gloss 为空（喂 `CORE-locale`） | 硬 |
| `PRM-ladder` | 单位阶梯的整数除法与朴素计算一致；**余数不丢**（各级 count × factor 之和 == amount） | 硬 |
| `PRM-raw` | 每个 `Rendered` 的 `raw` 等于输入的中性值；UI 层不解析 `text`（并进 `CORE-render`） | 硬 |
| `PRM-int` | L1 的 `field` 模块里**没有浮点类型**；slice 的场值全是整数 | 硬 |
| `PRM-nofield` | 源码扫描：L1 的 `field` 模块里没有 `argmax` / `classify` / `dominant` / flood fill（铁律五） | 硬 |

`PRM-nolit` 是本稿的主防线。它比其他几条扫描更严格——**L1 的这两个模块里一个字符串字面量都不该有**，
因为它们的产物就是字符串，一旦允许一个，就挡不住第二个。

---

## 五、实施步骤

| 步 | 内容 | 做完能看到什么 |
|---|---|---|
| 1 | `Value` 四型 · `Locale` 结构 · 从内容表加载 | 终端里加载一张 locale；`PRM-pure` 转绿 |
| 2 | 单位阶梯 · 日期 · 名字 · 标签四条渲染路径 · 模板填空 | 「34 谷」渲染成「三石七斗」；`PRM-ladder` / `PRM-tmpl` / `PRM-raw` 转绿 |
| 3 | `render_with_gloss` · 两张 locale | 换算括号出现；**切成同一张表，括号消失**；`PRM-gloss` 转绿 |
| 4 | `FieldSet` · slice 量化导出 · `sample` | `trait(白麻为丧, 某节点)` 有值；`PRM-int` / `PRM-nofield` 转绿 |
| 5 | `PRM-nolit` 源码扫描进 `zhouzhu_check`；并进 L1 独立测试套件 | 字面量从此进不来 |

⚠️ **第 3 步做完就该去验一次主题**：拿两张差异明显的 locale，看同一袋谷的显示与括号，
再把它们切成同一张。**那一瞬间的括号消失，就是整个项目要验证的东西的最小可见形态**——
比等到业务全做完再看，早了几个月。

---

## 六、风险

- **在 L1 里拼字符串。** `std::to_string(n) + "斗"` 是最自然的写法，也是禁令的正面违反。
  `PRM-nolit` 扫字面量能挡住显式的，挡不住从内容读来却用硬编码顺序拼的——
  **口径 d 要在代码注释里写明「模板决定语序」**。
- **顺手把观察者传进 `render`。** 「反正都要传，少一层转换」——一传，L1 就要认识知识与把握度，
  口径 a 的全部价值当场归零。**`Locale&` 是一道窄门，别把它拓宽。**
- **场值改回浮点。** 「定点 0..10000 精度不够」——真不够就加位数（改成 0..1000000），
  **不要回浮点**。跨机 float64 差异是实测过的（`generator/CLAUDE.md`），
  一旦场值进了判断，重放会在另一台机器上分叉，而这种 bug 几乎无法定位。
- **给渲染结果加缓存。** 列表翻页时每页几百次 `render`，看着值得缓存。
  但缓存里存的就是**成品字符串**——正是禁令要禁的东西，而且第三幕切表时它们不会失效。
  口径 h 说不缓存；真慢了，先量一量再说（几百次整数除法是微秒级）。
- **有人在 L2 写 `dominant_trait()`。** 铁律五禁的是从场 argmax 派生标签，
  但业务层写起来没有编译期阻力。`PRM-nofield` 只扫 L1。**这条要靠代码评审与 P1b。**
- **模板爆炸。** 四种值 × 若干 locale × 含糊程度，模板数量会涨得很快。
  这是内容问题不是机制问题，但要早点想清楚模板键的命名与组织，否则内容表会失控。
