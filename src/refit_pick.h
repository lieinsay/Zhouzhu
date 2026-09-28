#pragma once

#include <string>
#include <vector>

#include "ship_layout.h"

// 改装视角里鼠标指着什么。不含 Godot 头：上层把屏幕上那一点的射线换到船的局部坐标递进来，这里对格子的盒子求交。
//   · 没拖东西：射线先打着的那一节属于哪个节点（第 0 层的节连着吊在甲板下的船体一起算）——点下去就选中它；
//   · 拖着一种节点：亮光提示的格子 = 这种节点眼下所有落点占的格子并起来。亮格优先：射线打着的最近一格亮格（只有船身挡得住它），
//     就在盖住它的落点里挑中心离命中点最近的那个（复合节往哪头伸，看鼠标指在格子的哪一半）；一格亮的都没打着，才看打着的
//     贴着船、不亮的空格，给出落在那儿的锚点和不能放的原因（红色虚影）；先打着船身算被挡住，什么也不指。
struct PickRay {
    double ox = 0.0, oy = 0.0, oz = 0.0;
    double dx = 0.0, dy = 0.0, dz = -1.0;
};

struct CellBox {
    double x0 = 0.0, y0 = 0.0, z0 = 0.0;
    double x1 = 0.0, y1 = 0.0, z1 = 0.0;
};

constexpr double HULL_DEPTH_M = 3.5; // 第 0 层甲板面往下：甲板板 0.7 + 船体 2.8
CellBox cell_box(const Cell &p_cell, bool p_with_hull = false);
// 一个节点占的格子外包的盒子（画虚影、选中框用）
CellBox footprint_box(const Cell &p_anchor, ModuleKind p_kind, bool p_with_hull = false);
// 射线进盒子处的参数 t（≥ 0；起点在盒子里为 0）；打不着返回 false
bool ray_box(const PickRay &p_ray, const CellBox &p_box, double &r_t);

struct RefitHover {
    enum Type {
        NONE,
        NODE, // 船上的一个节点
        SPOT, // 拖着的节点的落点
    };
    Type type = NONE;
    Cell anchor; // NODE：那个节点的锚点；SPOT：拖着的节点落在这儿的锚点
    bool legal = false; // SPOT：能不能放
    std::string reason; // SPOT 且不能放：为什么
    bool operator==(const RefitHover &p_other) const {
        return type == p_other.type && anchor == p_other.anchor && legal == p_other.legal && reason == p_other.reason;
    }
    bool operator!=(const RefitHover &p_other) const { return !(*this == p_other); }
};

// 亮光提示的格子：这些落点占的格子并起来（不重复）
std::vector<Cell> glow_cells(const std::vector<Cell> &p_placements, ModuleKind p_kind);
// 没拖东西：射线指着船上的哪个节点
RefitHover pick_node(const ShipLayout &p_layout, const PickRay &p_ray);
// 拖着 p_kind：p_placements 是 layout_placements(p_layout, p_kind) 的结果
RefitHover pick_spot(const ShipLayout &p_layout, ModuleKind p_kind, const std::vector<Cell> &p_placements, const PickRay &p_ray);
