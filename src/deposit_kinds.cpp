#include "deposit_kinds.h"

#include <algorithm>
#include <cmath>

namespace deposits {

// 顺序同 skyisle-gen 的 RES_KINDS。数字按人手一把镐、一只筐来定：石料、浮石重，一份几秒；砂金一份要淘半天
const DepositKind KINDS[] = {
    { "timber", "林木", "林场", "伐木", "timber", 4.0, 40, 6.0, 48.0, DepositLook::LOGS, 0.24f, 0.51f, 0.24f },
    { "spring", "泉眼", "泉眼", "", "", 0.0, 0, 0.01, 0.0, DepositLook::NONE, 0.47f, 0.86f, 1.0f },
    { "clay", "黏土", "黏土坑", "挖土", "clay", 2.0, 40, 0.05, 12.0, DepositLook::PIT, 0.75f, 0.47f, 0.31f },
    { "peat", "泥炭 / 芦苇", "泥炭沼", "挖泥炭", "", 2.5, 40, 0.19, 24.0, DepositLook::REEDS, 0.43f, 0.35f, 0.24f },
    { "gravel", "砂砾", "砂砾滩", "挖砂", "gravel", 2.0, 50, 0.14, 12.0, DepositLook::BAR, 0.8f, 0.76f, 0.67f },
    { "placer", "砂金", "淘金滩", "淘金", "gold_dust", 8.0, 6, 0.13, 72.0, DepositLook::SLUICE, 1.0f, 0.84f, 0.31f },
    { "quarry", "采石场", "采石场", "凿石", "stone", 3.0, 40, 0.03, 24.0, DepositLook::QUARRY, 0.67f, 0.67f, 0.73f },
    { "floatstone", "浮石", "浮石崖", "凿浮石", "floatstone", 4.0, 30, 0.17, 24.0, DepositLook::OUTCROP, 0.59f, 0.39f, 0.9f },
    { "ore", "露天矿", "矿坑", "采矿", "", 5.0, 30, 2.5, 48.0, DepositLook::MINE, 0.78f, 0.24f, 0.2f },
    { "hotspring", "温泉", "温泉", "", "", 0.0, 0, 0.01, 0.0, DepositLook::NONE, 1.0f, 0.55f, 0.71f },
    { "sulfur", "硫磺", "硫磺坑", "采硫", "sulfur", 4.0, 20, 0.2, 48.0, DepositLook::CRUST, 0.9f, 0.9f, 0.24f },
    { "cave", "洞穴", "洞穴", "", "", 0.0, 0, 0.01, 0.0, DepositLook::NONE, 0.2f, 0.2f, 0.2f },
    { "guano", "鸟粪石", "鸟粪崖", "铲鸟粪", "guano", 3.0, 20, 0.1, 72.0, DepositLook::CRUST, 0.94f, 0.94f, 0.82f },
};
const int KIND_COUNT = int(sizeof(KINDS) / sizeof(KINDS[0]));

namespace {

// 露天矿的矿种（生成器 ORE_WEIGHTS 里出现的全部）→ 物品
struct OreKind {
    const char *subtype;
    const char *item;
};
const OreKind ORES[] = {
    { "铜", "copper_ore" },
    { "铁", "iron_ore" },
    { "铅锌", "lead_zinc_ore" },
    { "金", "gold_ore" },
    { "锰", "manganese_ore" },
    { "锡钨", "tin_ore" },
};

struct RawColor {
    const char *item;
    float r, g, b;
};
const RawColor RAW_COLORS[] = {
    { "stone", 0.62f, 0.62f, 0.66f },
    { "clay", 0.66f, 0.38f, 0.24f },
    { "floatstone", 0.82f, 0.76f, 0.93f },
    { "timber", 0.7f, 0.53f, 0.33f },
    { "gravel", 0.8f, 0.76f, 0.66f },
    { "gold_dust", 0.93f, 0.78f, 0.28f },
    { "copper_ore", 0.3f, 0.56f, 0.46f }, // 孔雀石绿
    { "iron_ore", 0.5f, 0.27f, 0.18f },
    { "lead_zinc_ore", 0.45f, 0.47f, 0.5f },
    { "gold_ore", 0.78f, 0.68f, 0.36f },
    { "manganese_ore", 0.24f, 0.21f, 0.24f },
    { "tin_ore", 0.4f, 0.4f, 0.43f },
    { "peat", 0.3f, 0.22f, 0.15f },
    { "reed", 0.8f, 0.74f, 0.48f },
    { "sulfur", 0.9f, 0.84f, 0.26f },
    { "guano", 0.92f, 0.92f, 0.84f },
};

bool is_reed(const DepositKind &p_kind, const std::string &p_subtype) {
    return std::string(p_kind.key) == "peat" && p_subtype.find("芦苇") != std::string::npos;
}

} // namespace

const DepositKind *kind(const std::string &p_key) {
    for (int k = 0; k < KIND_COUNT; k++) {
        if (p_key == KINDS[k].key) {
            return &KINDS[k];
        }
    }
    return nullptr;
}

int grade_of(const std::string &p_text) {
    if (p_text == "上") {
        return GRADE_HIGH;
    }
    if (p_text == "下") {
        return GRADE_LOW;
    }
    return GRADE_MID;
}

const char *grade_name(int p_grade) {
    return p_grade >= GRADE_HIGH ? "上" : (p_grade <= GRADE_LOW ? "下" : "中");
}

bool gatherable(const DepositKind &p_kind) {
    return p_kind.look != DepositLook::NONE;
}

std::string product_of(const DepositKind &p_kind, const std::string &p_subtype) {
    if (!gatherable(p_kind)) {
        return std::string();
    }
    const std::string key = p_kind.key;
    if (key == "ore") {
        for (const OreKind &ore : ORES) {
            if (p_subtype == ore.subtype) {
                return ore.item;
            }
        }
        return "copper_ore";
    }
    if (key == "peat") {
        return is_reed(p_kind, p_subtype) ? "reed" : "peat";
    }
    return p_kind.product;
}

int stock_cap(const DepositKind &p_kind, int p_grade, double p_area_km2) {
    static const double GRADE_MULT[3] = { 0.6, 1.0, 1.5 };
    const double grade = GRADE_MULT[std::clamp(p_grade, 0, 2)];
    const double area = std::clamp(std::sqrt(std::max(0.0, p_area_km2) / std::max(1e-6, p_kind.ref_area_km2)), 0.6, 2.0);
    return std::max(1, int(std::lround(p_kind.base_stock * grade * area)));
}

std::string site_name(const DepositKind &p_kind, const std::string &p_subtype) {
    const std::string key = p_kind.key;
    if (key == "ore") {
        return (p_subtype.empty() ? std::string() : p_subtype) + p_kind.site;
    }
    if (key == "peat") {
        return is_reed(p_kind, p_subtype) ? "芦苇荡" : p_kind.site;
    }
    if (p_subtype.empty()) {
        return p_kind.site;
    }
    return std::string(p_kind.site) + "（" + p_subtype + "）";
}

std::string verb_of(const DepositKind &p_kind, const std::string &p_subtype) {
    return is_reed(p_kind, p_subtype) ? std::string("割苇") : std::string(p_kind.verb);
}

bool product_color(const std::string &p_item, float &r_r, float &r_g, float &r_b) {
    for (const RawColor &c : RAW_COLORS) {
        if (p_item == c.item) {
            r_r = c.r, r_g = c.g, r_b = c.b;
            return true;
        }
    }
    return false;
}

} // namespace deposits
