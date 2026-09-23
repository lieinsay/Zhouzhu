#pragma once

#include <string>

// 物品与作物表。不含 Godot 头；名字是 UTF-8（两个编译目标都开了 /utf-8），Godot 那边用 String::utf8() 转。
// 物品只有 id 和名字（粮种、各村的土产、铜钱）；作物说明「哪样物品是它的种子、收下来得到哪样物品、生长期是基准的几倍、收一格得几份」。
// 眼下粟和菽都是「收成即种子」，但表里种子与产物分开写，将来加「稻谷 → 稻米」这类作物不用改结构。

struct ItemDef {
    const char *id;
    const char *name; // 显示名
};

struct CropDef {
    const char *id;
    const char *name;
    const char *seed; // 播它要用的物品
    const char *product; // 收它得到的物品
    double grow_factor; // 生长期 = FarmRules::grow_hours × 这个
    int yield; // 收一格得几份产物
};

namespace catalog {

extern const ItemDef ITEMS[];
extern const int ITEM_COUNT;
extern const CropDef CROPS[];
extern const int CROP_COUNT;

const ItemDef *item(const std::string &p_id); // 没有返回 nullptr
const CropDef *crop(const std::string &p_id);
const CropDef *crop_of_seed(const std::string &p_item); // 这样物品能种出什么；不是种子返回 nullptr
inline bool is_seed(const std::string &p_item) { return crop_of_seed(p_item) != nullptr; }
// 显示名；表里没有就原样返回 id
std::string item_name(const std::string &p_id);

} // namespace catalog
