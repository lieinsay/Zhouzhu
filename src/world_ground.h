#pragma once

#include <vector>

#include "dock_site.h"
#include "ground_query.h"
#include "settlement_model.h"
#include "world_terrain.h"

// 真实比例岛群（WorldTerrain）对码头几何、选址勘察、导航格的适配。不含 Godot 头，只持引用，无状态。
//
// 码头：生成器给的点在 100 m 格上，离真正的岸线还差几十米。外法线取岸距场的负梯度，
// 沿它自外向内找岸缘点（dock_site 的 plan_dock，量的是不算停机坪的原地形）。
// 村子：用局部坐标（原点在村心）——Godot 那头单精度，村子离群心几十公里，局部坐标才不抖。
// 禁区：溪涧（100 m 格上的溪涧格心连成的中线两侧）、河道与湖、码头的停机坪。

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

struct WorldSiteRules {
    double stream_half_width_m = 12.0; // 溪涧中线两侧各让出这么宽
    double pad_margin_m = 4.0; // 停机坪外再让出这么多
};

// 勘察的地面（村子局部坐标）
class WorldSiteGround : public SiteGround {
    const WorldTerrain &terrain;
    double origin_x, origin_z;
    std::vector<DockPlan> pads; // 附近的停机坪（世界坐标）
    WorldSiteRules rules;

public:
    // 只收离原点 p_pad_radius 以内的停机坪
    WorldSiteGround(const WorldTerrain &p_terrain, double p_origin_x, double p_origin_z, double p_pad_radius, const WorldSiteRules &p_rules = WorldSiteRules());
    bool site_height(double p_x, double p_z, double &r_y) const override;
    SiteBlock site_block(double p_x, double p_z) const override;
    // 世界坐标上该点离最近的溪涧中线多远（附近没有溪涧返回很大的数）
    double stream_distance(double p_wx, double p_wz) const;
};

// 导航的地面（村子局部坐标）
class WorldNavGround : public NavGround {
    const WorldTerrain &terrain;
    double origin_x, origin_z;

public:
    WorldNavGround(const WorldTerrain &p_terrain, double p_origin_x, double p_origin_z) :
            terrain(p_terrain), origin_x(p_origin_x), origin_z(p_origin_z) {}
    bool walk_height(double p_x, double p_z, double &r_y) const override { return terrain.surface(origin_x + p_x, origin_z + p_z, r_y); }
};
