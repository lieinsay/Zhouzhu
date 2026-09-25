#include "crops.h"

#include <cstring>

namespace catalog {

const ItemDef ITEMS[] = {
    { "millet", "粟" },
    { "bean", "菽" },
    // 土产：各村记在仓账上、托运的货（见 freight.h）；不是种子也不是粮
    { "cloth", "麻布" },
    { "pottery", "陶罐" },
    { "lacquer", "漆器" },
    { "coin", "铜钱" }, // 运费
    // 原料：资源点采来的（见 deposit_kinds.h）；不是种子也不是粮
    { "stone", "石料" },
    { "clay", "黏土" },
    { "floatstone", "浮石" },
    { "timber", "木料" },
    { "gravel", "砂石" },
    { "gold_dust", "砂金" },
    { "copper_ore", "铜矿石" },
    { "iron_ore", "铁矿石" },
    { "lead_zinc_ore", "铅锌矿石" },
    { "gold_ore", "金矿石" },
    { "manganese_ore", "锰矿石" },
    { "tin_ore", "锡钨矿石" },
    { "peat", "泥炭" },
    { "reed", "芦苇" },
    { "sulfur", "硫磺" },
    { "guano", "鸟粪" },
};
const int ITEM_COUNT = int(sizeof(ITEMS) / sizeof(ITEMS[0]));

const CropDef CROPS[] = {
    { "millet", "粟", "millet", "millet", 1.0, 4 },
    { "bean", "菽", "bean", "bean", 1.5, 3 }, // 豆子长得慢些、收得少些，但一样能一直种下去
};
const int CROP_COUNT = int(sizeof(CROPS) / sizeof(CROPS[0]));

const ItemDef *item(const std::string &p_id) {
    for (int k = 0; k < ITEM_COUNT; k++) {
        if (p_id == ITEMS[k].id) {
            return &ITEMS[k];
        }
    }
    return nullptr;
}

const CropDef *crop(const std::string &p_id) {
    for (int k = 0; k < CROP_COUNT; k++) {
        if (p_id == CROPS[k].id) {
            return &CROPS[k];
        }
    }
    return nullptr;
}

const CropDef *crop_of_seed(const std::string &p_item) {
    for (int k = 0; k < CROP_COUNT; k++) {
        if (p_item == CROPS[k].seed) {
            return &CROPS[k];
        }
    }
    return nullptr;
}

std::string item_name(const std::string &p_id) {
    const ItemDef *def = item(p_id);
    return def ? std::string(def->name) : p_id;
}

} // namespace catalog
