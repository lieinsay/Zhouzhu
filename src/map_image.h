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

// 资源分布底图（地图面板的「资源」层；同生成器调试台的「资源分布」主导视图）：地表压成暗灰的山影，上面每格画最稀的那一类——
// 金属矿 > 硫磺 > 砂金 > 浮石露头 > 鸟粪石 > 泥炭芦苇 > 黏土 > 砂砾 > 石料 > 林木（散的按赋存场，品位够门槛才算，越富越浓；片按片号）。
// p_patch_kinds：片号（deposit id）→ 那一片在 deposits::KINDS 里的下标，不是片为 −1。栅格没有资源层时只有暗底
MapImage build_resource_image(const WorldGrid &p_grid, const std::vector<int> &p_patch_kinds);
// 这一格画的是哪一类（deposits::KINDS 的下标），什么都没有为 −1；品位写进 r_grade（片为 1）
int dominant_resource(const WorldGrid &p_grid, size_t p_cell, const std::vector<int> &p_patch_kinds, double &r_grade);

// 世界 (x, z) ↔ 底图像素坐标（像素中心为 x.5）
void world_to_map(const WorldGrid &p_grid, double p_x, double p_z, double &r_u, double &r_v);
void map_to_world(const WorldGrid &p_grid, double p_u, double p_v, double &r_x, double &r_z);
