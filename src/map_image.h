#pragma once

#include <cstdint>
#include <vector>

#include "world_grid.h"

// 地图底图：一格一像素（行 = z 自北向南，列 = x 自西向东），地表色 × 山影，岸线描深，溪涧染蓝，虚空全透明。
// 本文件不含 Godot 头：输出 RGBA8 字节，上层做成贴图。
struct MapImage {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
};

MapImage build_map_image(const WorldGrid &p_grid);

// 世界 (x, z) ↔ 底图像素坐标（像素中心为 x.5）
void world_to_map(const WorldGrid &p_grid, double p_x, double p_z, double &r_u, double &r_v);
void map_to_world(const WorldGrid &p_grid, double p_u, double p_v, double &r_x, double &r_z);
