#pragma once

#include <vector>

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>

#include "building_parts.h"

// 室内编辑（开发者）的视角：只负责画，不存模板、不读输入。
//   · 一台环绕光标的相机（与布置模式同一种：绕焦点转、滚轮拉近拉远），默认从高处往下看剖开的屋子。
//   · 光标：一块贴地的方片；幽灵：要放的那一样（家具、梯子、门窗的位置、隔墙的预览），按状态上色——能放是绿、不能放是红、
//     光标下已经有东西是白（描出它的占地）。
// 所有形状都在建筑自己的局部坐标里给（BoxPart），这里挂在一个跟着建筑位姿走的节点下。
class InteriorOverlay : public godot::Node3D {
    GDCLASS(InteriorOverlay, godot::Node3D)

public:
    enum GhostState {
        GHOST_CAN_PLACE,
        GHOST_CANNOT_PLACE,
        GHOST_SELECTED,
    };

private:
    godot::Camera3D *camera = nullptr;
    godot::Node3D *frame = nullptr; // 跟着建筑的位姿
    godot::Node3D *ghost = nullptr;
    godot::Node3D *cursor = nullptr;
    std::vector<BoxPart> shown; // 眼下画着的幽灵：没变就不重建
    int shown_state = -1;
    godot::Ref<godot::StandardMaterial3D> mat_ok, mat_bad, mat_selected, mat_cursor;

    double orbit_yaw = 0.4;
    double orbit_elevation = 1.05;
    double orbit_distance = 16.0;
    godot::Vector3 focus;

    void ensure_built();
    void place_camera();

protected:
    static void _bind_methods();

public:
    void show_at(const godot::Transform3D &p_building, const godot::Vector3 &p_focus);
    // 每帧：建筑位姿、焦点（全局）、光标（局部，y = 这层地面）、幽灵的盒子（局部）与状态
    void refresh(const godot::Transform3D &p_building, const godot::Vector3 &p_focus, const godot::Vector3 &p_cursor,
            const std::vector<BoxPart> &p_ghost, GhostState p_state);
    void orbit(double p_yaw_delta, double p_elevation_delta);
    void zoom(double p_factor);
    godot::Vector3 get_forward() const; // 相机水平朝向（全局）
    godot::Vector3 get_right() const;
    double get_camera_yaw() const { return orbit_yaw; }
    godot::Camera3D *get_camera() const { return camera; }
};
