#pragma once

#include <cstdint>
#include <string>

// 资源的种类表：skyisle-gen 的矿点种类（resources.py 的 RES_KINDS）在游戏里是什么。不含 Godot 头，无状态。
// 能采的：采出来是哪样原料（crops.h 的物品）、按住 E 多久采一份、一处存多少（按品位与面积）、采空了几个钟头回满、
// 采集点叫什么、画成什么样；不能采的（泉眼、温泉、洞穴）只在地图上标。

enum class DepositLook : uint8_t {
    NONE, // 不画（采不了的）
    QUARRY, // 一面凿开的石壁、几块方料
    PIT, // 挖开的土坑、土堆
    OUTCROP, // 灰白带紫的浮石露头
    LOGS, // 伐倒的原木、树桩
    BAR, // 河边的砂砾滩
    SLUICE, // 淘金的溜槽
    MINE, // 矿坑：红褐的矿石堆、辘轳
    REEDS, // 芦苇 / 泥炭：一片苇丛
    CRUST, // 硫磺、鸟粪：一片结壳
};

struct DepositKind {
    const char *key; // 生成器的键
    const char *name; // 生成器的中文名
    const char *site; // 采集点叫什么
    const char *verb; // 按住 E 干的是什么活
    const char *product; // 物品 id；空串 = 采不了（露天矿、泥炭按子类另算，见 product_of）
    double gather_s; // 按住 E 几秒采一份
    int base_stock; // 中品、面积合常时存几份
    double ref_area_km2; // 这类矿点面积的常数（生成器的中位数上下）
    double refill_hours; // 从采空到回满几个钟头（游戏钟）
    DepositLook look;
    float r, g, b; // 地图上的颜色（sRGB 0–1）
};

namespace deposits {

extern const DepositKind KINDS[];
extern const int KIND_COUNT;

constexpr int GRADE_LOW = 0, GRADE_MID = 1, GRADE_HIGH = 2; // 下 / 中 / 上

const DepositKind *kind(const std::string &p_key); // 表里没有返回 nullptr
// 品位的字（上 / 中 / 下，UTF-8）→ 0..2；认不得算中
int grade_of(const std::string &p_text);
const char *grade_name(int p_grade);

bool gatherable(const DepositKind &p_kind);
// 采出来的物品：露天矿按矿种（铜 → copper_ore……），泥炭 / 芦苇按子类；采不了返回空串
std::string product_of(const DepositKind &p_kind, const std::string &p_subtype);
// 这处存多少份：base × 品位（下 0.6、中 1、上 1.5）× √(面积 / 常数)（夹在 0.6–2 倍）
int stock_cap(const DepositKind &p_kind, int p_grade, double p_area_km2);
// 采集点的名字：「采石场（安山岩 / 凝灰岩）」「芦苇荡」「铜矿坑」
std::string site_name(const DepositKind &p_kind, const std::string &p_subtype);
// 干的活：「凿石」「挖土」「割苇」……
std::string verb_of(const DepositKind &p_kind, const std::string &p_subtype);
// 原料的颜色（货箱盖板、地图）；不是原料返回 false
bool product_color(const std::string &p_item, float &r_r, float &r_g, float &r_b);

} // namespace deposits
