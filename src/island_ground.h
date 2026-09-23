#pragma once

#include "island_field.h"
#include "map_ground.h"
#include "villager_model.h"

// 一座游戏尺度的岛（IslandField）当作地图的地面。不含 Godot 头，只持引用，须比 field 活得短。
// 坐标是岛的局部坐标（与 IslandField 同）。
//   勘察高 = 高度场双线性；走路高 = 与地面网格同样的三角切法（碰撞就是这么铺的）；
//   水 = 最近节点在河里（水深取最近节点）；离水 = 最近节点到河中线的距离；水线 = 整条河；
//   停机坪 = 码头半径 dock_apron_m 的圆；船坞坪 = IslandField::yard；路 = 最近节点到路中线的距离；
//   地块格 = 高度场的格子（节点 (i, j) 恰是高度场节点，格点事实直接读数组）。
class IslandGround : public MapGround {
    const IslandField &field;
    double dock_apron_m;

    // 最近的高度场节点；越界返回 false
    bool nearest_node(double p_x, double p_z, size_t &r_k) const;

public:
    IslandGround(const IslandField &p_field, double p_dock_apron_m) :
            field(p_field), dock_apron_m(p_dock_apron_m) {}

    bool site_height(double p_x, double p_z, double &r_y) const override { return field.sample_height(p_x, p_z, r_y); }
    bool walk_height(double p_x, double p_z, double &r_y) const override;
    double water_depth(double p_x, double p_z) const override;
    WaterKind water_at(double p_x, double p_z) const override;
    double water_distance(double p_x, double p_z) const override;
    void water_lines(double p_x0, double p_z0, double p_x1, double p_z1, std::vector<WaterLine> &r_lines) const override;
    bool on_dock(double p_x, double p_z, double p_margin) const override;
    bool on_yard(double p_x, double p_z, double p_margin) const override { return field.yard.contains(p_x, p_z, p_margin); }
    bool on_trail(double p_x, double p_z, double p_margin) const override;
    PlotLattice plot_lattice() const override;
    bool plot_node(const PlotLattice &p_lattice, int p_i, int p_j, PlotNode &r_node) const override;
};

// 导航格铺满整座岛：格子边长 p_rules.cell_m，从 (−ext, −ext) 起
void island_nav_window(const IslandField &p_field, const NavRules &p_rules, double &r_x0, double &r_z0, int &r_cols, int &r_rows);
