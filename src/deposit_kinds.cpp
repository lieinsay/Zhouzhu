#include "deposit_kinds.h"

#include <algorithm>
#include <cmath>

#include "crops.h"

namespace deposits {

// 顺序同 skyisle-gen 的 RES_KINDS。采场的数字按人手一把镐、一只筐来定：石料重，一份几秒；砂金一份要淘半天
const DepositKind KINDS[] = {
    { "timber", "林木", DepositForm::PATCH, "", "", "", 0.0, 0, 0.0, DepositLook::NONE, 0.24f, 0.51f, 0.24f },
    { "spring", "泉眼", DepositForm::POINT, "泉眼", "", "", 0.0, 0, 0.0, DepositLook::SPRING, 0.47f, 0.86f, 1.0f },
    { "clay", "黏土", DepositForm::FIELD, "土坑", "挖土", "clay", 2.0, 40, 12.0, DepositLook::PIT, 0.75f, 0.47f, 0.31f },
    { "peat", "泥炭 / 芦苇", DepositForm::PATCH, "", "", "", 0.0, 0, 0.0, DepositLook::NONE, 0.43f, 0.35f, 0.24f },
    { "gravel", "砂砾", DepositForm::FIELD, "采砂场", "挖砂", "gravel", 2.0, 50, 12.0, DepositLook::BAR, 0.8f, 0.76f, 0.67f },
    { "placer", "砂金", DepositForm::FIELD, "淘金点", "淘金", "gold_dust", 8.0, 6, 72.0, DepositLook::SLUICE, 1.0f, 0.84f, 0.31f },
    { "stone", "石料", DepositForm::FIELD, "采石场", "凿石", "stone", 3.0, 40, 24.0, DepositLook::QUARRY, 0.67f, 0.67f, 0.73f },
    { "floatstone", "浮石", DepositForm::PATCH, "", "", "", 0.0, 0, 0.0, DepositLook::NONE, 0.59f, 0.39f, 0.9f },
    { "ore", "金属矿", DepositForm::FIELD, "矿坑", "采矿", "", 5.0, 30, 48.0, DepositLook::MINE, 0.78f, 0.24f, 0.2f },
    { "hotspring", "温泉", DepositForm::POINT, "温泉", "", "", 0.0, 0, 0.0, DepositLook::HOTSPRING, 1.0f, 0.55f, 0.71f },
    { "sulfur", "硫磺", DepositForm::FIELD, "硫磺坑", "采硫", "sulfur", 4.0, 20, 48.0, DepositLook::CRUST, 0.9f, 0.9f, 0.24f },
    { "cave", "洞穴", DepositForm::POINT, "洞穴", "", "", 0.0, 0, 0.0, DepositLook::CAVE, 0.2f, 0.2f, 0.2f },
    { "guano", "鸟粪石", DepositForm::PATCH, "", "", "", 0.0, 0, 0.0, DepositLook::NONE, 0.94f, 0.94f, 0.82f },
    { "salt", "岩盐", DepositForm::FIELD, "盐井", "汲卤煮盐", "salt", 4.0, 30, 48.0, DepositLook::SLUICE, 0.92f, 0.90f, 0.85f },
    { "saltspring", "盐泉", DepositForm::POINT, "盐泉", "", "", 0.0, 0, 0.0, DepositLook::SPRING, 0.75f, 0.85f, 0.9f },
    { "fossil", "贝壳化石", DepositForm::POINT, "贝壳化石", "", "", 0.0, 0, 0.0, DepositLook::QUARRY, 0.85f, 0.82f, 0.68f },
};
const int KIND_COUNT = int(sizeof(KINDS) / sizeof(KINDS[0]));

// 下标即 ScatterLook。树一株几份木料、长回来要两天；石块、浮石一块凿完三天才「长」回来（坡上又滚下来、又露出来一块）
const ScatterKind SCATTER[] = {
    { "broadleaf", "阔叶树", "伐木", "timber", 4.0, 4, 48.0, ScatterLook::BROADLEAF, true, 0.32, 0.22f, 0.36f, 0.17f },
    { "conifer", "针叶树", "伐木", "timber", 4.0, 4, 48.0, ScatterLook::CONIFER, true, 0.3, 0.16f, 0.3f, 0.18f },
    { "shrub", "灌丛", "砍柴", "firewood", 2.0, 2, 24.0, ScatterLook::SHRUB, false, 1.0, 0.33f, 0.42f, 0.19f },
    { "reeds", "苇丛", "割苇", "", 2.5, 3, 24.0, ScatterLook::REEDS, false, 0.9, 0.62f, 0.6f, 0.36f },
    { "boulder", "石块", "凿石", "stone", 3.0, 3, 72.0, ScatterLook::BOULDER, false, 0.8, 0.55f, 0.55f, 0.56f },
    { "floatstone", "浮石块", "凿浮石", "floatstone", 4.0, 3, 72.0, ScatterLook::FLOATSTONE, false, 0.8, 0.8f, 0.75f, 0.92f },
    { "clay_bank", "黏土坡", "挖土", "clay", 2.0, 4, 24.0, ScatterLook::CLAY_BANK, false, 1.3, 0.66f, 0.4f, 0.26f },
    { "gravel_heap", "砂砾堆", "挖砂", "gravel", 2.0, 4, 24.0, ScatterLook::GRAVEL_HEAP, false, 1.1, 0.78f, 0.74f, 0.64f },
    { "placer", "淘金沙", "淘金", "gold_dust", 8.0, 1, 72.0, ScatterLook::PLACER, false, 1.0, 0.86f, 0.74f, 0.4f },
    { "ore_rock", "矿石", "采矿", "", 5.0, 2, 96.0, ScatterLook::ORE_ROCK, false, 0.7, 0.5f, 0.35f, 0.3f },
    { "crust", "硫磺结壳", "采硫", "sulfur", 4.0, 2, 48.0, ScatterLook::CRUST, false, 1.0, 0.9f, 0.84f, 0.26f },
};
const int SCATTER_COUNT = int(sizeof(SCATTER) / sizeof(SCATTER[0]));

namespace {

// 金属矿的矿种（生成器 ORE_WEIGHTS 里出现的全部）→ 物品
struct OreKind {
    const char *subtype;
    const char *item;
};
const OreKind ORES[] = {
    { "铜", "copper_ore" },
    { "铁", "iron_ore" },
    { "铅锌", "lead_zinc_ore" },
    { "金银", "gold_ore" },
    { "锰", "manganese_ore" },
    { "锡", "tin_ore" },
    { "铬", "chromium_ore" },
    { "镍", "nickel_ore" },
};
constexpr int ORE_COUNT = int(sizeof(ORES) / sizeof(ORES[0]));

// 采得到的原料（颜色在物品表里）
const char *const RAW_ITEMS[] = {
    "stone", "clay", "floatstone", "timber", "firewood", "gravel", "gold_dust", "copper_ore", "iron_ore", "lead_zinc_ore", "gold_ore",
    "manganese_ore", "tin_ore", "peat", "reed", "sulfur", "guano", "salt", "chromium_ore", "nickel_ore",
};

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

bool has_working(const DepositKind &p_kind) {
    return p_kind.form == DepositForm::FIELD;
}

bool is_landmark(const DepositKind &p_kind) {
    return p_kind.form == DepositForm::POINT;
}

std::string ore_item(const std::string &p_subtype) {
    return ORES[ore_index(p_subtype)].item;
}

int ore_index(const std::string &p_subtype) {
    // 保留现有开局包的旧名称；矿种索引次序不变。
    if (p_subtype == "金") return 3;
    if (p_subtype == "锡钨") return 5;
    for (int k = 0; k < ORE_COUNT; k++) {
        if (p_subtype == ORES[k].subtype) {
            return k;
        }
    }
    return 0;
}

std::string product_of(const DepositKind &p_kind, const std::string &p_subtype) {
    if (!has_working(p_kind)) {
        return std::string();
    }
    if (std::string(p_kind.key) == "ore") {
        return ore_item(p_subtype);
    }
    return p_kind.product;
}

int working_cap(const DepositKind &p_kind, double p_grade) {
    return std::max(1, int(std::lround(p_kind.base_stock * (0.5 + std::clamp(p_grade, 0.0, 1.0)))));
}

std::string site_name(const DepositKind &p_kind, const std::string &p_subtype) {
    const std::string key = p_kind.key;
    if (key == "ore") {
        return p_subtype + p_kind.site;
    }
    if (p_subtype.empty() || key == "placer") {
        return p_kind.site;
    }
    return std::string(p_kind.site) + "（" + p_subtype + "）";
}

std::string verb_of(const DepositKind &p_kind) {
    return p_kind.verb;
}

bool product_color(const std::string &p_item, float &r_r, float &r_g, float &r_b) {
    for (const char *raw : RAW_ITEMS) {
        if (p_item == raw) {
            catalog::item_color(p_item, r_r, r_g, r_b);
            return true;
        }
    }
    return false;
}

const ScatterKind &scatter(ScatterLook p_look) {
    return SCATTER[std::clamp(int(p_look), 0, SCATTER_COUNT - 1)];
}

std::string scatter_product(ScatterLook p_look, uint8_t p_sub) {
    if (p_look == ScatterLook::ORE_ROCK) {
        return ORES[std::min(int(p_sub), ORE_COUNT - 1)].item;
    }
    if (p_look == ScatterLook::REEDS) {
        return p_sub == SUB_REED ? "reed" : "peat";
    }
    if (p_look == ScatterLook::CRUST && p_sub == SUB_GUANO) {
        return "guano";
    }
    return scatter(p_look).product;
}

std::string scatter_name(ScatterLook p_look, uint8_t p_sub) {
    if (p_look == ScatterLook::ORE_ROCK) {
        return std::string(ORES[std::min(int(p_sub), ORE_COUNT - 1)].subtype) + "矿石";
    }
    if (p_look == ScatterLook::REEDS) {
        return p_sub == SUB_REED ? "芦苇" : "泥炭";
    }
    if (p_look == ScatterLook::CRUST && p_sub == SUB_GUANO) {
        return "鸟粪";
    }
    return scatter(p_look).name;
}

std::string scatter_verb(ScatterLook p_look, uint8_t p_sub) {
    if (p_look == ScatterLook::REEDS && p_sub != SUB_REED) {
        return "挖泥炭";
    }
    if (p_look == ScatterLook::CRUST && p_sub == SUB_GUANO) {
        return "铲鸟粪";
    }
    return scatter(p_look).verb;
}

int scatter_yield(ScatterLook p_look, double p_scale) {
    const int base = scatter(p_look).yield;
    return std::clamp(int(std::lround(base * p_scale * p_scale)), 1, 3 * base);
}

} // namespace deposits
