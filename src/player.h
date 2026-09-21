#pragma once

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/character_body3d.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/node3d.hpp>

// 玩家角色：第三人称，相对相机朝向的 WASD、跳、疾跑。
// 根节点自己从不旋转——转的是相机支点（鼠标）和身体模型（朝移动方向）。
// 它不知道飞船的存在：入座时由上层 set_controlled(false) 停掉输入与物理，再直接摆位姿。
//
// 场景（player.tscn）里须有：Body（Node3D，身体模型）、CameraPivot/SpringArm3D/Camera3D。
class Player : public godot::CharacterBody3D {
    GDCLASS(Player, godot::CharacterBody3D)

    double walk_speed = 5.0;
    double sprint_speed = 8.5;
    double jump_speed = 7.5;
    double gravity = 22.0;
    double mouse_sensitivity = 0.0025;

    bool controlled = true;
    double look_yaw = 0.0;
    double look_pitch = -0.25;
    uint32_t saved_layer = 0;
    uint32_t saved_mask = 0;

    godot::Node3D *body = nullptr;
    godot::Node3D *camera_pivot = nullptr;
    godot::Camera3D *camera = nullptr;

    void apply_look();

protected:
    static void _bind_methods();

public:
    void _ready() override;
    void _physics_process(double p_delta) override;
    void _unhandled_input(const godot::Ref<godot::InputEvent> &p_event) override;

    void set_controlled(bool p_controlled);
    bool is_controlled() const { return controlled; }
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
    double get_mouse_sensitivity() const { return mouse_sensitivity; }
};
