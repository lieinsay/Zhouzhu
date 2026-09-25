#pragma once

#include <cstdint>
#include <string>

// 资源的种类表：skyisle-gen 的资源种类（resources.py 的 RES_KINDS，按它的 DESIGN-NOTES 四点二十一分三种形态）在游戏里是什么。不含 Godot 头，无状态。
//
//   点（泉眼、温泉、洞穴）：一处地标，摆个样子、地图上标，采不了；
//   片（林木、泥炭芦苇、浮石露头、鸟粪石）：一片地（terrain.bin 的片号），片里散布一株株 / 一块块能采的东西（见下面的散布物）；
//   散（金属矿、硫磺、砂金、黏土、砂砾、石料）：逐格的赋存品位（terrain.bin 的赋存场），品位够的地方散布露头；
//     生成器另按村挑了采场（采石场、土坑、采砂场、矿坑、淘金点、硫磺坑）——游戏里是带堆场的固定采集点：存量大、能打箱。
//
// 散布物（ScatterKind）：地上一株株、一块块的——树、灌丛、苇丛、浮石块、石块、黏土坡、砂砾堆、淘金沙、矿石、结壳。
// 每一株都能按住 E 采，采空了变树桩 / 坑，按全局钟长回来（resource_scatter.h）。

enum class DepositForm : uint8_t {
    POINT, // 点
    PATCH, // 片
    FIELD, // 散
};

enum class DepositLook : uint8_t {
    NONE, // 片：没有采集点
    // 采场
    QUARRY, // 一面凿开的石壁、几块方料
    PIT, // 挖开的土坑、土堆
    BAR, // 河边的砂砾滩
    SLUICE, // 淘金的溜槽
    MINE, // 矿坑：红褐的矿石堆、辘轳
    CRUST, // 硫磺：一片结壳
    // 地标
    SPRING, // 泉眼：一圈石头围着一汪水
    HOTSPRING, // 温泉：冒着白汽的池子
    CAVE, // 洞穴：岩壁上一个黑洞口
};

struct DepositKind {
    const char *key; // 生成器的键
    const char *name; // 生成器的中文名
    DepositForm form;
    const char *site; // 散：采场叫什么；点：地标叫什么；片：空
    const char *verb; // 在采场按住 E 干的是什么活
    const char *product; // 采场的物品 id；空串 = 按子类（矿坑）或没有采场
    double gather_s; // 采场按住 E 几秒采一份
    int base_stock; // 采场存几份（那一格品位 0.5 时）
    double refill_hours; // 采场从采空到回满几个钟头（游戏钟）
    DepositLook look;
    float r, g, b; // 地图上的颜色（sRGB 0–1）
};

enum class ScatterLook : uint8_t {
    BROADLEAF, // 阔叶树：树干 + 两三团圆冠
    CONIFER, // 针叶树：树干 + 叠着的锥
    SHRUB, // 灌丛：一蓬矮的
    REEDS, // 苇丛：一簇细高的秆
    BOULDER, // 石块
    FLOATSTONE, // 浮石块：灰白带紫
    CLAY_BANK, // 黏土坡：一片翻出来的红土
    GRAVEL_HEAP, // 砂砾堆
    PLACER, // 淘金沙：一片发亮的细砂
    ORE_ROCK, // 矿石：带色的石块
    CRUST, // 结壳：硫磺、鸟粪
};

struct ScatterKind {
    const char *key;
    const char *name; // 「阔叶树」
    const char *verb; // 「伐木」
    const char *product; // 物品 id；空串 = 按子类（矿石按矿种、苇丛按泥炭 / 芦苇）
    double gather_s; // 按住 E 几秒采一份
    int yield; // 中等大小的一株 / 一块有几份（按大小缩放，至少 1）
    double refill_hours; // 采空了几个钟头长回来
    ScatterLook look;
    bool solid; // 挡人挡船（树干）；矮的不挡
    double radius; // 大小 1 时占地的半径（米）：树是树干，别的是整块；够得着 = 离它边 reach 以内
    float r, g, b; // 主色（sRGB 0–1）
};

namespace deposits {

extern const DepositKind KINDS[];
extern const int KIND_COUNT;

constexpr int GRADE_LOW = 0, GRADE_MID = 1, GRADE_HIGH = 2; // 下 / 中 / 上

const DepositKind *kind(const std::string &p_key); // 表里没有返回 nullptr
// 品位的字（上 / 中 / 下，UTF-8）→ 0..2；认不得算中
int grade_of(const std::string &p_text);
const char *grade_name(int p_grade);

bool has_working(const DepositKind &p_kind); // 散的才有采场
bool is_landmark(const DepositKind &p_kind); // 点
// 采场采出来的物品：矿坑按矿种（铜 → copper_ore……），其余按表；没有采场返回空串
std::string product_of(const DepositKind &p_kind, const std::string &p_subtype);
// 采场存几份：base × (0.5 + 那一格的品位)，至少 1
int working_cap(const DepositKind &p_kind, double p_grade);
// 采场 / 地标的名字：「采石场（安山岩 / 凝灰岩）」「铜矿坑」「土坑（溪边黏土）」「洞穴（崖洞）」
std::string site_name(const DepositKind &p_kind, const std::string &p_subtype);
std::string verb_of(const DepositKind &p_kind);
// 矿种（铜、铁……）→ 矿石的物品 id；认不得给铜矿石
std::string ore_item(const std::string &p_subtype);
// 原料的颜色（货箱盖板、地图）；不是原料返回 false
bool product_color(const std::string &p_item, float &r_r, float &r_g, float &r_b);

// 散布物（下标即 ScatterLook 的值）
extern const ScatterKind SCATTER[];
extern const int SCATTER_COUNT;
const ScatterKind &scatter(ScatterLook p_look);
// 散布物的子类：矿石的矿种下标、苇丛是泥炭还是芦苇、结壳是硫磺还是鸟粪（resource_scatter 生成时定，存在实例上）
constexpr uint8_t SUB_PEAT = 0, SUB_REED = 1;
constexpr uint8_t SUB_SULFUR = 0, SUB_GUANO = 1;
int ore_index(const std::string &p_subtype); // 矿种 → 下标；认不得 0（铜）
// 这一株采出来的物品
std::string scatter_product(ScatterLook p_look, uint8_t p_sub);
// 这一株叫什么（苇丛按子类叫「芦苇」「泥炭」，矿石按矿种叫「铜矿石」）
std::string scatter_name(ScatterLook p_look, uint8_t p_sub);
std::string scatter_verb(ScatterLook p_look, uint8_t p_sub);
// 这一株有几份：yield × 大小²（夹在 1 到 3 倍 yield）
int scatter_yield(ScatterLook p_look, double p_scale);

} // namespace deposits
