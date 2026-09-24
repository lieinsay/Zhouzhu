#pragma once

#include <string>

// 建筑表。不含 Godot 头；名字是 UTF-8（两个编译目标都开了 /utf-8），Godot 那边用 String::utf8() 转。
// 每种建筑只是一组尺寸：占地（面阔 × 进深，米）、台基露出地面多高、墙高、屋脊比檐口高多少。
// 不全是房屋：船台（"berth"，见 land_berth.h）只是一块台面，没有墙、顶、屋里，村民不住也不串门（house = false）。
// 这是建筑的「外壳」：占地、台基、墙高、屋顶与分几层。屋里什么样（门窗、隔墙、家具、梯子）由房屋模板说（house_template.h），
// 同一种外壳可以有好几套模板；几何由 building_parts 按外壳 + 模板拼出来，人走得进去。
// 选址规矩不在这里（见 settlement_model.h），这里只回答「这种建筑多大」。

struct BuildingDef {
    const char *id; // "house" / "hall" / "granary" / "tower" / "berth"
    const char *name; // 显示名
    double width_m; // 面阔：沿建筑自己的 x
    double depth_m; // 进深：沿建筑自己的 z，门在 +z 那一面
    double plinth_m; // 台基露出地面多高（地坪 = 占地内最高的地面 + 这个）
    double wall_m; // 墙高：台基面到檐口
    double roof_m; // 屋脊比檐口高多少
    int levels = 1; // 墙高里分几层（望楼三层，每层 wall_m / levels 高；层间有楼板，梯子上下）
    bool house = true; // 是房屋（有墙有顶、能进去、有房屋模板）；船台不是
};

namespace buildings {

extern const BuildingDef DEFS[];
extern const int COUNT;

const BuildingDef *by_id(const std::string &p_id); // 没有返回 nullptr
int index_of(const std::string &p_id); // 没有返回 −1
inline const BuildingDef &at(int p_index) { return DEFS[p_index]; }

} // namespace buildings
