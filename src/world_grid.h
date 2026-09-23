#pragma once

#include <cstddef>
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

    static constexpr uint8_t FLOW_TO_VOID = 0x10;
    static constexpr int FLOW_OFFSETS[8][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }, { -1, -1 }, { -1, 1 }, { 1, -1 }, { 1, 1 } }; // (dr, dc)

    bool has_hydro() const { return !flow.empty(); }
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
};

// 地表类的颜色（sRGB 0..1，地图底图与地面网格共用一套）
struct GroundColor {
    float r, g, b;
};
GroundColor landcover_color(int p_class);

// 失败返回 false，r_error 说为什么
bool parse_world_grid(const uint8_t *p_data, size_t p_size, WorldGrid &r_grid, std::string &r_error);
