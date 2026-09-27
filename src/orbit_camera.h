#pragma once

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include "planet_frame.h"

class PlanetView;

// 开发者的轨道相机（行星计划 P2，F7，DevTools 建、挂在当前场景里）：从人的相机的位姿起飞，一路升到两万公里看整颗星，Esc 落回人身上。
// 状态全用双精度：相机在行星上的经纬度、离零点多高（游戏 y）、航向（北起顺时针）与俯仰——每帧换成锚群的平面坐标（AEQD 逆算）摆 Camera3D，
// PlanetView 从这里拿双精度的位置（has_true_position / get_true_flat），所以离锚群几千公里、两万公里高时画面也不抖。
// 朝向就在相机的局部系里（画面的世界轴 = 相机处的东、上、南）。
// 操作：鼠标（或方向键）转视角；WASD 沿大圆平着飞、空格 / Ctrl 升降、滚轮升降一档（×1.3），都按离地多高放大（对数地快：
// 平飞每秒走 (离地高 + 50 m) × move_rate，升降每秒高度乘 e^climb_rate），Shift ×4；不低于脚下的地面（锚群外是云带顶）2 m。
// Esc 或 F7 发 leave 信号，DevTools 把人的相机换回来。
class OrbitCamera : public godot::Camera3D {
    GDCLASS(OrbitCamera, godot::Camera3D)

    double lat = 0.0, lon = 0.0, height = 0.0; // 度、度、游戏 y（米）
    double heading = 0.0, pitch = -20.0; // 度
    double wheel_clearance = -1.0; // 滚轮设的离地高（平滑逼近）；< 0 没在滚
    double move_rate = 0.6;
    double climb_rate = 0.55;
    double wheel_step = 1.3;
    double min_clearance = 2.0;
    double look_speed = 0.12; // 鼠标每像素几度
    bool has_state = false;
    bool autopilot = false; // 脚本在摆（工具、测试）：不读键鼠
    PlanetVec flat; // 锚群平面坐标（双精度）
    double ground = 0.0; // 脚下的地面（游戏 y）
    godot::ObjectID view_id;

    PlanetView *view() const;
    void apply();
    double ground_at(const PlanetVec &p_flat) const;

protected:
    static void _bind_methods();

public:
    void _ready() override;
    void _process(double p_delta) override;
    void _input(const godot::Ref<godot::InputEvent> &p_event) override;

    // 接过某个相机的位姿（位置、朝向、视角、近平面）
    void begin_from(godot::Camera3D *p_from, PlanetView *p_view);
    bool has_true_position() const { return has_state; }
    PlanetVec get_true_flat() const { return flat; }

    // 给脚本：直接摆（纬度、经度、游戏 y、航向、俯仰，度 / 米）；摆过之后不读键鼠，直到 set_autopilot(false)
    void set_orbit_state(double p_lat, double p_lon, double p_height, double p_heading, double p_pitch);
    void set_autopilot(bool p_value) { autopilot = p_value; }
    bool is_autopilot() const { return autopilot; }
    godot::Vector2 get_latlon() const { return godot::Vector2(real_t(lat), real_t(lon)); }
    double get_height() const { return height; }
    double get_clearance() const { return height - ground; }
    double get_heading() const { return heading; }
    double get_pitch() const { return pitch; }
    // 此刻平飞 / 升降多快（m/s，不含 Shift）
    double get_move_speed() const;
    void wheel(int p_notches); // 正 = 升
};
