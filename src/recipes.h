#pragma once

#include <cstdint>
#include <string>

#include "item_slots.h"

// 配方与合成。不含 Godot 头，无状态。
// 一条配方：拿几样料（至多三样）合出几份一样东西，要在哪儿做——随身（背包里就能做）、工作台旁、窑旁（设施是摆在地上的物件，见 placeables.h）。
// 合一回：设施在不在身边、料够不够、合出来的东西放不放得下都查过才动背包；不做就说为什么（给玩家看的话）。
// 料只从背包里扣、产物只进背包（船舱、木箱里的料得先搬回背包）。

enum class Station : uint8_t {
    HAND, // 随身
    WORKBENCH, // 工作台
    KILN, // 窑
    COUNT,
};

constexpr unsigned station_bit(Station p_station) {
    return 1u << unsigned(p_station);
}

struct Ingredient {
    const char *item; // nullptr = 没有这一样
    int count;
};

struct RecipeDef {
    const char *id;
    const char *product;
    int yield; // 合一回得几份
    Station station;
    Ingredient in[3];
};

namespace recipes {

extern const RecipeDef RECIPES[];
extern const int RECIPE_COUNT;

const RecipeDef *find(const std::string &p_id); // 没有返回 nullptr
const char *station_name(Station p_station); // 「随身」「工作台」「窑」（UTF-8）
int ingredient_count(const RecipeDef &p_recipe);

// 设施够不够：p_stations 是身边有的设施（station_bit 的或；随身总是有）
bool station_ok(const RecipeDef &p_recipe, unsigned p_stations);
// 按 p_bag 里的料能合几回（不看设施、不看放不放得下）
int times(const RecipeDef &p_recipe, const ItemBag &p_bag);
// 合不了的头一条原因（设施 > 料 > 放不下）；合得了返回空串。
// p_cursor：面板上还拖在鼠标上的那一叠——合完它得还有地方放回去（r_bag 或 p_other）
std::string problem(const RecipeDef &p_recipe, unsigned p_stations, const ItemSlots &p_bag, const ItemStack &p_cursor = ItemStack(),
        const ItemSlots *p_other = nullptr);
// 合一回：扣料、放产物；返回空串表示做了，否则原因，背包不动
std::string craft(const RecipeDef &p_recipe, unsigned p_stations, ItemSlots &r_bag, const ItemStack &p_cursor = ItemStack(),
        const ItemSlots *p_other = nullptr);

} // namespace recipes
