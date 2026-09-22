#pragma once

#include <vector>

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/character_body3d.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/node3d.hpp>

// 玩家角色：第三人称，相对相机朝向的 WASD、跳、疾跑。
// 根节点自己从不旋转——转的是相机支点（鼠标）和身体模型（朝移动方向）。
// 它不知道飞船的存在：入座时由上层 set_controlled(false) 停掉输入与物理，再直接摆位姿。
// set_input_blocked(true) 只挡输入（开着背包时 W/S 是在选东西，不是在走路），物理照常：重力、落地都还在。
// 爬梯：Sensor（Area3D）碰到带 "ladder" 元数据的区域时，前 / 后变成沿梯子上 / 下（面朝墙按前是上，背对墙按前是下），
// 水平速度放慢，不受重力。梯子是谁的（船）它不知道。
// god 模式（开发者工具开的，它不知道是谁）：不受重力，按相机的水平朝向飞，空格升、Ctrl 降、Shift 加速；碰撞照旧，降到地上就站住。
//
// 场景（player.tscn）里须有：Body（Node3D，身体模型）、CameraPivot/SpringArm3D/Camera3D、Sensor（Area3D）。
class Player : public godot::CharacterBody3D {
    GDCLASS(Player, godot::CharacterBody3D)

    double walk_speed = 5.0;
    double sprint_speed = 8.5;
    double jump_speed = 7.5;
    double gravity = 22.0;
    double climb_speed = 2.5;
    double mouse_sensitivity = 0.0025;
    double god_speed = 30.0;
    double god_boost = 5.0; // 按住 Shift 时速度乘这么多

    bool controlled = true;
    bool input_blocked = false;
    bool god_mode = false;
    double look_yaw = 0.0;
    double look_pitch = -0.25;
    uint32_t saved_layer = 0;
    uint32_t saved_mask = 0;

    godot::Node3D *body = nullptr;
    godot::Node3D *camera_pivot = nullptr;
    godot::Camera3D *camera = nullptr;
    std::vector<godot::Node3D *> ladders; // 眼下碰着的梯子区域（区域的 +Z 是离墙的方向）

    void apply_look();
    void turn_body(const godot::Vector3 &p_wish, double p_delta); // 身体模型转向移动方向
    void on_sensor_area_entered(godot::Node3D *p_area);
    void on_sensor_area_exited(godot::Node3D *p_area);

protected:
    static void _bind_methods();

public:
    void _ready() override;
    void _physics_process(double p_delta) override;
    void _unhandled_input(const godot::Ref<godot::InputEvent> &p_event) override;

    void set_controlled(bool p_controlled);
    bool is_controlled() const { return controlled; }
    void set_input_blocked(bool p_blocked) { input_blocked = p_blocked; }
    bool is_input_blocked() const { return input_blocked; }
    bool is_on_ladder() const { return !ladders.empty(); }
    void set_god_mode(bool p_enabled);
    bool is_god_mode() const { return god_mode; }
    void set_look_yaw(double p_yaw);
    double get_look_yaw() const { return look_yaw; }
    void set_facing_yaw(double p_yaw); // 身体模型朝向
    godot::Camera3D *get_camera() const { return camera; }

    void set_walk_speed(double p_value) { walk_speed = p_value; }
    double get_walk_speed() const { return walk_speed; }
    void set_sprint_speed(double p_value) { sprint_speed = p_value; }
    double get_sprint_speed() const { return sprint_speed; }
    void set_jump_speed(double p_value) { jump_speed = p_value; }
    double get_jump_speed() const { return jump_speed; }
    void set_gravity(double p_value) { gravity = p_value; }
    double get_gravity() const { return gravity; }
    void set_mouse_sensitivity(double p_value) { mouse_sensitivity = p_value; }
    void set_god_speed(double p_value) { god_speed = p_value; }
    double get_god_speed() const { return god_speed; }
    void set_god_boost(double p_value) { god_boost = p_value; }
    double get_god_boost() const { return god_boost; }
    double get_mouse_sensitivity() const { return mouse_sensitivity; }
};
