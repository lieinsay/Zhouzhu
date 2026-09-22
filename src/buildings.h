#pragma once

#include <string>

// 建筑表。不含 Godot 头；名字是 UTF-8（两个编译目标都开了 /utf-8），Godot 那边用 String::utf8() 转。
// 每种建筑只是一组尺寸：占地（面阔 × 进深，米）、台基露出地面多高、墙高、屋脊比檐口高多少。
// 几何是占位的：台基 + 墙 + 悬山顶 + 门（门开在进深方向的正面，建筑自己的 +z 那一面）。眼下只是摆设：进不去、用不了。
// 选址规矩不在这里（见 settlement_model.h），这里只回答「这种建筑多大」。

struct BuildingDef {
    const char *id; // "house" / "hall" / "granary" / "tower"
    const char *name; // 显示名
    double width_m; // 面阔：沿建筑自己的 x
    double depth_m; // 进深：沿建筑自己的 z，门在 +z 那一面
    double plinth_m; // 台基露出地面多高（地坪 = 占地内最高的地面 + 这个）
    double wall_m; // 墙高：台基面到檐口
    double roof_m; // 屋脊比檐口高多少
};

namespace buildings {

extern const BuildingDef DEFS[];
extern const int COUNT;

const BuildingDef *by_id(const std::string &p_id); // 没有返回 nullptr
int index_of(const std::string &p_id); // 没有返回 −1
inline const BuildingDef &at(int p_index) { return DEFS[p_index]; }

} // namespace buildings
