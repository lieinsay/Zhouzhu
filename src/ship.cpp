#include "ship.h"

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

constexpr double MOOR_MAX_SPEED = 9.0;
constexpr double MOOR_GAIN = 1.2; // 靠泊速度 = 距离 × 此值 + 下限，越近越慢
constexpr double MOOR_MIN_SPEED = 0.6;
constexpr double MOOR_YAW_RATE = 0.7;
constexpr double BANK_PER_YAW_RATE = 0.3; // 侧倾角 / 偏航角速度
constexpr double TRIM_PER_CLIMB = 0.012; // 纵倾角 / 垂直速度
constexpr double CAMERA_PITCH_MIN = -1.2;
constexpr double CAMERA_PITCH_MAX = 0.5;

// 从 p_from 转到 p_to 的最短有向角（godot-cpp 的 Math 里没有 angle_difference）
double angle_difference(double p_from, double p_to) {
    const double diff = std::fmod(p_to - p_from, Math_TAU);
    return std::fmod(2.0 * diff, Math_TAU) - diff;
}

} // namespace

void Ship::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_controlled", "controlled"), &Ship::set_controlled);
    ClassDB::bind_method(D_METHOD("is_controlled"), &Ship::is_controlled);
    ClassDB::bind_method(D_METHOD("place_at", "transform"), &Ship::place_at);
    ClassDB::bind_method(D_METHOD("moor_at", "berth"), &Ship::moor_at);
    ClassDB::bind_method(D_METHOD("is_mooring"), &Ship::is_mooring);
    ClassDB::bind_method(D_METHOD("get_speed"), &Ship::get_speed);
    ClassDB::bind_method(D_METHOD("get_helm_position"), &Ship::get_helm_position);
    ClassDB::bind_method(D_METHOD("get_seat_transform"), &Ship::get_seat_transform);
    ClassDB::bind_method(D_METHOD("get_exit_transform"), &Ship::get_exit_transform);
    ClassDB::bind_method(D_METHOD("get_camera"), &Ship::get_camera);

    ClassDB::bind_method(D_METHOD("set_half_beam", "value"), &Ship::set_half_beam);
    ClassDB::bind_method(D_METHOD("get_half_beam"), &Ship::get_half_beam);
    ClassDB::bind_method(D_METHOD("set_min_altitude", "value"), &Ship::set_min_altitude);
    ClassDB::bind_method(D_METHOD("get_min_altitude"), &Ship::get_min_altitude);
    ClassDB::bind_method(D_METHOD("set_max_speed", "value"), &Ship::set_max_speed);
    ClassDB::bind_method(D_METHOD("get_max_speed"), &Ship::get_max_speed);
    ClassDB::bind_method(D_METHOD("set_max_climb", "value"), &Ship::set_max_climb);
    ClassDB::bind_method(D_METHOD("get_max_climb"), &Ship::get_max_climb);
    ClassDB::bind_method(D_METHOD("set_max_yaw_rate", "value"), &Ship::set_max_yaw_rate);
    ClassDB::bind_method(D_METHOD("get_max_yaw_rate"), &Ship::get_max_yaw_rate);

    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "half_beam", PROPERTY_HINT_RANGE, "0.5,20,0.05,suffix:m"), "set_half_beam", "get_half_beam");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_altitude", PROPERTY_HINT_RANGE, "-100,1000,1,suffix:m"), "set_min_altitude", "get_min_altitude");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_speed", PROPERTY_HINT_RANGE, "1,100,0.5,suffix:m/s"), "set_max_speed", "get_max_speed");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_climb", PROPERTY_HINT_RANGE, "1,50,0.5,suffix:m/s"), "set_max_climb", "get_max_climb");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_yaw_rate", PROPERTY_HINT_RANGE, "0.1,3,0.01,suffix:rad/s"), "set_max_yaw_rate", "get_max_yaw_rate");

    ADD_SIGNAL(MethodInfo("moored"));
}

void Ship::_ready() {
    if (Engine::get_singleton()->is_editor_hint()) {
        set_physics_process(false);
        set_process_unhandled_input(false);
        return;
    }
    visual = get_node<Node3D>("Visual");
    helm = get_node<Node3D>("Helm");
    seat = get_node<Node3D>("Seat");
    exit_point = get_node<Node3D>("Exit");
    camera_rig = get_node<Node3D>("CameraRig");
    camera = get_node<Camera3D>("CameraRig/Camera3D");
    ERR_FAIL_COND_MSG(!visual || !helm || !seat || !exit_point || !camera_rig || !camera, "Ship: 场景里缺 Visual / Helm / Seat / Exit / CameraRig/Camera3D");
    state.yaw = get_rotation().y;
}

void Ship::_physics_process(double p_delta) {
    if (mooring) {
        step_mooring(p_delta);
    } else {
        step_flight(p_delta);
    }
    if (visual) {
        const real_t blend = real_t(1.0 - std::exp(-4.0 * p_delta));
        Vector3 tilt = visual->get_rotation();
        tilt.z += (real_t(state.yaw_rate * BANK_PER_YAW_RATE) - tilt.z) * blend; // 左转（角速度为正）向左倾
        tilt.x += (real_t(state.vy * TRIM_PER_CLIMB) - tilt.x) * blend;
        visual->set_rotation(tilt);
    }
}

void Ship::step_flight(double p_delta) {
    FlightInput in;
    if (controlled) {
        Input *input = Input::get_singleton();
        in.throttle = input->get_axis(input_actions::MOVE_BACK, input_actions::MOVE_FORWARD);
        in.rudder = input->get_axis(input_actions::MOVE_LEFT, input_actions::MOVE_RIGHT);
        in.lift = input->get_axis(input_actions::DESCEND, input_actions::ASCEND);
    }
    state = flight_step(state, in, params, p_delta);

    // 别沉进云海
    const double altitude = get_global_position().y;
    if (altitude < min_altitude) {
        state.vy = std::max(state.vy, 2.0 * (min_altitude - altitude));
    }

    // 没人掌舵又已停稳：彻底不动，甲板上的人脚下才是死的
    if (!controlled && get_speed() < 0.01 && std::abs(state.yaw_rate) < 0.001) {
        state.vx = state.vy = state.vz = state.yaw_rate = 0.0;
        return;
    }

    set_rotation(Vector3(0, real_t(state.yaw), 0));
    set_velocity(Vector3(real_t(state.vx), real_t(state.vy), real_t(state.vz)));
    move_and_slide();
    // 撞上东西后的实际速度写回模型，否则它会顶着岛一直「加速」
    const Vector3 actual = get_velocity();
    state.vx = actual.x;
    state.vy = actual.y;
    state.vz = actual.z;
}

void Ship::step_mooring(double p_delta) {
    Vector3 position = get_global_position();
    const Vector3 to_berth = berth_position - position;
    const double distance = to_berth.length();
    const double step = std::min(MOOR_MAX_SPEED, MOOR_GAIN * distance + MOOR_MIN_SPEED) * p_delta;
    const bool there = distance <= step;
    position = there ? berth_position : position + to_berth * real_t(step / distance);

    const double yaw_error = angle_difference(state.yaw, berth_yaw);
    const double yaw_step = MOOR_YAW_RATE * p_delta;
    const bool aligned = std::abs(yaw_error) <= yaw_step;
    state.yaw = aligned ? berth_yaw : state.yaw + (yaw_error > 0 ? yaw_step : -yaw_step);
    state.yaw_rate = aligned ? 0.0 : (yaw_error > 0 ? MOOR_YAW_RATE : -MOOR_YAW_RATE);

    const Vector3 moved = position - get_global_position();
    state.vx = moved.x / p_delta;
    state.vy = moved.y / p_delta;
    state.vz = moved.z / p_delta;
    set_global_position(position);
    set_rotation(Vector3(0, real_t(state.yaw), 0));

    if (there && aligned) {
        mooring = false;
        state.vx = state.vy = state.vz = state.yaw_rate = 0.0;
        emit_signal("moored");
    }
}

void Ship::_unhandled_input(const Ref<InputEvent> &p_event) {
    if (!controlled || !camera_rig || Input::get_singleton()->get_mouse_mode() != Input::MOUSE_MODE_CAPTURED) {
        return;
    }
    const Ref<InputEventMouseMotion> motion = p_event;
    if (motion.is_valid()) {
        const Vector2 rel = motion->get_relative();
        camera_yaw -= rel.x * mouse_sensitivity;
        camera_pitch = std::clamp(camera_pitch - rel.y * mouse_sensitivity, CAMERA_PITCH_MIN, CAMERA_PITCH_MAX);
        camera_rig->set_rotation(Vector3(real_t(camera_pitch), real_t(camera_yaw), 0));
    }
}

void Ship::set_controlled(bool p_controlled) {
    controlled = p_controlled;
    if (controlled && camera_rig) {
        // 上舵时相机回到正后方
        camera_yaw = 0.0;
        camera_pitch = -0.3;
        camera_rig->set_rotation(Vector3(real_t(camera_pitch), 0, 0));
    }
}

void Ship::place_at(const Transform3D &p_transform) {
    mooring = false;
    state = FlightState();
    state.yaw = p_transform.basis.get_euler().y;
    set_global_position(p_transform.origin);
    set_rotation(Vector3(0, real_t(state.yaw), 0));
    set_velocity(Vector3());
}

void Ship::moor_at(const Transform3D &p_berth) {
    berth_position = p_berth.origin;
    // 两舷都有舷门，船头朝哪边都能靠：取转得少的那个
    const double yaw = p_berth.basis.get_euler().y;
    berth_yaw = std::abs(angle_difference(state.yaw, yaw)) <= Math_PI / 2 ? yaw : yaw + Math_PI;
    mooring = true;
}

double Ship::get_speed() const {
    return std::sqrt(state.vx * state.vx + state.vy * state.vy + state.vz * state.vz);
}

Vector3 Ship::get_helm_position() const {
    return helm ? helm->get_global_position() : get_global_position();
}

Transform3D Ship::get_seat_transform() const {
    return seat ? seat->get_global_transform() : get_global_transform();
}

Transform3D Ship::get_exit_transform() const {
    return exit_point ? exit_point->get_global_transform() : get_global_transform();
}
