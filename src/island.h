#pragma once

#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include "dock_site.h"
#include "ground_host.h"
#include "island_field.h"

// 一座浮空岛。节点原点在岸缘高度（局部 y = 0 即岸缘），岛体向下垂到岛底。
// 进树时按属性生成高度场 → 网格 + 碰撞 + 栈桥（+ 河水）；生成物没有 owner，不会写进场景文件。
// 每次生成完发 rebuilt 信号：长在岛上的东西（Farm、Shipyard、Settlement、Villagers）靠它知道地形换了。
// 它是地面宿主（GroundHost）：地面是 IslandGround（高度场、河、路、停机坪、船坞坪），农田是它下面的 Farm 子节点。
// 几座岛拼成一张地图的是 IslandMap。编辑器里同样生成，改属性即重建，方便摆岛。
class Island : public godot::StaticBody3D, public GroundHost {
    GDCLASS(Island, godot::StaticBody3D)

    godot::String label; // 给人看的名字（HUD、码头名）；空 = 用节点名
    int64_t seed = 1;
    double area_m2 = 120000.0;
    double age = 0.5;
    double relief_m = 60.0;
    double elongation = 1.3;
    double axis_deg = 0.0;
    double dock_bearing_deg = 0.0;
    double cliff_m = 22.0;
    double keel_m = 110.0;

    bool has_river = false;
    bool has_yard = false;

    IslandParams params; // 上一次 rebuild 用的那一份
    IslandField field;
    bool rebuild_queued = false;

    void queue_rebuild();
    void clear_generated();

protected:
    static void _bind_methods();

public:
    // 码头几何（栈桥、泊位、出生点都由它推出，见 dock_builder）
    DockPlan dock_plan() const;

    void _ready() override;

    void rebuild();

    // 泊位接触线：栈桥头外一道缝处、船舷该贴上的位姿。基向 X 指向栈桥（岛内），Z 顺着岸；
    // 船按自己面向栈桥那一侧的船边算原点该在哪（见 Ship::place_at / moor_at）。
    godot::Transform3D get_berth_contact() const;
    godot::Vector3 get_pier_end_position() const;
    // 停机坪上、面朝栈桥的出生位姿
    godot::Transform3D get_spawn_transform() const;
    // 该处（全局 xz）的地面全局高度；没有地面返回 NAN
    double get_surface_height(const godot::Vector3 &p_global) const;
    // 停机坪 → 河滩的路（全局坐标，顺着走的次序）；岛上没有河就没有路，返回空
    godot::PackedVector3Array get_trail_points() const;
    // 河中线（全局坐标，自源头到河口，点在水面高）；没有河返回空
    godot::PackedVector3Array get_river_points() const;

    // 地面宿主（Farm / Shipyard / Settlement / Villagers 挂在岛下面）
    godot::Node3D *host_node() override { return this; }
    const godot::Node3D *host_node() const override { return this; }
    bool host_ready() const override { return field.n > 0; }
    std::unique_ptr<MapGround> host_ground() const override;
    void host_nav_window(const SettlementLayout &p_layout, const NavRules &p_rules, double &r_x0, double &r_z0, int &r_cols, int &r_rows) const override;
    uint64_t host_seed() const override { return uint64_t(seed); }
    Farm *host_farm() const override; // 岛下面的 Farm 子节点
    YardPlan host_yard() const override { return field.yard; }

    // 给同在 C++ 里的上层读
    const IslandField &get_field() const { return field; }
    const IslandParams &get_params() const { return params; }
    godot::String get_label() const { return label.is_empty() ? godot::String(get_name()) : label; }
    void set_label(const godot::String &p_value) { label = p_value; }

    void set_seed(int64_t p_value);
    int64_t get_seed() const { return seed; }
    void set_area_m2(double p_value);
    double get_area_m2() const { return area_m2; }
    void set_age(double p_value);
    double get_age() const { return age; }
    void set_relief_m(double p_value);
    double get_relief_m() const { return relief_m; }
    void set_elongation(double p_value);
    double get_elongation() const { return elongation; }
    void set_axis_deg(double p_value);
    double get_axis_deg() const { return axis_deg; }
    void set_dock_bearing_deg(double p_value);
    double get_dock_bearing_deg() const { return dock_bearing_deg; }
    void set_cliff_m(double p_value);
    double get_cliff_m() const { return cliff_m; }
    void set_keel_m(double p_value);
    double get_keel_m() const { return keel_m; }
    void set_has_river(bool p_value);
    bool get_has_river() const { return has_river; }
    void set_has_yard(bool p_value);
    bool get_has_yard() const { return has_yard; }
    // 要了船坞、坪也真的凿出来了（岸线太退的岛凿不出）
    bool has_shipyard() const { return field.yard.present; }
};
