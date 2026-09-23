#pragma once

#include <cstdint>
#include <vector>

#include "ground_query.h"
#include "settlement_model.h"

// 一张地图的地面：玩法（农田、盖房、村民走路与挑水）问地面的全部问题都在这里。纯虚，不含 Godot 头。
// 游戏尺度的岛（IslandGround）、真实比例的岛群（WorldGround）、测试用的平板岛各实现一份；
// 规矩（哪里算禁区、哪里能垦、墒情怎么算）只写一份，放在用它的地方（ground_block、farm_model），只通过这些问题取事实。
//
// 坐标是实现者自己的局部坐标（岛的局部坐标 / 以某点为原点的岛群坐标），x 向东、z 向南、y 向上。
// 回答都是「这一点」的事实：有没有地面、多高、是不是水、离水多远、在不在码头的坪 / 船坞的坪 / 路上、是不是熟地。

enum class WaterKind : uint8_t {
    NONE,
    RIVER, // 河（岛上）
    STREAM, // 溪涧（岛群，含两侧不盖房的河滩）
    POND, // 河道、湖这类大片水面（岛群）
};

// 水的中线上的一点：挑水的人在它两侧找干地站
struct WaterPoint {
    double x = 0.0, z = 0.0;
    double half_width = 0.0; // 水面半宽
};
using WaterLine = std::vector<WaterPoint>; // 至少两点，顺着水排

// 地块格：节点 (i, j) 在 (x0 + (j − origin)·cell, z0 + (i − origin)·cell)，格 (i, j) 以节点 (i, j) 为左上角（i 沿 z、j 沿 x）。
// origin 让岛的格子与高度场的节点逐位重合（岛：x0 = 0、origin = (n − 1) / 2，与 IslandField::coord 同一算式）。
// bounded 为假时格子无边（岛群），真时只有 [i_min, i_max) × [j_min, j_max) 有效。
struct PlotLattice {
    double x0 = 0.0, z0 = 0.0;
    double origin = 0.0;
    double cell_m = 2.5;
    bool bounded = false;
    int i_min = 0, j_min = 0, i_max = 0, j_max = 0;

    double node_x(int p_j) const { return (p_j - origin) * cell_m + x0; }
    double node_z(int p_i) const { return (p_i - origin) * cell_m + z0; }
    double center_x(int p_j) const { return node_x(p_j) + 0.5 * cell_m; }
    double center_z(int p_i) const { return node_z(p_i) + 0.5 * cell_m; }
    bool contains(int p_i, int p_j) const { return !bounded || (p_i >= i_min && p_j >= j_min && p_i < i_max && p_j < j_max); }
    // 点落在哪一格（向下取整）；越界返回 false
    bool cell_of(double p_x, double p_z, int &r_i, int &r_j) const;
    // 同一套格子换一个坐标系：新坐标 = 旧坐标 − (p_dx, p_dz)
    PlotLattice shifted(double p_dx, double p_dz) const;
};

// 地块格点上的事实（农田勘察按四个角取）
struct PlotNode {
    bool ground = false;
    double y = 0.0;
    bool wet = false; // 在水里
    double water_m = 1e18; // 离水中线多远
};

class MapGround : public NavGround {
public:
    static constexpr double FAR = 1e18;

    // 勘察（盖房、码头）用的地面高；没有地面返回 false。走路用的是 NavGround::walk_height（与碰撞网格一致）
    virtual bool site_height(double p_x, double p_z, double &r_y) const = 0;

    // 水：这一点是不是水；离最近的水中线多远（附近没有水给 FAR）；框里的水中线（挑水用）
    virtual WaterKind water_at(double p_x, double p_z) const { return WaterKind::NONE; }
    virtual double water_distance(double p_x, double p_z) const { return FAR; }
    virtual void water_lines(double p_x0, double p_z0, double p_x1, double p_z1, std::vector<WaterLine> &r_lines) const {}

    // 禁区：这一点在不在码头的停机坪上 / 船坞的坪上 / 路上（各自再往外让 p_margin）
    virtual bool on_dock(double p_x, double p_z, double p_margin) const { return false; }
    virtual bool on_yard(double p_x, double p_z, double p_margin) const { return false; }
    virtual bool on_trail(double p_x, double p_z, double p_margin) const { return false; }

    // 熟地：地表本来就是田（岛群的可耕地、梯田），不靠近水也能种
    virtual bool fertile(double p_x, double p_z) const { return false; }
    // 这一点属于哪座岛（地图自己的编号）
    virtual int island_at(double p_x, double p_z) const { return 0; }

    // 农田的地块格与格点事实。默认的格点事实按坐标取；岛上的格点就是高度场节点，直接读数组
    virtual PlotLattice plot_lattice() const { return PlotLattice(); }
    virtual bool plot_node(const PlotLattice &p_lattice, int p_i, int p_j, PlotNode &r_node) const;
};

// 禁区规矩（盖房）：一点压着的最要紧的那一样。水 > 停机坪 > 船坞坪 > 路
SiteBlock ground_block(const MapGround &p_ground, double p_x, double p_z, const SiteRules &p_rules);

// 盖房用的地面：MapGround + 选址规矩。只持引用
class MapSiteGround : public SiteGround {
    const MapGround &ground;
    SiteRules rules;

public:
    MapSiteGround(const MapGround &p_ground, const SiteRules &p_rules) :
            ground(p_ground), rules(p_rules) {}
    bool site_height(double p_x, double p_z, double &r_y) const override { return ground.site_height(p_x, p_z, r_y); }
    SiteBlock site_block(double p_x, double p_z) const override { return ground_block(ground, p_x, p_z, rules); }
};
