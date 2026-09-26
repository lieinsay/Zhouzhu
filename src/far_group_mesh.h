#pragma once

#include <cstdint>
#include <vector>

#include "planet_data.h"

// 远处岛群的低模（行星计划 P1，docs/PLAN-PLANET.md 第 4.3 节「60–600 km」）：按一份粗版（PlanetLod）建顶面、崖壁与倒锥的岛底。不含 Godot 头。
//   顶面：粗版格子的角上一个点，高 = 相邻实地格「块内最高点」的平均（远处看山的剪影）；岸上的角取该岛的岸缘高；
//         颜色按地表类（与岛群的地皮同一份调色，world_grid 的 landcover_color）、陡了露岩；季相份额（草、田、灌丛、林）同地面块，着色器按时令换季、盖雪。
//   崖壁：实地格挨着虚空的边，从岸缘垂到崖脚（岸缘 − 崖高）；岛底：从崖脚往里收成倒锥，深按离岸多远的平方根——
//         与游戏里岛群的岛体同一式（world_terrain 的 island_body：龙骨 = max(生成器的龙骨, 25·√最深岸距)、崖高 = 龙骨的 12%，夹在 30–160 m）。
// 顶点已按真球面弯过：位置是该群心切平面坐标里的 T(q)（q 是群平面坐标：x 东、y 上、z 南），法线按该点的局部系转过；
// 上层只要按「群心相对相机」的刚体变换摆上去（PlanetView 用双精度算），不必再在着色器里弯。
// 另带每个顶点的海拔（群平面的 y）与「顶面 / 岛体」的标记，着色器按它算时令与积雪。

struct FarGroupRules {
    double solid_land = 0.5; // 陆地占比过这个算实地格
    double cliff_m = 20.0; // 生成器的崖高（粗版里没有，取岛群的缺省）
    double keel_k = 25.0, keel_exp = 0.5; // 同 world_terrain.cpp 的 KEEL_K / KEEL_EXP
    double wall_share = 0.12, wall_min_m = 30.0, wall_max_m = 160.0;
    double rock_slope = 0.86; // 顶面法线的 y 低于它开始露岩（同地面块：约 30°）
};

struct FarGroupMesh {
    int node = -1;
    double res_m = 0.0;
    std::vector<float> positions; // xyz（切平面坐标，已弯过）
    std::vector<float> normals; // xyz
    std::vector<float> colors; // rgba（sRGB）
    std::vector<uint8_t> season; // 每顶点 4 字节：草、田、灌丛、林的份额（0..255，岩、崖壁、岛底是 0）
    std::vector<float> uvs; // 每顶点 2 个：海拔（米，群平面的 y）、顶面 1 / 崖壁与岛底 0
    std::vector<int32_t> indices; // 正面顺时针（Godot 约定）
    double aabb_min[3] = { 0.0, 0.0, 0.0 }, aabb_max[3] = { 0.0, 0.0, 0.0 };
    double peak_x = 0.0, peak_y = 0.0, peak_z = 0.0; // 最高的角（群平面坐标，没弯过）
    int solid_cells = 0;

    bool empty() const { return indices.empty(); }
    size_t vertex_count() const { return positions.size() / 3; }
};

// p_radius：弯曲用的 R（PlanetShape::radius()）
FarGroupMesh build_far_group_mesh(const PlanetLod &p_lod, double p_radius, const FarGroupRules &p_rules = FarGroupRules());
