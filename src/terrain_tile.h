#pragma once

#include <cstdint>
#include <vector>

#include "world_terrain.h"

// 岛群地面的分块：一棵四叉树盖住整个栅格，离焦点近的块细分到叶（128 m、64 格即 2 m 一格），
// 远处用大块粗格（同样 64 格，块越大格越粗）。块的网格全按 WorldTerrain 取样，所以相邻块只在
// 精度不同处有细缝，靠沿块边垂下去的一圈裙边遮住。
// 本文件不含 Godot 头：选块、建网格都是纯函数；输出平铺的 float 数组，由上层转成引擎的数组。

struct TileKey {
    int level = 0; // 0 = 根
    int ix = 0, iz = 0;

    bool operator==(const TileKey &p_other) const { return level == p_other.level && ix == p_other.ix && iz == p_other.iz; }
    bool operator<(const TileKey &p_other) const {
        return level != p_other.level ? level < p_other.level : ix != p_other.ix ? ix < p_other.ix : iz < p_other.iz;
    }
    uint64_t packed() const { return (uint64_t(level) << 48) | (uint64_t(uint32_t(ix) & 0xFFFFFF) << 24) | uint64_t(uint32_t(iz) & 0xFFFFFF); }
};

class TileLayout {
    double x0 = 0.0, z0 = 0.0; // 根块西北角
    double root_size = 0.0;
    int max_level = 0;
    // 「这片可能有地」的二维前缀和（按栅格格子），查任意矩形 O(1)
    int rows = 0, cols = 0;
    double res = 1.0, grid_x = 0.0, grid_z = 0.0;
    std::vector<int32_t> land_sat;

public:
    static constexpr double LEAF_M = 128.0;
    static constexpr int CELLS = 64; // 每块每边的格数

    TileLayout() = default;
    explicit TileLayout(const WorldGrid &p_grid);

    int get_max_level() const { return max_level; }
    double size(int p_level) const { return root_size / double(1 << p_level); }
    double step(int p_level) const { return size(p_level) / CELLS; }
    void corner(const TileKey &p_key, double &r_x, double &r_z) const;
    // 块内（外扩 p_margin 米）是否可能有地
    bool may_have_land(const TileKey &p_key, double p_margin = 0.0) const;
    // 盖住该点的叶块
    TileKey leaf_at(double p_x, double p_z) const;
    bool contains(const TileKey &p_outer, const TileKey &p_inner) const;
    bool overlaps(const TileKey &p_a, const TileKey &p_b) const { return contains(p_a, p_b) || contains(p_b, p_a); }

    // 以焦点为准选出这一刻该有的块（只含可能有地的）：焦点到块的水平距离 < 块边长 × p_split 就细分
    std::vector<TileKey> select(double p_focus_x, double p_focus_z, double p_split = 1.25) const;
};

struct TileMesh {
    TileKey key;
    double origin_x = 0.0, origin_z = 0.0; // 块的西北角：顶点坐标相对它（远离原点时 float 也够准）
    std::vector<float> positions; // xyz
    std::vector<float> normals; // xyz
    std::vector<float> colors; // rgba，sRGB
    std::vector<int32_t> indices; // 正面顺时针（Godot 约定）
    std::vector<float> collision; // 每三个点（xyz）一个三角面；只有叶块才有
    int land_quads = 0;

    bool empty() const { return indices.empty(); }
};

TileMesh build_tile_mesh(const WorldTerrain &p_terrain, const TileLayout &p_layout, const TileKey &p_key);
