#pragma once

#include <cstdint>

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/static_body3d.hpp>

#include "ground_host.h"
#include "settlement.h"
#include "villagers.h"
#include "world.h"
#include "world_sites.h"

class Hinterland;

// 岛群里的一个村 / 一户散户：节点放在村心（y = 0、不转），局部坐标原点就是村心——单精度在几十公里外也不抖。
// 进树时按 plan_world_village 铺底清单，挂一个 Settlement 子节点盖房子（布置模式的改动从 layout_path 叠上去）；
// 村民（Villagers）要走近了才招（set_folk），导航格只铺村子周围一圈。
// 它是地面宿主：地面、禁区（溪涧、码头的坪、船坞坪）都从 World 的连续地形来（WorldGround，原点挪到村心），
// 村民种的是 World 下面那片农田。建与拆都由 SiteStream 按距离决定。
// 村子的账（谁住哪、在干什么、仓、水缸、认领的地）在远处账房（Hinterland）：招村民时借来交给 Villagers，散了还回去接着在远处记。
class WorldVillage : public godot::Node3D, public GroundHost {
    GDCLASS(WorldVillage, godot::Node3D)

    World *world = nullptr;
    SiteSettlement site;
    uint64_t seed = 1;
    godot::String layout_path; // 布置模式的改动存在哪
    VillagePlan plan;
    bool planned = false;
    Settlement *settlement = nullptr;
    Villagers *villagers = nullptr;
    Hinterland *hinterland = nullptr; // 可以没有：那就走近了从头招人
    int site_index = -1; // sites.settlements 下标（账房里的账号）

protected:
    static void _bind_methods();

public:
    static constexpr double NAV_MARGIN_M = 30.0; // 导航格在房子外接框外再铺这么宽

    // 进树之前调；p_layout_path 是布置模式改动的文件（可以还不存在）；p_hinterland 是远处账房（可以为 nullptr），p_index 是账号
    void setup(World *p_world, const SiteSettlement &p_site, uint64_t p_seed, const godot::String &p_layout_path, Hinterland *p_hinterland = nullptr, int p_index = -1);
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
    bool is_edited() const { return settlement && settlement->is_dirty(); } // 布置模式改过、还没存

    // GroundHost
    godot::Node3D *host_node() override { return this; }
    const godot::Node3D *host_node() const override { return this; }
    bool host_ready() const override { return planned; }
    std::unique_ptr<MapGround> host_ground() const override;
    void host_nav_window(const SettlementLayout &p_layout, const NavRules &p_rules, double &r_x0, double &r_z0, int &r_cols, int &r_rows) const override;
    uint64_t host_seed() const override { return seed * 2654435761ull + uint64_t(site.id) * 2ull + (site.hamlet ? 1ull : 0ull); }
    Farm *host_farm() const override { return world ? world->host_farm() : nullptr; }
    void host_origin(double &r_x, double &r_z) const override {
        r_x = site.x;
        r_z = site.z;
    }
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
