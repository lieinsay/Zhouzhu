#pragma once

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>

#include "buildings.h"

// 开发者布置模式的视角：只负责画，不存清单、不读输入。
//   · 一台环绕光标的相机：绕焦点转（yaw / 俯仰），滚轮拉近拉远；上层把鼠标的移动递进来，把焦点（光标位置）每帧递进来。
//   · 光标处一座半透明的幽灵建筑（台基 + 墙 + 悬山顶），按上层给的状态上色：能盖是绿、不能盖是红、落在已有建筑上是白（选中它）。
//   · 脚下一圈占地描边，同色。
// 幽灵的形状按建筑表的尺寸拼，换了种类就重拼。
class BuildOverlay : public godot::Node3D {
    GDCLASS(BuildOverlay, godot::Node3D)

public:
    enum GhostState {
        GHOST_CAN_PLACE,
        GHOST_CANNOT_PLACE,
        GHOST_SELECTED,
    };

private:
    godot::Camera3D *camera = nullptr;
    godot::Node3D *ghost = nullptr;
    const BuildingDef *ghost_def = nullptr;
    godot::Ref<godot::StandardMaterial3D> mat_ok, mat_bad, mat_selected, mat_edge_ok, mat_edge_bad, mat_edge_selected;

    double orbit_yaw = 0.6;
    double orbit_elevation = 0.7;
    double orbit_distance = 30.0;
    godot::Vector3 focus;

    void ensure_built();
    void rebuild_ghost(const BuildingDef &p_def);
    void place_camera();

protected:
    static void _bind_methods();

public:
    void show_at(const godot::Vector3 &p_focus); // 打开时：焦点与默认视角
    // 每帧：焦点、幽灵的位姿（全局，原点在地坪中心）与状态
    void refresh(const godot::Vector3 &p_focus, const BuildingDef &p_def, const godot::Transform3D &p_ghost, GhostState p_state);
    void orbit(double p_yaw_delta, double p_elevation_delta);
    void zoom(double p_factor);
    // 相机的水平朝向：前（相机看的方向压到水平面）与右，上层据此挪光标
    godot::Vector3 get_forward() const;
    godot::Vector3 get_right() const;
    double get_camera_yaw() const { return orbit_yaw; }
    godot::Camera3D *get_camera() const { return camera; }
};
