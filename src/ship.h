#pragma once

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/character_body3d.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/node3d.hpp>

#include "flight_model.h"

// 小空艇。原点在甲板面中心，船头朝 −z；甲板可站人。
// 受控时：输入 → flight_model → move_and_slide（与岛碰撞）。不受控：输入为零，自己停稳悬着。
// 靠泊：向给定泊位位姿自动滑过去，期间不理碰撞（泊位由 Island 保证是空的）。
//
// 场景（ship.tscn）里须有：Visual（Node3D，转向时侧倾的只是它）、Helm / Seat / Exit（Marker3D）、
// CameraRig/Camera3D（追尾相机，鼠标绕船转）。
class Ship : public godot::CharacterBody3D {
    GDCLASS(Ship, godot::CharacterBody3D)

    FlightParams params;
    FlightState state;
    double half_beam = 2.5;
    double min_altitude = 15.0; // 云海之上的最低飞行高度（全局 y）
    double mouse_sensitivity = 0.0025;

    bool controlled = false;
    bool mooring = false;
    godot::Vector3 berth_position;
    double berth_yaw = 0.0;
    double camera_yaw = 0.0;
    double camera_pitch = -0.3;

    godot::Node3D *visual = nullptr;
    godot::Node3D *camera_rig = nullptr;
    godot::Camera3D *camera = nullptr;
    godot::Node3D *helm = nullptr;
    godot::Node3D *seat = nullptr;
    godot::Node3D *exit_point = nullptr;

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

    // 立刻摆到位姿上并清零速度（开局、读档）
    void place_at(const godot::Transform3D &p_transform);
    // 开始向泊位滑行；到位后发 moored 信号
    void moor_at(const godot::Transform3D &p_berth);
    bool is_mooring() const { return mooring; }

    double get_speed() const;
    godot::Vector3 get_helm_position() const;
    godot::Transform3D get_seat_transform() const;
    godot::Transform3D get_exit_transform() const;
    godot::Camera3D *get_camera() const { return camera; }

    void set_half_beam(double p_value) { half_beam = p_value; }
    double get_half_beam() const { return half_beam; }
    void set_min_altitude(double p_value) { min_altitude = p_value; }
    double get_min_altitude() const { return min_altitude; }
    void set_max_speed(double p_value) { params.max_speed = p_value; }
    double get_max_speed() const { return params.max_speed; }
    void set_max_climb(double p_value) { params.max_climb = p_value; }
    double get_max_climb() const { return params.max_climb; }
    void set_max_yaw_rate(double p_value) { params.max_yaw_rate = p_value; }
    double get_max_yaw_rate() const { return params.max_yaw_rate; }
};
