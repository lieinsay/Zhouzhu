#pragma once

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/character_body3d.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include "flight_model.h"
#include "ship_layout.h"

// 空艇。原点在舷门格 (0,0,0) 的第 0 层甲板面中心，船头朝 −z，x 向右舷；甲板可站人。
// 船是三维格子里拼的（layout："x,y,z:kind" 序列，见 ship_layout.h）：
//   · 每列最上面那一节是开放甲板：甲板板（第 0 层）或下面舱体的顶、按邻列高度取舍的舷栏、模块的陈设
//     （驾驶区舵柱舵轮舵楼顶，储存区两舷各两个货位、中间走道，动力区一对侧桅帆）；
//   · 被上面盖住的节是封闭舱体（储存区 = 货舱，墙上有舱门；动力区 = 机舱，墙上透着浮石的光）；
//   · 第 0 层每节下面吊船体，动力区再吊浮石；最前一排前面一只船头楔；
//   · 货位（hold_slots）：空着是一块垫木，放了货是一只带碰撞的货箱，箱顶按货物着色；哪个货位上是什么由上层（跑货的货箱账）set_cargo 告诉它，它只画；
//   · 相邻两列高度不同处生成梯子：踏板 + 带 "ladder" 元数据的 Area3D（+Z 离墙）+ 舷栏缺口，Player 靠它上下；
//   · 野泊时从一舷的舷门放下的舷梯（set_gangway，带碰撞，跟着船走）：上层照 wild_landing 算好的长短落差放、上舵时收起。
// 舷门在舷门那一排（z = 0、第 0 层）两头的船边，z ∈ ±0.9 不设栏。
// 泊位 / 坞位是「接触线」：X 指向码头的位姿，船按面向码头那一侧的船边算原点该在哪，所以船怎么改都不用挪码头。
// 受控时：输入 → flight_model → move_and_slide（与岛碰撞）。不受控：输入为零，自己停稳悬着。
// 靠泊 / 入坞：向接触线滑过去，期间不理碰撞（位姿由 Island / Shipyard 保证是空的）。
//
// 场景（ship.tscn）里须有：Visual（Node3D，转向时侧倾的只是它；拼出来的样子都挂在下面）、
// CameraRig/Camera3D（追尾相机，鼠标绕船转）。碰撞体与梯子区域拼在根节点下。
class Ship : public godot::CharacterBody3D {
    GDCLASS(Ship, godot::CharacterBody3D)

    FlightParams params;
    FlightState state;
    ShipLayout layout = default_layout();
    ShipStats stats;
    double min_altitude = 15.0; // 云海之上的最低飞行高度（全局 y）
    double mouse_sensitivity = 0.0025;

    bool controlled = false;
    int gear = 0; // 速度档位（flight_gears）；靠泊、放到泊位上时回到头一档
    bool mooring = false;
    godot::Vector3 berth_position;
    double berth_yaw = 0.0;
    double camera_yaw = 0.0;
    double camera_pitch = -0.3;

    godot::Vector3 helm_local;
    godot::Vector3 seat_local;
    godot::Vector3 exit_local;
    godot::PackedVector3Array ladder_feet_local;
    godot::PackedVector3Array ladder_tops_local;
    std::vector<HoldSlot> slots;
    std::vector<godot::String> cargo; // 与 slots 一一对应：物品 id，空 = 空位
    int gangway_side = 0; // 放着舷梯的一舷（+1 右舷、−1 左舷）；0 = 收着
    double gangway_run = 0.0, gangway_rise = 0.0;

    godot::Node3D *visual = nullptr;
    godot::Node3D *camera_rig = nullptr;
    godot::Camera3D *camera = nullptr;

    godot::Ref<godot::Material> mat_wood, mat_hull, mat_rail, mat_sail, mat_stone, mat_crate, mat_roof, mat_glow; // 弯曲标准材质（curved_material）

    void ensure_materials();
    void clear_generated();
    void rebuild();
    void build_module(const Module &p_module);
    void build_ladder(const Ladder &p_ladder);
    void build_cargo();
    void build_gangway();
    void step_flight(double p_delta);
    void step_mooring(double p_delta);
    // 把面向接触线那一侧的船边贴到接触线上，原点该在哪；r_flipped：船头是否与接触线的 Z 反向
    godot::Vector3 origin_for(const godot::Transform3D &p_contact, double p_yaw) const;

protected:
    static void _bind_methods();

public:
    void _ready() override;
    void _physics_process(double p_delta) override;
    void _unhandled_input(const godot::Ref<godot::InputEvent> &p_event) override;

    void set_controlled(bool p_controlled);
    // 换锚（行星计划 P3）：船原点搬到 p_origin、船头转 p_dyaw（弧度，绕 y）、速度乘 p_rotation；飞行模型的状态跟着
    void shift(const godot::Vector3 &p_origin, double p_dyaw, const godot::Basis &p_rotation);
    bool is_controlled() const { return controlled; }

    // 立刻贴到接触线上并清零速度（开局、读档、召回、改完船重新坐定）。
    // p_keep_heading：保留现在的船头朝向（接触线的两个方向里取转得少的），否则船头顺着接触线的 Z
    void place_at(const godot::Transform3D &p_contact, bool p_keep_heading = false);
    // 开始向接触线滑行（两舷都有舷门，船头朝哪边都能靠：取转得少的）；到位后发 moored 信号
    void moor_at(const godot::Transform3D &p_contact);
    bool is_mooring() const { return mooring; }
    // 船舷是否贴在这条接触线上（两个朝向都算）
    bool is_at(const godot::Transform3D &p_contact, double p_tolerance) const;

    double get_speed() const;
    godot::Vector3 get_helm_position() const;
    godot::Transform3D get_seat_transform() const;
    godot::Transform3D get_exit_transform() const;
    godot::Camera3D *get_camera() const { return camera; }
    // 某一列顶层甲板的中心（全局）；没有这一列返回原点
    godot::Vector3 get_deck_position(int p_x, int p_z) const;
    godot::PackedVector3Array get_ladder_feet() const; // 梯脚（全局，离墙半米）
    godot::PackedVector3Array get_ladder_tops() const; // 梯顶落脚处（全局）

    // 布局：脚本 / 场景文件用 "x,y,z:kind" 数组；C++ 上层直接用模型。换了布局立刻重拼，发 refitted 信号
    void set_layout(const godot::PackedStringArray &p_ids);
    godot::PackedStringArray get_layout() const;
    const ShipLayout &get_layout_model() const { return layout; }
    // 合法就换并重拼（返回 true）；不合法不动
    bool apply_layout(const ShipLayout &p_layout);
    const ShipStats &get_stats_model() const { return stats; }
    godot::Dictionary get_stats() const; // sections / mass / thrust / hold / max_speed / max_climb / max_yaw_rate / can_fly
    bool can_fly() const { return stats.can_fly(); }
    int get_section_count() const { return layout.size(); }
    double get_bow_z() const { return bow_z(layout); }
    double get_stern_z() const { return stern_z(layout); }
    double get_port_edge_x() const { return port_edge_x(layout); }
    double get_starboard_edge_x() const { return starboard_edge_x(layout); }
    int get_levels() const { return layout.levels(); }

    // 船舱：每节储存区 12 格（开背包时列在背包下面；里面装什么是上层的账）
    int get_bay_slots() const { return stats.bay; }
    // 全局的一点在不在船上（落在某一列的格子里、船底之上、顶层甲板之上 2.5 m 以内）
    bool is_aboard(const godot::Vector3 &p_global) const;

    // 货位（编号见 hold_slots）：全局位置是那一格的甲板面；open = 人够得着
    int get_hold_slot_count() const { return int(slots.size()); }
    godot::Vector3 get_hold_slot_position(int p_slot) const;
    bool is_hold_slot_open(int p_slot) const;
    // 离 p_global 最近、水平 p_range 以内、高差 1.5 m 以内的够得着的货位；没有 −1
    int find_hold_slot(const godot::Vector3 &p_global, double p_range) const;
    // 货位上放着什么（物品 id；空串 = 空位）。改船重拼时照旧画着，货位变少时多出来的丢掉（上层保证改船前船是空的）
    void set_cargo(int p_slot, const godot::String &p_item);
    godot::String get_cargo(int p_slot) const;
    int get_cargo_count() const; // 放着几箱

    // 野泊的舷梯：从 p_side 舷（+1 右舷、−1 左舷）的舷门甲板边斜下去，水平 p_run、落差 p_rise（wild_landing 算的），梯脚稍稍埋进地里
    void set_gangway(int p_side, double p_run, double p_rise);
    void clear_gangway();
    bool has_gangway() const { return gangway_side != 0; }
    int get_gangway_side() const { return gangway_side; }
    godot::Vector3 get_gangway_foot() const; // 梯脚（全局）；收着时是船原点
    godot::Vector3 get_gangway_top() const; // 梯顶（舷门甲板边，全局）
    // 船落在 p_pose（船原点的位姿）时，船身（第 0 层每节的整格、往上到最高层）在 p_floor_y + 0.5 m 以上压着的实物
    // （房子、栈桥、船台、采掘面的石头……；碰撞层 1）。p_exclude 里的不算（船自己、人）。没有返回 nullptr
    // p_gangway_side ≠ 0 时另查舷梯上方那一条（梯板上 0.3–2.3 m，梯脚那 1.2 m 不查）
    godot::Object *landing_obstacle(const godot::Transform3D &p_pose, double p_floor_y, const godot::TypedArray<godot::RID> &p_exclude, int p_gangway_side = 0,
            double p_gangway_run = 0.0, double p_gangway_rise = 0.0) const;

    void set_min_altitude(double p_value) { min_altitude = p_value; }
    double get_min_altitude() const { return min_altitude; }
    double get_max_speed() const { return apply_gear(params, gear).max_speed; } // 当前档位下
    void set_gear(int p_gear);
    int get_gear() const { return gear; }
    godot::String get_gear_name() const;
};
