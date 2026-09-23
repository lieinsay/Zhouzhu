#pragma once

#include "build_overlay.h"
#include "build_panel.h"
#include "ground_host.h"
#include "play_notice.h"
#include "player.h"
#include "settlement.h"

// 布置模式（开发者）：相机绕着地面光标转，WASD 相对相机挪光标、Q/R 转向、1-4 选建筑、E 放、X 拆、F5 把改动存回文件。
// 与场景无关的玩法部件（不是节点）：改哪一处聚落由编排递进来（地图宿主说脚下是哪处），这里只认它的 Settlement 与地面宿主；
// 光标在宿主的局部坐标里按 0.5 m 取整、贴着地面走。规矩在 settlement_model 里，这里只转述原因。开着的时候人站不站住由编排定。
class BuildMode {
    bool open = false;
    GroundHost *host = nullptr;
    Settlement *target = nullptr;
    godot::Vector3 cursor; // 全局，连续移动
    double yaw = 0.0; // 弧度
    int kind = 0;
    int selected = -1; // 光标下已有的那一座
    BuildPanel *panel = nullptr;
    BuildOverlay *overlay = nullptr;

public:
    static constexpr double CURSOR_SPEED = 8.0; // m/s；按住 Shift 三倍
    static constexpr double CURSOR_SPRINT = 3.0;
    static constexpr double SNAP = 0.5; // 放建筑时坐标取整到这么细
    static constexpr double YAW_STEP = 15.0; // 度
    static constexpr double ZOOM_STEP = 0.85;
    static constexpr double MOUSE_SENSITIVITY = 0.004;

    void setup(BuildPanel *p_panel, BuildOverlay *p_overlay);
    // 宿主下面的 Settlement 子节点；没有返回 nullptr
    static Settlement *settlement_of(GroundHost *p_host);

    bool is_open() const { return open; }
    // 在 p_host 的聚落里开布置模式，光标从 p_at 起；宿主没有聚落就拒绝（p_say 说为什么），返回是否开了
    bool open_on(GroundHost *p_host, const godot::Vector3 &p_at, const Say &p_say);
    void close(Player *p_player);
    void browse(double p_delta, bool p_interact, const Say &p_say);
    void refresh();
    void orbit(double p_dx, double p_dy);
    void zoom(bool p_in);

    godot::Vector3 cursor_snapped() const; // 光标按 0.5 m 格取整（宿主坐标）、贴地（全局）
    godot::String get_kind() const;
    int get_selected() const { return selected; }
    double get_yaw_deg() const;
    double get_camera_yaw() const { return overlay ? overlay->get_camera_yaw() : 0.0; }
    Settlement *get_target() const { return target; }
};
