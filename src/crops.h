#pragma once

#include <string>

// 物品与作物表。不含 Godot 头；名字是 UTF-8（两个编译目标都开了 /utf-8），Godot 那边用 String::utf8() 转。
// 物品有 id、名字、一格最多叠几份、图标颜色（粮种、各村的土产、铜钱、资源点采来的原料、合成出来的东西）；
// 作物说明「哪样物品是它的种子、收下来得到哪样物品、生长期是基准的几倍、收一格得几份」。
// 眼下粟和菽都是「收成即种子」，但表里种子与产物分开写，将来加「稻谷 → 稻米」这类作物不用改结构。
// 工具：拿在手上（快捷栏选中）采对口的原料，一份的工时除以 speed。

struct ItemDef {
    const char *id;
    const char *name; // 显示名
    int stack; // 一格最多叠几份
    float r, g, b; // 图标、货箱盖板的颜色（sRGB 0–1）
};

struct CropDef {
    const char *id;
    const char *name;
    const char *seed; // 播它要用的物品
    const char *product; // 收它得到的物品
    double grow_factor; // 生长期 = FarmRules::grow_hours × 这个
    int yield; // 收一格得几份产物
};

struct ToolDef {
    const char *item; // 工具这样物品
    const char *products; // 对口的原料 id，逗号分隔
    double speed; // 采对口的原料快几倍
};

namespace catalog {

extern const ItemDef ITEMS[];
extern const int ITEM_COUNT;
extern const CropDef CROPS[];
extern const int CROP_COUNT;
extern const ToolDef TOOLS[];
extern const int TOOL_COUNT;

constexpr int DEFAULT_STACK = 64; // 表里没有的物品一格叠多少

const ItemDef *item(const std::string &p_id); // 没有返回 nullptr
const CropDef *crop(const std::string &p_id);
const CropDef *crop_of_seed(const std::string &p_item); // 这样物品能种出什么；不是种子返回 nullptr
inline bool is_seed(const std::string &p_item) { return crop_of_seed(p_item) != nullptr; }
// 显示名；表里没有就原样返回 id
std::string item_name(const std::string &p_id);
// 一格最多叠几份
int stack_of(const std::string &p_id);
// 图标颜色；表里没有给一个灰绿
void item_color(const std::string &p_id, float &r_r, float &r_g, float &r_b);

const ToolDef *tool(const std::string &p_item); // 不是工具返回 nullptr
// 手上拿着 p_tool 采 p_product 快几倍（不对口 / 不是工具 = 1）
double tool_speed(const std::string &p_tool, const std::string &p_product);

} // namespace catalog
