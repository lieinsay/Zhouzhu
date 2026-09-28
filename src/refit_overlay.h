#pragma once

#include <vector>

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/label3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>

#include "refit_pick.h"
#include "ship_layout.h"

// 改装的装配视角：只负责画，不存布局、不读输入。
// 自己的变换跟着船走（上层每次刷新时把船的全局位姿递进来），所以下面画的东西都在船的局部坐标里：
//   · 亮格：拖着（或指着面板上的）一种节点时，它眼下能放的格子一格格发亮、一呼一吸；
//   · 虚影：拖着的节点落在鼠标指着的地方是什么样——整个节点的外包盒，能放黄绿、不能放红，头顶写节点名或不能放的原因；
//   · 选中框：点选的节点一圈白框；鼠标指着的节点一圈淡框；
//   · 船头 / 船尾 / 左舷 / 右舷 四个标签，让人一眼认出方向；
//   · 一台环绕相机：绕船的中心转（yaw / 俯仰）、滚轮拉远拉近，距离按船的尺寸取；并把屏幕上的一点换成船局部坐标里的射线。
class RefitOverlay : public godot::Node3D {
    GDCLASS(RefitOverlay, godot::Node3D)

    // 一只描边的盒子：半透明的填充 + 十二条棱（棱不做深度测试，隔着船身也看得见）
    struct Frame {
        godot::Node3D *root = nullptr;
        godot::MeshInstance3D *fill = nullptr;
        godot::MeshInstance3D *edges[12] = {};
    };

    godot::Camera3D *camera = nullptr;
    Frame ghost, selection, hovered;
    godot::Label3D *ghost_label = nullptr;
    std::vector<godot::MeshInstance3D *> glows;
    godot::Label3D *labels[4] = { nullptr, nullptr, nullptr, nullptr };
    godot::Ref<godot::StandardMaterial3D> mat_glow, mat_ok, mat_bad, mat_selected, mat_edge_ok, mat_edge_bad, mat_edge_selected, mat_edge_hover;

    double orbit_yaw = 0.7;
    double orbit_elevation = 0.45;
    double orbit_distance = 24.0;
    double base_distance = 24.0;
    double screen_shift = 0.0; // 船往屏幕右边挪多少像素（让开左边的面板）
    godot::Vector3 focus;

    void ensure_built();
    Frame make_frame(const char *p_name);
    void place_frame(Frame &p_frame, const CellBox &p_box, real_t p_pad, const godot::Ref<godot::StandardMaterial3D> &p_fill,
            const godot::Ref<godot::StandardMaterial3D> &p_edge);
    void place_camera();

protected:
    static void _bind_methods();

public:
    struct View {
        const ShipLayout *layout = nullptr;
        godot::Transform3D ship; // 船的全局位姿
        std::vector<Cell> glow; // 亮格
        double pulse = 0.0; // 0..1：亮格一呼一吸
        bool ghost = false; // 拖着的节点的虚影
        CellBox ghost_box;
        bool ghost_legal = false;
        godot::String ghost_text; // 头顶那行：节点名或不能放的原因
        bool selected = false;
        CellBox selected_box;
        bool hovered = false; // 鼠标指着的节点（没拖东西时）
        CellBox hovered_box;
    };

    // 打开时：对准这艘船、把相机摆到默认的斜后方
    void show_for(const ShipLayout &p_layout, const godot::Transform3D &p_ship);
    // 每帧
    void refresh(const View &p_view);
    void orbit(double p_yaw_delta, double p_elevation_delta);
    void zoom(bool p_in);
    // 船在屏幕上往右挪 p_pixels 像素（相机横移，不转向）：让开左边的面板
    void set_screen_shift(double p_pixels);
    godot::Camera3D *get_camera() const { return camera; }
    // 屏幕上一点（视口坐标）→ 船局部坐标里的射线（用最近一次刷新的船位姿）
    PickRay ray_at(const godot::Vector2 &p_screen) const;
    // 船局部坐标的一点 → 屏幕上的一点；在相机后面返回 false
    bool screen_point(const godot::Vector3 &p_local, godot::Vector2 &r_screen) const;
};
