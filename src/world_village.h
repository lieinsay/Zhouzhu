#pragma once

#include <cstdint>

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/static_body3d.hpp>

#include "settlement.h"
#include "settlement_host.h"
#include "villagers.h"
#include "world.h"
#include "world_sites.h"

// 岛群里的一个村 / 一户散户：节点放在村心（y = 0、不转），局部坐标原点就是村心——单精度在几十公里外也不抖。
// 进树时按 plan_world_village 铺清单，挂一个 Settlement 子节点盖房子；村民（Villagers）要走近了才招（set_folk），
// 导航格只铺村子周围一圈。它是聚落宿主：地面、禁区（溪涧、码头的坪）都从 World 的连续地形来。
// 建与拆都由 SiteStream 按距离决定。
class WorldVillage : public godot::Node3D, public SettlementHost {
    GDCLASS(WorldVillage, godot::Node3D)

    World *world = nullptr;
    SiteSettlement site;
    uint64_t seed = 1;
    VillagePlan plan;
    bool planned = false;
    Settlement *settlement = nullptr;
    Villagers *villagers = nullptr;

protected:
    static void _bind_methods();

public:
    static constexpr double NAV_MARGIN_M = 30.0; // 导航格在房子外接框外再铺这么宽
    static constexpr double PAD_SEARCH_M = 1500.0; // 离村心这么远以内的码头坪算禁区

    // 进树之前调
    void setup(World *p_world, const SiteSettlement &p_site, uint64_t p_seed);
    void _ready() override;

    // 招 / 散村民（p_max_folk 封顶）
    void set_folk(bool p_on, int p_per_house, int p_max_folk);
    bool has_folk() const { return villagers != nullptr; }

    const SiteSettlement &get_site() const { return site; }
    int get_site_id() const { return site.id; }
    bool is_hamlet() const { return site.hamlet; }
    godot::String get_site_name() const { return godot::String::utf8(site.name.c_str()); }
    double get_radius() const { return plan.radius; }
    int get_missing() const { return plan.missing; }
    Settlement *get_settlement() const { return settlement; }
    Villagers *get_villagers() const { return villagers; }

    // SettlementHost
    godot::Node3D *host_node() override { return this; }
    const godot::Node3D *host_node() const override { return this; }
    bool host_ready() const override { return planned; }
    std::unique_ptr<SiteGround> host_site_ground(const SiteRules &p_rules) const override;
    std::unique_ptr<NavGround> host_nav_ground() const override;
    void host_nav_window(const SettlementLayout &p_layout, const NavRules &p_rules, double &r_x0, double &r_z0, int &r_cols, int &r_rows) const override;
    uint64_t host_seed() const override { return seed * 2654435761ull + uint64_t(site.id) * 2ull + (site.hamlet ? 1ull : 0ull); }
};

// 一个码头的栈桥与碰撞：节点放在岸缘点（y = 0），地面已由 World 的地形压平。
class WorldDock : public godot::StaticBody3D {
    GDCLASS(WorldDock, godot::StaticBody3D)

    int index = -1; // World 的码头下标
    int dock_id = -1;

protected:
    static void _bind_methods() {}

public:
    // 进树之前调
    void setup(const DockPlan &p_plan, int p_index, int p_id);
    int get_index() const { return index; }
    int get_dock_id() const { return dock_id; }
};
