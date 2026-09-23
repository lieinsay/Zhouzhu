#pragma once

#include <godot_cpp/classes/static_body3d.hpp>

#include "flat_ground.h"
#include "ground_host.h"
#include "map_host.h"

// 平板岛测试图：一座圆形、顶面全平的岛，一条溪横穿，一个码头，码头旁一块船坞坪（flat_ground 算），岛心北边一个自动铺的村子（village_plan 算）。
// 它同时是地面宿主（GroundHost：Farm、Shipyard、Settlement、Villagers 挂在它下面）与地图宿主（MapHost：编排只认它）。
// 用途是证明「新地图只要实现这两份宿主接口，所有玩法就跑得起来」：这里除了回答接口、搭个圆盘和栈桥，一行玩法都没有。
// 节点原点在岛心、顶面 y = 0；只平移不转。
class FlatMap : public godot::StaticBody3D, public GroundHost, public MapHost {
    GDCLASS(FlatMap, godot::StaticBody3D)

    godot::String label = U"平板岛码头";
    int64_t seed = 1;
    double radius_m = 160.0;
    double dock_bearing_deg = 0.0;
    // 自动铺的村子（与岛群同一个铺村算法）：村心（岛的局部坐标）、户数；铺好的清单交给下面的 Settlement 当底清单
    double village_x = 0.0, village_z = -70.0;
    int households = 6;

    FlatIsland island;
    bool built = false;

    void build();

protected:
    static void _bind_methods();

public:
    static constexpr double THICKNESS_M = 30.0; // 圆盘多厚（只为看着像座岛）

    void _ready() override;

    // GroundHost
    godot::Node3D *host_node() override { return this; }
    const godot::Node3D *host_node() const override { return this; }
    bool host_ready() const override { return built; }
    std::unique_ptr<MapGround> host_ground() const override;
    void host_nav_window(const SettlementLayout &p_layout, const NavRules &p_rules, double &r_x0, double &r_z0, int &r_cols, int &r_rows) const override;
    uint64_t host_seed() const override { return uint64_t(seed); }
    Farm *host_farm() const override;
    YardPlan host_yard() const override { return island.yard; }

    // MapHost
    godot::Node3D *map_node() override { return this; }
    bool map_ready() const override { return built; }
    double map_surface(const godot::Vector3 &p_global) const override;
    int map_island_at(const godot::Vector3 &p_global) const override;
    int map_dock_count() const override { return island.dock.ok ? 1 : 0; }
    godot::Transform3D map_dock_berth(int p_index) const override;
    godot::Transform3D map_dock_spawn(int p_index) const override;
    godot::Vector3 map_dock_position(int p_index) const override;
    int map_dock_island(int p_index) const override { return 0; }
    godot::String map_dock_label(int p_index) const override { return label; }
    std::vector<Shipyard *> map_shipyards() const override;
    Farm *map_farm_at(const godot::Vector3 &p_global) const override { return host_farm(); }
    GroundHost *map_settlement_at(const godot::Vector3 &p_global) const override { return const_cast<FlatMap *>(this); }

    const FlatIsland &get_island() const { return island; }

    void set_label(const godot::String &p_value) { label = p_value; }
    godot::String get_label() const { return label; }
    void set_seed(int64_t p_value) { seed = p_value; }
    int64_t get_seed() const { return seed; }
    void set_radius_m(double p_value) { radius_m = p_value; }
    double get_radius_m() const { return radius_m; }
    void set_dock_bearing_deg(double p_value) { dock_bearing_deg = p_value; }
    double get_dock_bearing_deg() const { return dock_bearing_deg; }
    void set_village_x(double p_value) { village_x = p_value; }
    double get_village_x() const { return village_x; }
    void set_village_z(double p_value) { village_z = p_value; }
    double get_village_z() const { return village_z; }
    void set_households(int p_value) { households = p_value; }
    int get_households() const { return households; }
};
