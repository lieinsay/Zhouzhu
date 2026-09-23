#pragma once

#include <vector>

#include "dock_site.h"
#include "map_ground.h"
#include "world_terrain.h"
#include "yard_site.h"

// 真实比例岛群（WorldTerrain）对码头几何、船坞坪、地图地面的适配。不含 Godot 头，只持引用，无状态。
//
// 码头：生成器给的点在 100 m 格上，离真正的岸线还差几十米。外法线取岸距场的负梯度，
// 沿它自外向内找岸缘点（dock_site 的 plan_dock，量的是不算停机坪的原地形）。
// 船坞坪：主码头旁，按 yard_site 的规矩在陆地上找坪；溪涧与水面也算「凿不得」，所以坪绕开它们。
// 地面（WorldGround）：坐标原点可以挪（岛群里的一个村用村心作原点——Godot 那头单精度，村子离群心几十公里，局部坐标才不抖）。
//   有河网时（栅格 v2，见 world_river.h）：水 = 河槽（离中线不到平岸半宽 + channel_margin_m），河漫滩单列（不盖房、能垦）；
//     离水 = 到河中线的距离（河的影响范围外给 FAR）；挑水的线 = 河中线，水面半宽按 runoff 时的水情；水深也按 runoff（导航格用）。
//   没有河网时（v1）：水 = 河道 / 湖格（水面）与溪涧中线两侧 stream_bank_m（河滩）；离水 = 到溪涧、河道、湖格心连成的中线的距离；
//   湖格（地表类 11）两种情况都是水面。
//   停机坪 = 码头坪半径 + 坡肩；船坞坪 = WorldTerrain 的 yards；熟地 = 可耕地、梯田；岛号 = 最近格的岛；
//   地块格与岛群坐标对齐（2.5 m，节点在岛群坐标的整格上），岛群里不管从哪个村看，同一块地都是同一格。

// 码头的地面：原地形（压平之前）
class WorldDockGround : public DockGround {
    const WorldTerrain &terrain;

public:
    explicit WorldDockGround(const WorldTerrain &p_terrain) :
            terrain(p_terrain) {}
    bool land(double p_x, double p_z) const override { return terrain.is_land(p_x, p_z); }
    bool ground(double p_x, double p_z, double &r_y) const override;
};

DockRules world_dock_rules();
// 该处岸线的外法线（岸距场的负梯度，单位向量）
void world_shore_normal(const WorldTerrain &p_terrain, double p_x, double p_z, double &r_out_x, double &r_out_z);
// 生成器的码头点 → 码头几何（世界坐标）
DockPlan plan_world_dock(const WorldTerrain &p_terrain, double p_x, double p_z, const DockRules &p_rules);

struct WorldGroundRules {
    double stream_bank_m = 12.0; // 没有河网时：溪涧中线两侧各这么宽算水（不盖房、不垦）
    double channel_margin_m = 0.5; // 有河网时：平岸半宽再往外这么多算河槽
    double runoff = 1.0; // 挑水线、水深按这个径流指数的水情（1 = 年均）
    double pad_shoulder_m = 1.0; // 停机坪外半圈是渐回原地形的坡，禁区从坡肩再往外量
    double water_half_width_m = 3.0; // 挑水的人离水中线这么远站
    double water_line_step_m = 10.0; // 水中线每隔这么远一个点
    double plot_cell_m = 2.5; // 地块格边长（与甲乙两岛同）
};

// 船坞坪的规矩：同甲乙两岛，只是岸线进退大（岸线噪声 ±28 m），进深上限放宽
YardRules world_yard_rules();
// 码头旁的船坞坪；在 set_pads 之后调（量的是原地形的陆地，另避开溪涧与水面）
YardPlan plan_world_yard(const WorldTerrain &p_terrain, const DockPlan &p_dock, const YardRules &p_rules, const WorldGroundRules &p_ground = WorldGroundRules());

class WorldGround : public MapGround {
    const WorldTerrain &terrain;
    double origin_x, origin_z; // 本地坐标 (0, 0) 在岛群坐标里的位置
    WorldGroundRules rules;

public:
    WorldGround(const WorldTerrain &p_terrain, double p_origin_x, double p_origin_z, const WorldGroundRules &p_rules = WorldGroundRules()) :
            terrain(p_terrain), origin_x(p_origin_x), origin_z(p_origin_z), rules(p_rules) {}

    bool site_height(double p_x, double p_z, double &r_y) const override { return terrain.surface(origin_x + p_x, origin_z + p_z, r_y); }
    bool walk_height(double p_x, double p_z, double &r_y) const override { return terrain.surface(origin_x + p_x, origin_z + p_z, r_y); }
    WaterKind water_at(double p_x, double p_z) const override;
    double water_distance(double p_x, double p_z) const override;
    void water_lines(double p_x0, double p_z0, double p_x1, double p_z1, std::vector<WaterLine> &r_lines) const override;
    double water_depth(double p_x, double p_z) const override;
    bool on_floodplain(double p_x, double p_z) const override;
    bool on_dock(double p_x, double p_z, double p_margin) const override;
    bool on_yard(double p_x, double p_z, double p_margin) const override;
    bool fertile(double p_x, double p_z) const override;
    int island_at(double p_x, double p_z) const override;
    PlotLattice plot_lattice() const override;

    // 岛群坐标上该点离最近的溪涧中线多远（附近没有溪涧返回很大的数）
    double stream_distance(double p_wx, double p_wz) const;
};
