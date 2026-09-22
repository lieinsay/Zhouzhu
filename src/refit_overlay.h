#pragma once

#include <vector>

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/label3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>

#include "ship_layout.h"

// 改装的装配视角：只负责画，不存布局、不读输入。
// 自己的变换跟着船走（上层每次刷新时把船的全局位姿递进来），所以下面画的东西都在船的局部坐标里：
//   · 可放的空格：半透明的浅蓝盒子；
//   · 光标格：一只描边的盒子——空格能放是黄、不能放是红、落在已有的节上是白（选中它）；
//   · 船头 / 船尾 / 左舷 / 右舷 四个标签，让人一眼认出方向；
//   · 一台环绕相机：绕船的中心转（yaw / 俯仰），距离按船的尺寸取，上层把鼠标的移动递进来。
class RefitOverlay : public godot::Node3D {
    GDCLASS(RefitOverlay, godot::Node3D)

    godot::Camera3D *camera = nullptr;
    godot::Node3D *cursor = nullptr;
    godot::MeshInstance3D *cursor_fill = nullptr;
    std::vector<godot::MeshInstance3D *> cursor_edges;
    std::vector<godot::MeshInstance3D *> candidates;
    godot::Label3D *labels[4] = { nullptr, nullptr, nullptr, nullptr };
    godot::Ref<godot::StandardMaterial3D> mat_candidate, mat_ok, mat_bad, mat_selected, mat_edge_ok, mat_edge_bad, mat_edge_selected;

    double orbit_yaw = 0.7;
    double orbit_elevation = 0.45;
    double orbit_distance = 24.0;
    godot::Vector3 focus;

    void ensure_built();
    void place_camera();

protected:
    static void _bind_methods();

public:
    enum CursorState {
        CURSOR_CAN_PLACE, // 空格，能放
        CURSOR_CANNOT_PLACE, // 空格，不能放
        CURSOR_SELECTED, // 已有的节
    };

    // 打开时：对准这艘船、把相机摆到默认的斜后方
    void show_for(const ShipLayout &p_layout, const godot::Transform3D &p_ship);
    // 每帧：布局、光标与候选格
    void refresh(const ShipLayout &p_layout, const godot::Transform3D &p_ship, const Cell &p_cursor, CursorState p_state, const std::vector<Cell> &p_candidates);
    void orbit(double p_yaw_delta, double p_elevation_delta);
    godot::Camera3D *get_camera() const { return camera; }
};
