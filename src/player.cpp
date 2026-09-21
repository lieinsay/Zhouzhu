#include "player.h"

#include <algorithm>
#include <cmath>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>

#include "input_actions.h"

using namespace godot;

namespace {

constexpr double GROUND_ACCEL = 14.0; // 水平速度逼近目标的速率（1/s）
constexpr double AIR_ACCEL = 3.0;
constexpr double TURN_RATE = 12.0; // 身体转向移动方向的速率
constexpr double PITCH_MIN = -1.3;
constexpr double PITCH_MAX = 0.6;

} // namespace

void Player::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_controlled", "controlled"), &Player::set_controlled);
    ClassDB::bind_method(D_METHOD("is_controlled"), &Player::is_controlled);
    ClassDB::bind_method(D_METHOD("set_look_yaw", "yaw"), &Player::set_look_yaw);
    ClassDB::bind_method(D_METHOD("get_look_yaw"), &Player::get_look_yaw);
    ClassDB::bind_method(D_METHOD("set_facing_yaw", "yaw"), &Player::set_facing_yaw);
    ClassDB::bind_method(D_METHOD("get_camera"), &Player::get_camera);

    ClassDB::bind_method(D_METHOD("set_walk_speed", "value"), &Player::set_walk_speed);
    ClassDB::bind_method(D_METHOD("get_walk_speed"), &Player::get_walk_speed);
    ClassDB::bind_method(D_METHOD("set_sprint_speed", "value"), &Player::set_sprint_speed);
    ClassDB::bind_method(D_METHOD("get_sprint_speed"), &Player::get_sprint_speed);
    ClassDB::bind_method(D_METHOD("set_jump_speed", "value"), &Player::set_jump_speed);
    ClassDB::bind_method(D_METHOD("get_jump_speed"), &Player::get_jump_speed);
    ClassDB::bind_method(D_METHOD("set_gravity", "value"), &Player::set_gravity);
    ClassDB::bind_method(D_METHOD("get_gravity"), &Player::get_gravity);
    ClassDB::bind_method(D_METHOD("set_mouse_sensitivity", "value"), &Player::set_mouse_sensitivity);
    ClassDB::bind_method(D_METHOD("get_mouse_sensitivity"), &Player::get_mouse_sensitivity);

    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "walk_speed", PROPERTY_HINT_RANGE, "0,20,0.1,suffix:m/s"), "set_walk_speed", "get_walk_speed");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sprint_speed", PROPERTY_HINT_RANGE, "0,30,0.1,suffix:m/s"), "set_sprint_speed", "get_sprint_speed");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "jump_speed", PROPERTY_HINT_RANGE, "0,20,0.1,suffix:m/s"), "set_jump_speed", "get_jump_speed");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gravity", PROPERTY_HINT_RANGE, "0,60,0.1,suffix:m/s²"), "set_gravity", "get_gravity");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mouse_sensitivity", PROPERTY_HINT_RANGE, "0.0005,0.01,0.0001"), "set_mouse_sensitivity", "get_mouse_sensitivity");
}

void Player::_ready() {
    // 扩展类的虚函数在编辑器里也会跑；角色只在游戏里动
    if (Engine::get_singleton()->is_editor_hint()) {
        set_physics_process(false);
        set_process_unhandled_input(false);
        return;
    }
    body = get_node<Node3D>("Body");
    camera_pivot = get_node<Node3D>("CameraPivot");
    camera = get_node<Camera3D>("CameraPivot/SpringArm3D/Camera3D");
    ERR_FAIL_COND_MSG(!body || !camera_pivot || !camera, "Player: 场景里缺 Body 或 CameraPivot/SpringArm3D/Camera3D");
    apply_look();
}

void Player::apply_look() {
    if (camera_pivot) {
        camera_pivot->set_rotation(Vector3(real_t(look_pitch), real_t(look_yaw), 0));
    }
}

void Player::_unhandled_input(const Ref<InputEvent> &p_event) {
    if (!controlled || Input::get_singleton()->get_mouse_mode() != Input::MOUSE_MODE_CAPTURED) {
        return;
    }
    const Ref<InputEventMouseMotion> motion = p_event;
    if (motion.is_valid()) {
        const Vector2 rel = motion->get_relative();
        look_yaw -= rel.x * mouse_sensitivity;
        look_pitch = std::clamp(look_pitch - rel.y * mouse_sensitivity, PITCH_MIN, PITCH_MAX);
        apply_look();
    }
}

void Player::_physics_process(double p_delta) {
    if (!controlled) {
        return;
    }
    Input *input = Input::get_singleton();
    const Vector2 stick = input->get_vector(input_actions::MOVE_LEFT, input_actions::MOVE_RIGHT, input_actions::MOVE_FORWARD, input_actions::MOVE_BACK);
    const Vector3 wish = Basis(Vector3(0, 1, 0), real_t(look_yaw)).xform(Vector3(stick.x, 0, stick.y));
    const double speed = input->is_action_pressed(input_actions::SPRINT) ? sprint_speed : walk_speed;

    Vector3 velocity = get_velocity();
    const bool grounded = is_on_floor();
    const real_t blend = real_t(1.0 - std::exp(-(grounded ? GROUND_ACCEL : AIR_ACCEL) * p_delta));
    velocity.x += (wish.x * real_t(speed) - velocity.x) * blend;
    velocity.z += (wish.z * real_t(speed) - velocity.z) * blend;
    if (grounded) {
        velocity.y = input->is_action_just_pressed(input_actions::JUMP) ? real_t(jump_speed) : 0;
    } else {
        velocity.y -= real_t(gravity * p_delta);
    }
    set_velocity(velocity);
    move_and_slide();

    if (body && wish.length_squared() > 0.01f) {
        const double target = std::atan2(-wish.x, -wish.z); // 模型正面朝 −z
        const double current = body->get_rotation().y;
        body->set_rotation(Vector3(0, real_t(Math::lerp_angle(current, target, 1.0 - std::exp(-TURN_RATE * p_delta))), 0));
    }
}

void Player::set_controlled(bool p_controlled) {
    if (controlled == p_controlled) {
        return;
    }
    controlled = p_controlled;
    if (controlled) {
        set_collision_layer(saved_layer);
        set_collision_mask(saved_mask);
    } else {
        // 入座：不再参与碰撞，位姿交给上层摆
        saved_layer = get_collision_layer();
        saved_mask = get_collision_mask();
        set_collision_layer(0);
        set_collision_mask(0);
    }
    set_velocity(Vector3());
}

void Player::set_look_yaw(double p_yaw) {
    look_yaw = p_yaw;
    apply_look();
}

void Player::set_facing_yaw(double p_yaw) {
    if (body) {
        body->set_rotation(Vector3(0, real_t(p_yaw), 0));
    }
}
