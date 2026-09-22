#pragma once

#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include "island_field.h"

// 一座浮空岛。节点原点在岸缘高度（局部 y = 0 即岸缘），岛体向下垂到岛底。
// 进树时按属性生成高度场 → 网格 + 碰撞 + 栈桥（+ 河水）；生成物没有 owner，不会写进场景文件。
// 每次生成完发 rebuilt 信号：长在岛上的东西（Farm）靠它知道地形换了。
// 编辑器里同样生成，改属性即重建，方便摆岛。
class Island : public godot::StaticBody3D {
    GDCLASS(Island, godot::StaticBody3D)

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
    godot::Vector3 dock_local(double p_along_out, double p_up) const;

protected:
    static void _bind_methods();

public:
    static constexpr double PIER_WIDTH = 3.0;
    static constexpr double PIER_LIFT = 0.1; // 栈桥面比停机坪高这么多，免得与地面共面闪烁

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

    // 给同在 C++ 里的上层读（Farm 据此勘察农田）
    const IslandField &get_field() const { return field; }
    const IslandParams &get_params() const { return params; }

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
