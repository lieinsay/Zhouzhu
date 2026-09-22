#pragma once

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/character_body3d.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include "flight_model.h"
#include "ship_layout.h"

// 空艇。原点在甲板面上、舷门正中，船头朝 −z；甲板可站人。
// 船是一节节拼的：layout（自船头到船尾的模块 id）决定几何与碰撞——每节一段甲板、船体、舷栏，
// 驾驶区有舵柱、舵轮与舵楼顶，储存区两侧摞货箱、中间留走道，动力区船底吊浮石、甲板上一对侧桅帆；
// 船头一只楔、首尾各一道栏。舷门固定在 z = 0 的两舷（前 count/2 节在舷门前），所以泊位、坞位、跳板都指舷门，
// 船怎么改都不用挪码头。舵位 / 座位 / 出口由布局算出；FlightParams 由船况（ship_layout）算出。
// 受控时：输入 → flight_model → move_and_slide（与岛碰撞）。不受控：输入为零，自己停稳悬着。
// 靠泊 / 入坞：向给定位姿自动滑过去，期间不理碰撞（位姿由 Island / Shipyard 保证是空的）。
//
// 场景（ship.tscn）里须有：Visual（Node3D，转向时侧倾的只是它；拼出来的样子都挂在下面）、
// CameraRig/Camera3D（追尾相机，鼠标绕船转）。碰撞体拼在根节点下。
class Ship : public godot::CharacterBody3D {
    GDCLASS(Ship, godot::CharacterBody3D)

    FlightParams params;
    FlightState state;
    ShipLayout layout = default_layout();
    ShipStats stats;
    double half_beam = 2.5;
    double min_altitude = 15.0; // 云海之上的最低飞行高度（全局 y）
    double mouse_sensitivity = 0.0025;

    bool controlled = false;
    bool mooring = false;
    godot::Vector3 berth_position;
    double berth_yaw = 0.0;
    double camera_yaw = 0.0;
    double camera_pitch = -0.3;

    godot::Vector3 helm_local;
    godot::Vector3 seat_local;
    godot::Vector3 exit_local;

    godot::Node3D *visual = nullptr;
    godot::Node3D *camera_rig = nullptr;
    godot::Camera3D *camera = nullptr;

    godot::Ref<godot::StandardMaterial3D> mat_wood, mat_hull, mat_rail, mat_sail, mat_stone, mat_crate, mat_roof;

    void ensure_materials();
    void clear_generated();
    void rebuild();
    void build_section(int p_index);
    void step_flight(double p_delta);
    void step_mooring(double p_delta);

protected:
    static void _bind_methods();

public:
    void _ready() override;
    void _physics_process(double p_delta) override;
    void _unhandled_input(const godot::Ref<godot::InputEvent> &p_event) override;

    void set_controlled(bool p_controlled);
    bool is_controlled() const { return controlled; }

    // 立刻把舷门摆到位姿上并清零速度（开局、读档、召回）
    void place_at(const godot::Transform3D &p_transform);
    // 开始向泊位 / 坞位滑行（位姿指舷门该在哪）；到位后发 moored 信号
    void moor_at(const godot::Transform3D &p_berth);
    bool is_mooring() const { return mooring; }

    double get_speed() const;
    godot::Vector3 get_helm_position() const;
    godot::Transform3D get_seat_transform() const;
    godot::Transform3D get_exit_transform() const;
    godot::Camera3D *get_camera() const { return camera; }

    // 布局：脚本 / 场景文件用 id 数组；C++ 上层直接用模型。换了布局立刻重拼，发 refitted 信号
    void set_layout(const godot::PackedStringArray &p_ids);
    godot::PackedStringArray get_layout() const;
    const ShipLayout &get_layout_model() const { return layout; }
    // 合法就换并重拼（返回 true）；不合法不动
    bool apply_layout(const ShipLayout &p_layout);
    const ShipStats &get_stats_model() const { return stats; }
    godot::Dictionary get_stats() const; // sections / mass / thrust / hold / max_speed / max_climb / max_yaw_rate / can_fly
    bool can_fly() const { return stats.can_fly(); }
    int get_section_count() const { return int(layout.size()); }
    godot::String get_section_id(int p_index) const;
    double get_bow_z() const { return layout_bow_z(layout); }
    double get_stern_z() const { return layout_stern_z(layout); }

    void set_half_beam(double p_value) { half_beam = p_value; }
    double get_half_beam() const { return half_beam; }
    void set_min_altitude(double p_value) { min_altitude = p_value; }
    double get_min_altitude() const { return min_altitude; }
    double get_max_speed() const { return params.max_speed; }
};
