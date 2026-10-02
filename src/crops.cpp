#include "crops.h"

#include <cstring>

namespace catalog {

const ItemDef ITEMS[] = {
    { "millet", "粟", 64, 0.88f, 0.76f, 0.36f },
    { "bean", "菽", 64, 0.62f, 0.56f, 0.32f },
    // 土产：各村记在仓账上、托运的货（见 freight.h）；不是种子也不是粮
    { "cloth", "麻布", 64, 0.86f, 0.82f, 0.7f }, // 本色
    { "pottery", "陶罐", 16, 0.7f, 0.38f, 0.22f }, // 赭
    { "lacquer", "漆器", 16, 0.5f, 0.08f, 0.07f }, // 朱黑
    { "coin", "铜钱", 999, 0.72f, 0.56f, 0.3f }, // 运费
    // 原料：地上一株株、一块块采来的，或采场里采的（见 deposit_kinds.h）；不是种子也不是粮
    { "stone", "石料", 64, 0.62f, 0.62f, 0.66f },
    { "clay", "黏土", 64, 0.66f, 0.38f, 0.24f },
    { "floatstone", "浮石", 64, 0.82f, 0.76f, 0.93f },
    { "timber", "木料", 64, 0.7f, 0.53f, 0.33f },
    { "firewood", "柴", 64, 0.55f, 0.45f, 0.27f }, // 灌丛砍的枝条
    { "gravel", "砂石", 64, 0.8f, 0.76f, 0.66f },
    { "gold_dust", "砂金", 64, 0.93f, 0.78f, 0.28f },
    { "copper_ore", "铜矿石", 64, 0.3f, 0.56f, 0.46f }, // 孔雀石绿
    { "iron_ore", "铁矿石", 64, 0.5f, 0.27f, 0.18f },
    { "lead_zinc_ore", "铅锌矿石", 64, 0.45f, 0.47f, 0.5f },
    { "gold_ore", "金银矿石", 64, 0.78f, 0.68f, 0.36f },
    { "manganese_ore", "锰矿石", 64, 0.24f, 0.21f, 0.24f },
    { "tin_ore", "锡矿石", 64, 0.4f, 0.4f, 0.43f },
    { "chromium_ore", "铬矿石", 64, 0.35f, 0.4f, 0.32f },
    { "nickel_ore", "镍矿石", 64, 0.5f, 0.56f, 0.39f },
    { "salt", "盐", 64, 0.92f, 0.90f, 0.85f },
    { "peat", "泥炭", 64, 0.3f, 0.22f, 0.15f },
    { "reed", "芦苇", 64, 0.8f, 0.74f, 0.48f },
    { "sulfur", "硫磺", 64, 0.9f, 0.84f, 0.26f },
    { "guano", "鸟粪", 64, 0.92f, 0.92f, 0.84f },
    // 合成出来的（见 recipes.h）：材料、工具、能摆在地上的（见 placeables.h）
    { "plank", "木板", 64, 0.86f, 0.7f, 0.47f },
    { "brick", "砖", 64, 0.66f, 0.3f, 0.22f },
    { "stone_axe", "石斧", 1, 0.52f, 0.56f, 0.6f },
    { "stone_pick", "石镐", 1, 0.46f, 0.48f, 0.56f },
    { "wood_shovel", "木锹", 1, 0.78f, 0.62f, 0.4f },
    { "workbench", "工作台", 16, 0.6f, 0.43f, 0.25f },
    { "kiln", "窑", 16, 0.6f, 0.36f, 0.27f },
    { "chest", "木箱", 16, 0.68f, 0.5f, 0.3f },
    { "torch", "火把", 64, 0.96f, 0.62f, 0.2f },
    { "fence", "篱笆", 64, 0.56f, 0.45f, 0.3f },
    // 改地形与搭建（切片十一）：挖出来的土（垒地面、夯墙用），窑里烧的瓦；搭建件（见 build_pieces.h）
    { "earth", "土", 64, 0.45f, 0.35f, 0.24f },
    { "roof_tile", "瓦", 64, 0.36f, 0.36f, 0.4f },
    { "foundation", "地基", 16, 0.52f, 0.5f, 0.46f },
    { "floorboard", "楼板", 16, 0.56f, 0.43f, 0.29f },
    { "earth_wall", "夯土墙", 16, 0.76f, 0.64f, 0.46f },
    { "brick_wall", "砖墙", 16, 0.6f, 0.33f, 0.25f },
    { "door_wall", "门墙", 16, 0.62f, 0.48f, 0.32f },
    { "window_wall", "窗墙", 16, 0.7f, 0.58f, 0.42f },
    { "gable", "山墙", 16, 0.72f, 0.6f, 0.43f },
    { "thatch_roof", "草顶", 16, 0.8f, 0.68f, 0.4f },
    { "tile_roof", "瓦顶", 16, 0.3f, 0.3f, 0.34f },
    { "pillar", "木柱", 16, 0.3f, 0.2f, 0.12f },
    { "ladder", "梯子", 16, 0.5f, 0.36f, 0.22f },
};
const int ITEM_COUNT = int(sizeof(ITEMS) / sizeof(ITEMS[0]));

const CropDef CROPS[] = {
    { "millet", "粟", "millet", "millet", 1.0, 4 },
    { "bean", "菽", "bean", "bean", 1.5, 3 }, // 豆子长得慢些、收得少些，但一样能一直种下去
};
const int CROP_COUNT = int(sizeof(CROPS) / sizeof(CROPS[0]));

const ToolDef TOOLS[] = {
    { "stone_axe", "timber,firewood", 2.0 }, // 伐木、砍柴
    { "stone_pick", "stone,floatstone,copper_ore,iron_ore,lead_zinc_ore,gold_ore,manganese_ore,tin_ore,sulfur", 2.0 }, // 凿石、采矿
    { "wood_shovel", "clay,gravel,gold_dust,peat,guano", 2.0 }, // 挖土、淘砂
};
const int TOOL_COUNT = int(sizeof(TOOLS) / sizeof(TOOLS[0]));

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

int stack_of(const std::string &p_id) {
    const ItemDef *def = item(p_id);
    return def ? def->stack : DEFAULT_STACK;
}

void item_color(const std::string &p_id, float &r_r, float &r_g, float &r_b) {
    const ItemDef *def = item(p_id);
    if (def) {
        r_r = def->r, r_g = def->g, r_b = def->b;
    } else {
        r_r = 0.45f, r_g = 0.5f, r_b = 0.35f;
    }
}

const ToolDef *tool(const std::string &p_item) {
    for (int k = 0; k < TOOL_COUNT; k++) {
        if (p_item == TOOLS[k].item) {
            return &TOOLS[k];
        }
    }
    return nullptr;
}

double tool_speed(const std::string &p_tool, const std::string &p_product) {
    const ToolDef *def = tool(p_tool);
    if (!def || p_product.empty()) {
        return 1.0;
    }
    // 逗号分隔的清单里有没有这样原料（整词比）
    const char *list = def->products;
    const size_t n = p_product.size();
    for (const char *at = list; *at;) {
        const char *comma = std::strchr(at, ',');
        const size_t len = comma ? size_t(comma - at) : std::strlen(at);
        if (len == n && std::strncmp(at, p_product.c_str(), n) == 0) {
            return def->speed;
        }
        if (!comma) {
            break;
        }
        at = comma + 1;
    }
    return 1.0;
}

} // namespace catalog
