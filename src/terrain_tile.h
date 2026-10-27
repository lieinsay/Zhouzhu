#pragma once

#include <cstdint>
#include <vector>

#include "terrain_edits.h"
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

    // 改地形的格子（terrain_edits.h）：原点取根块的西北角，两格正好一个叶块的粗格、16 格一块正好叶块的八分之一
    EditGrid edit_grid() const;
    // 改地形的一块（16 m）落在哪个叶块里
    TileKey leaf_of_chunk(int p_ci, int p_cj) const;
};

struct TileMesh {
    TileKey key;
    double origin_x = 0.0, origin_z = 0.0; // 块的西北角：顶点坐标相对它（远离原点时 float 也够准）
    std::vector<float> positions; // xyz
    std::vector<float> normals; // xyz
    std::vector<float> colors; // rgba，sRGB
    // 季相份额（每顶点 4 个字节，0..255）：草（枯黄）、田（休耕 / 青苗 / 黄熟）、灌丛（冬天发褐）、林（落叶的秋色、光秃）；
    // 崖壁、岛底、露岩、河床是 0。地皮着色器（game/shaders/ground.gdshader）按它与时令给顶点色换季
    std::vector<uint8_t> season;
    // 实际表面语义：岩、砂砾、草基底、林地；余量为土。仅渲染属性，不改变源数据或碰撞。
    std::vector<uint8_t> surface;
    std::vector<int32_t> indices; // 正面顺时针（Godot 约定）
    std::vector<float> collision; // 每三个点（xyz）一个三角面；只有叶块才有
    int land_quads = 0;

    bool empty() const { return indices.empty(); }
};

// p_holes：叶块里改过的块（EditGrid::chunk_key，见 tile_edit_chunks）——这些块里实心粗格的顶面与块边的裙边不出，由补丁（build_edit_patch）画；
// 崖壁、岛底照出。粗块不看它
TileMesh build_tile_mesh(const WorldTerrain &p_terrain, const TileLayout &p_layout, const TileKey &p_key, const std::vector<int64_t> &p_holes = {});

// 叶块里有哪几块改过（按键排好）
std::vector<int64_t> tile_edit_chunks(const TileLayout &p_layout, const TerrainEdits &p_edits, const TileKey &p_key);

// 改过的一块（16 × 16 格）的补丁：块里每个实心粗格逐 1 m 格出顶面——原地形照叶块的粗格插值（同一条对角线，没改过的格与叶块逐位接得上），
// 加上级数 × 0.5 m；格与格之间级数不同出一道竖直的坎（朝低的那格），坎归高的那格所在的块出，块外没改动时归这一块出（两头都出）；
// 坎在叶块边上时往低的那格让 1 cm（邻块的裙边在那条线上）。改过的格顶面是土色（挖到土层以下是石色）、不换季；坎按深浅一条条染土 / 石。
// 没改过的格挨着粗格的块边垂一圈裙边（不进碰撞），遮住 T 形接缝。顶点相对这块所在的叶块的西北角（与叶块同一套坐标）；带碰撞（顶面 + 坎）
TileMesh build_edit_patch(const WorldTerrain &p_terrain, const TileLayout &p_layout, const TerrainEdits &p_edits, int p_ci, int p_cj);

// 一格原地形的土层多厚（米，0.5 m 一档）：往下挖过这么深出石料。按格心的地表份额（裸岩 0、崖缘 0.5、高山草甸 1、湿地 2.5、别的 2）
double soil_depth_at(const WorldTerrain &p_terrain, double p_x, double p_z);
