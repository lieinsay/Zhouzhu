#include "recipes.h"

#include <algorithm>

#include "crops.h"

namespace recipes {

// 随身能做的只有几样：木料劈成板、板钉成工作台、松明；其余的要工作台，陶与砖要进窑烧（木料当柴）
const RecipeDef RECIPES[] = {
    { "plank", "plank", 4, Station::HAND, { { "timber", 1 } } },
    { "workbench", "workbench", 1, Station::HAND, { { "plank", 4 } } },
    { "torch", "torch", 2, Station::HAND, { { "plank", 1 } } },
    { "chest", "chest", 1, Station::WORKBENCH, { { "plank", 8 } } },
    { "fence", "fence", 3, Station::WORKBENCH, { { "plank", 2 }, { "timber", 1 } } },
    { "stone_axe", "stone_axe", 1, Station::WORKBENCH, { { "stone", 2 }, { "plank", 1 } } },
    { "stone_pick", "stone_pick", 1, Station::WORKBENCH, { { "stone", 3 }, { "plank", 1 } } },
    { "wood_shovel", "wood_shovel", 1, Station::WORKBENCH, { { "plank", 3 } } },
    { "kiln", "kiln", 1, Station::WORKBENCH, { { "stone", 8 }, { "clay", 4 } } },
    { "brick", "brick", 4, Station::KILN, { { "clay", 4 }, { "timber", 1 } } },
    { "pottery", "pottery", 1, Station::KILN, { { "clay", 3 }, { "timber", 1 } } },
};
const int RECIPE_COUNT = int(sizeof(RECIPES) / sizeof(RECIPES[0]));

const RecipeDef *find(const std::string &p_id) {
    for (int k = 0; k < RECIPE_COUNT; k++) {
        if (p_id == RECIPES[k].id) {
            return &RECIPES[k];
        }
    }
    return nullptr;
}

const char *station_name(Station p_station) {
    switch (p_station) {
        case Station::HAND:
            return "随身";
        case Station::WORKBENCH:
            return "工作台";
        case Station::KILN:
            return "窑";
        default:
            return "";
    }
}

int ingredient_count(const RecipeDef &p_recipe) {
    int n = 0;
    while (n < 3 && p_recipe.in[n].item) {
        n++;
    }
    return n;
}

bool station_ok(const RecipeDef &p_recipe, unsigned p_stations) {
    return ((p_stations | station_bit(Station::HAND)) & station_bit(p_recipe.station)) != 0;
}

int times(const RecipeDef &p_recipe, const ItemBag &p_bag) {
    int best = 1 << 30;
    for (int k = 0; k < ingredient_count(p_recipe); k++) {
        const Ingredient &in = p_recipe.in[k];
        best = std::min(best, p_bag.count(in.item) / std::max(1, in.count));
    }
    return ingredient_count(p_recipe) == 0 ? 0 : best;
}

std::string problem(const RecipeDef &p_recipe, unsigned p_stations, const ItemSlots &p_bag, const ItemStack &p_cursor, const ItemSlots *p_other) {
    if (!station_ok(p_recipe, p_stations)) {
        return std::string("要在") + station_name(p_recipe.station) + "旁边做";
    }
    for (int k = 0; k < ingredient_count(p_recipe); k++) {
        const Ingredient &in = p_recipe.in[k];
        const int have = p_bag.count(in.item);
        if (have < in.count) {
            return catalog::item_name(in.item) + "不够（" + std::to_string(have) + " / " + std::to_string(in.count) + "）";
        }
    }
    // 在副本上扣料、放产物：放不下（或拖着的那叠没地方放回去）就不做
    ItemSlots after = p_bag;
    for (int k = 0; k < ingredient_count(p_recipe); k++) {
        after.take(p_recipe.in[k].item, p_recipe.in[k].count);
    }
    if (after.put(p_recipe.product, p_recipe.yield) > 0) {
        return "背包放不下" + catalog::item_name(p_recipe.product);
    }
    if (!p_cursor.empty() && after.room_for(p_cursor.item) + (p_other ? p_other->room_for(p_cursor.item) : 0) < p_cursor.count) {
        return "背包快满了：先把拖着的" + catalog::item_name(p_cursor.item) + "放下";
    }
    return std::string();
}

std::string craft(const RecipeDef &p_recipe, unsigned p_stations, ItemSlots &r_bag, const ItemStack &p_cursor, const ItemSlots *p_other) {
    const std::string why = problem(p_recipe, p_stations, r_bag, p_cursor, p_other);
    if (!why.empty()) {
        return why;
    }
    for (int k = 0; k < ingredient_count(p_recipe); k++) {
        r_bag.take(p_recipe.in[k].item, p_recipe.in[k].count);
    }
    r_bag.put(p_recipe.product, p_recipe.yield);
    return std::string();
}

} // namespace recipes
