#include "crops.h"

#include <cstring>

namespace catalog {

const ItemDef ITEMS[] = {
    { "millet", "粟" },
    { "bean", "菽" },
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
