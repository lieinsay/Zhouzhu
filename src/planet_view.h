#pragma once

#include <map>
#include <memory>

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "planet_data.h"
#include "planet_frame.h"

class GroupWorld;
class MapHost;

// 行星的画面（行星计划 P1，docs/PLAN-PLANET.md 第 4.2 节）：一个场景一个，挂在有行星数据的地图里（world_2051.tscn）。
// 读行星包（planet_dir：planet.bin + lods.bin，纯 C++ 的 PlanetData），锚 = 地图宿主的岛群（world.json 的 source.lat / lon / node）；
// 每帧按当前相机（在锚群的平面坐标里）用双精度算出：
//   c、R_c、δ(c) → 全局着色器参数 planet_curve_*（地皮、散布、河、天空的云层、弯曲标准材质、云带顶都读它，visual(p) 见 planet_curve.gdshaderinc）；
//   相机的经纬度（AEQD 逆算）→ 天与光按它算日月、钟按参考经度的地方时（D10：clock_node 的经度，2051 = 152.15°E）；
//   远处岛群的刚体摆放（group_transform：群心相对相机的位置与朝向，FarGroups 用）；
//   当前相机的远近平面：远平面至少 camera_far（60 km：着色器把 10 km 外沿视线压到 26 km 以内，见 CurveSqueeze），近平面随离地高度放大（高空时到 1 m）。
// 云带顶：把场景里的平板云海（cloud_sea_path）藏起来，换成一张跟着相机走的同心环网格（y = −3800，D13），半径 cloud_deck_radius，按真球面弯。
// 没有行星数据（读不了包、地图没有经纬度）就什么都不做：planet_curve_on = 0，一切照旧。离开场景时把 planet_curve_on 设回 0（全局参数跨场景留着）。
// 给拾取、HUD、地图的 CPU 版：to_visual / to_flat、latlon_at、bearing / distance 到某群，见 _bind_methods。
class PlanetView : public godot::Node3D {
    GDCLASS(PlanetView, godot::Node3D)

    static PlanetView *active;

    godot::String planet_dir = "res://planet/seed42";
    godot::NodePath map_path = "../World";
    godot::NodePath cloud_sea_path = "../CloudSea";
    int clock_node = 2051; // 钟点 = 这个群的地方时（D10）
    double camera_far = 60000.0; // > CurveSqueeze::LIMIT_M
    double cloud_deck_radius = 1000000.0;
    double near_min = 0.05, near_max = 1.0; // 近平面：离地 near_rise_from 以下是 near_min，往上按高度放大到 near_max
    double near_rise_from = 1000.0, near_rise_to = 6000.0;

    std::shared_ptr<PlanetData> planet; // 远处岛群的后台活也拿着一份（场景拆的时候活可能还在做）
    MapHost *map = nullptr;
    bool loaded = false; // 行星包读到了
    bool anchored = false; // 锚定到了地图的岛群
    int anchor_node = -1;
    GroupFrame anchor;
    CurveView view;
    PlanetVec cam;
    PlanetVec cam_planet; // 相机的真位置（行星系）
    PlanetBasis cam_axes; // 相机处的东、上、南（行星系）
    double cam_lat = 0.0, cam_lon = 0.0;
    double clock_lon = 0.0;
    double ground_clearance = 0.0;
    godot::MeshInstance3D *deck = nullptr;
    godot::Node3D *cloud_sea = nullptr;
    std::map<uint64_t, std::pair<double, double>> base_near; // 相机实例号 → 场景里配的近平面、远平面
    bool globals_on = false;
    double squeeze_keep = CurveSqueeze::KEEP_M, squeeze_limit = CurveSqueeze::LIMIT_M;

    bool load_planet();
    bool try_anchor();
    void build_deck();
    void update_camera(godot::Camera3D *p_camera);
    void push_globals();
    PlanetVec true_position(const PlanetVec &p_flat) const { return anchor.to_planet(p_flat); }

protected:
    static void _bind_methods();
    void _notification(int p_what);

public:
    ~PlanetView();
    void _ready() override;
    void _process(double p_delta) override;

    // 场景里正在用的那一个（没有、或没锚定返回 nullptr）：World 的剔除、天与光、远处岛群从这里问
    static PlanetView *active_view();

    bool is_active() const { return loaded && anchored; }
    // 换锚（GroupStream 在换锚那一帧调）：锚群改成这一群（它的 world.json 的 source），弯曲、经纬度、刚体摆放从下一回 _process 起按它算
    void set_anchor_world(const GroupWorld *p_world);
    const PlanetData *get_planet() const { return planet.get(); }
    std::shared_ptr<const PlanetData> share_planet() const { return planet; }
    int get_anchor_node() const { return anchor_node; }
    const GroupFrame &get_anchor() const { return anchor; }
    const CurveView &get_view() const { return view; }
    PlanetVec camera_flat() const { return cam; }
    // 相机处的东、上、南（行星系；列）与相机的真位置（行星系，米）
    const PlanetBasis &camera_axes() const { return cam_axes; }
    PlanetVec camera_planet() const { return cam_planet; }
    double get_squeeze_keep() const { return squeeze_keep; }
    double get_squeeze_limit() const { return squeeze_limit; }
    godot::MeshInstance3D *get_deck() const { return deck; } // P1 的云带顶网格（P2 高空淡出）
    // 锚群平面坐标 p_flat 处的地面高（地图宿主问的；不在地图里 NaN）
    double surface_at(const PlanetVec &p_flat) const;
    double get_radius() const { return anchor.radius; }
    // 画面上离相机 p_distance（水平）处下弯多少（米，按真球面，剔除的余量用）
    double drop_at(double p_distance) const;
    // 群 p_node 的平面坐标 → 画面（世界）坐标的刚体变换：群心相对相机的位置与朝向（双精度算好再转单精度）；
    // 群内的弯曲（δ）由网格自己带（far_group_mesh 的顶点已弯过）。没有这个群返回单位变换
    godot::Transform3D group_transform(int p_node) const;
    // 群 p_node 的群心离相机多远（米，大圆）
    double group_distance(int p_node) const;

    // 给脚本的 CPU 版
    godot::Vector2 get_camera_latlon() const { return godot::Vector2(real_t(cam_lat), real_t(cam_lon)); }
    // 相机在真球面上的位置（行星系，米，双精度）：换锚前后比它（经纬度的单精度只准到一两米）
    godot::PackedFloat64Array get_camera_planet_position() const;
    double get_camera_lat() const { return cam_lat; }
    double get_camera_lon() const { return cam_lon; }
    double get_clock_longitude() const { return clock_lon; }
    // 相机处的地方时比钟点早多少钟头（东边早，15° 一小时）
    double get_local_hour_offset() const;
    double get_ground_clearance() const { return ground_clearance; }
    // 相机处的地理方向（x 东、y 上、z 南：历法算的日月就是这么给的）→ 画面的世界轴（锚群平面轴在相机处的像）。
    // 两者只差锚群投影在相机处的子午线收敛角（离群心几十公里时零点几度）；换锚前后这个角不一样，所以日月按它转，画面才不跳（行星计划 P3）
    godot::Vector3 enu_to_visual(const godot::Vector3 &p_enu) const;
    godot::Vector2 latlon_at(const godot::Vector3 &p_flat) const;
    godot::Vector3 to_visual(const godot::Vector3 &p_flat) const;
    godot::Vector3 to_flat(const godot::Vector3 &p_visual) const;
    // 群 p_node 的粗版最高点在画面上的位置（截图工具瞄准用）；没有返回 (0,0,0)
    godot::Vector3 group_peak_visual(int p_node) const;
    double bearing_to_node(int p_node) const; // 从相机看那一群的方位（度，北起顺时针）
    double distance_to_node(int p_node) const { return group_distance(p_node); }
    int nearest_node() const; // 离相机最近的群

    void set_planet_dir(const godot::String &p_value) { planet_dir = p_value; }
    godot::String get_planet_dir() const { return planet_dir; }
    void set_map_path(const godot::NodePath &p_value) { map_path = p_value; }
    godot::NodePath get_map_path() const { return map_path; }
    void set_cloud_sea_path(const godot::NodePath &p_value) { cloud_sea_path = p_value; }
    godot::NodePath get_cloud_sea_path() const { return cloud_sea_path; }
    void set_clock_node(int p_value) { clock_node = p_value; }
    int get_clock_node() const { return clock_node; }
    void set_camera_far(double p_value) { camera_far = p_value; }
    double get_camera_far() const { return camera_far; }
    void set_cloud_deck_radius(double p_value) { cloud_deck_radius = p_value; }
    double get_cloud_deck_radius() const { return cloud_deck_radius; }
};
