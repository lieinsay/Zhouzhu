#pragma once

#include <cstddef>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// 一个岛群的 100 m 栅格：skyisle-gen 第三层产物经 tools/export_skyisle.py 转出的 terrain.bin（格式见那个脚本）。
// 本文件不含 Godot 头：从字节解析，按连续坐标取样，没有状态。
//
// 游戏坐标：x 向东、z 向南、y 向上，原点 = 群心，单位米；y 就是云带顶以上的高度。
// 格 (r, c) 的格心在 (x0 + c·res, z0 + r·res)。

struct WorldGrid {
    int rows = 0, cols = 0;
    double res = 100.0;
    double x0 = 0.0, z0 = 0.0;
    std::vector<float> height; // 云带顶以上的米数；虚空格是最近陆地格的值
    std::vector<float> shore; // 到岸线的有符号距离（米），陆地为正
    std::vector<uint8_t> island; // 最近的岛号
    std::vector<uint8_t> landcover; // 地表类下标，0 = 虚空
    std::vector<uint8_t> stream; // 1 = 溪涧
    // 水文（v2 才有；v1 为空，那就没有河网）：D8 流向码与汇流面积，见 export_skyisle.py
    std::vector<uint8_t> flow; // 0 = 不流；低四位 1..8 = FLOW_OFFSETS 的下标 + 1；FLOW_TO_VOID = 流出岸缘
    std::vector<float> flow_acc; // 平方公里，含本格
    // 河道与地形区（v3 才有；旧版为空）：生成器下切过的河道格的河宽 / 水深（米，溪涧为湿季值，非河道格 0），地形区下标
    std::vector<float> river_width;
    std::vector<float> river_depth;
    std::vector<uint8_t> zone; // 0 虚空 1 高山 2 山地 3 丘陵 4 台地平原 5 河谷 6 崖缘 7 水域（skyisle-gen resources.py 的 ZONE_NAMES）
    // 资源（v5 才有；旧版为空）：散的赋存场（每类一层、品位 × 255，可叠）与片号（deposit id + 1，0 = 不在片里）
    std::vector<uint8_t> field; // FIELD_COUNT 层，层内行主序
    std::vector<uint16_t> patch;
    // v6: authoritative natural and human landscapes; shore is a float subcell distance.
    std::vector<uint8_t> cultivable, cultivated, fallow_years, natural_cover, landuse, lith, cloud_forest, rockwall_dir;
    std::vector<float> rockwall, rain, runoff, bed_height, water_table, condense;
    std::vector<int32_t> polder;
    struct RiverSource {
        int island = 0, down = -1, join = -1, exit = 0, basin = -1;
        double outlet_drop = 0.0; // world.json landscape.falls 的 kind=0 崖瀑
        // rivers.json COLS, preserved in generator order (row/col identify cell centres).
        std::vector<std::array<float, 20>> points;
    };
    std::vector<RiverSource> river_sources;
    std::vector<std::vector<float>> basin_runoff;
    bool authoritative_rivers = false;

    // 赋存场的层（同 export_skyisle.py 的 FIELD_KINDS、生成器的 res_field）
    enum Field : int {
        FIELD_ORE = 0,
        FIELD_SULFUR,
        FIELD_PLACER,
        FIELD_CLAY,
        FIELD_GRAVEL,
        FIELD_STONE,
        FIELD_SALT,
        FIELD_COUNT,
    };
    static const char *field_key(int p_field); // 生成器的键：ore / sulfur / placer / clay / gravel / stone
    static int field_of(const char *p_key); // 键 → 层；不是散的返回 −1

    static constexpr uint8_t FLOW_TO_VOID = 0x10;
    static constexpr int FLOW_OFFSETS[8][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }, { -1, -1 }, { -1, 1 }, { 1, -1 }, { 1, 1 } }; // (dr, dc)

    bool has_hydro() const { return !flow.empty(); }
    bool has_channels() const { return !river_width.empty(); }
    bool has_resources() const { return !field.empty(); }
    size_t index(int p_r, int p_c) const { return size_t(p_r) * size_t(cols) + size_t(p_c); }
    bool empty() const { return rows == 0 || cols == 0; }
    double min_x() const { return x0 - 0.5 * res; }
    double min_z() const { return z0 - 0.5 * res; }
    double max_x() const { return x0 + (cols - 0.5) * res; }
    double max_z() const { return z0 + (rows - 0.5) * res; }

    // 连续坐标 → 格坐标（格心为整数）
    double col_of(double p_x) const { return (p_x - x0) / res; }
    double row_of(double p_z) const { return (p_z - z0) / res; }

    // Catmull-Rom 双三次，出界按边缘格取
    double height_at(double p_x, double p_z) const;
    double shore_at(double p_x, double p_z) const;
    // 最近的格（出界夹到边上）
    size_t nearest_cell(double p_x, double p_z) const;
    int land_count() const;
    // 赋存场：某一格的品位（0–1）；连续坐标上双线性插值（格心之间渐变，出界与没有资源栅格为 0）
    double field_cell(int p_field, size_t p_index) const { return field.empty() ? 0.0 : field[size_t(p_field) * height.size() + p_index] / 255.0; }
    double field_at(int p_field, double p_x, double p_z) const;
    // 片号：该点最近的格属于哪一片（deposit id）；不在片里、没有资源栅格返回 −1
    int patch_at(double p_x, double p_z) const;
    int patch_cell(size_t p_index) const { return patch.empty() ? -1 : int(patch[p_index]) - 1; }
};

// 地表类的颜色（sRGB 0..1，地图底图与地面网格共用一套）
struct GroundColor {
    float r, g, b;
};
GroundColor landcover_color(int p_class);

// 失败返回 false，r_error 说为什么
bool parse_world_grid(const uint8_t *p_data, size_t p_size, WorldGrid &r_grid, std::string &r_error);
