#pragma once

#include <map>
#include <set>
#include <memory>
#include <vector>

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/node_path.hpp>

#include "job_pool.h"

class PlanetView;
class GroupWorld;

// 远处的岛群（行星计划 P1，docs/PLAN-PLANET.md 第 4.3 节「60–600 km」）：相机 radius 以内的岛群（锚群除外）按行星包的粗版画低模。
//   - 网格在后台建（共用的任务池 PlanetJobs 的 0 号通道，JobKind::FAR_GROUP，优先级 = 离相机多远）：far_group_mesh 的顶面、崖壁、倒锥的岛底，顶点已按真球面弯过；
//   - 粗版按距离两档：fine_distance 以内 500 m、以外 2 km（回滞 fine_hysteresis）；换档时旧的留着，新的建好再换；
//   - 每帧按 PlanetView 给的「群心相对相机」的刚体变换（双精度算）摆上去，所以远处的群落在真球面上、从地平线上冒出来；
//   - 着色器 game/shaders/far_group.gdshader：地表色（与岛群地皮同一份调色）、季相与积雪按各群自己的纬度与气候（行星包的年均温、温差、台面）
//     算的简化版海拔表——实例参数 season_lines =（雪线、枯黄线、秋色、落叶），CPU 按全局钟的历日每个游戏钟头算一回；
//   - 不投影、不碰撞（D7：非锚的群只画）；着色器把远处沿视线压近（planet_squeeze），引擎按真位置剔会被远平面剪掉——
//     包围盒放到极大，每帧按视锥的四个侧面自己剔（侧面过相机，压近不改方向）。
// 行星计划 P4：还没有粗版的群（生存模式的新世界）画替身——planet_surface 按行星包的数撒的那一团（与行星贴图里的替身同一份），2 km 一档；
//   行星包加进或替换某群的粗版（该群 lod_generation 变了）就重建，替身和已有普通网格都刷新。
class FarGroups : public godot::Node3D {
    GDCLASS(FarGroups, godot::Node3D)

    godot::NodePath planet_path = "../PlanetView";
    godot::NodePath map_path = "../World";
    double radius = 600000.0;
    double fine_distance = 280000.0;
    double fine_hysteresis = 40000.0;
    double reselect_m = 4000.0; // 相机挪了这么远才重挑群
    double fine_res = 500.0, coarse_res = 2000.0;

    struct Group {
        int node = -1;
        double distance = 0.0;
        double want_res = 0.0; // 该用哪一档（米）
        double built_res = 0.0; // 挂着的是哪一档（0 = 还没有）
        double queued_res = 0.0; // 交出去、还没挂上也没扔掉的是哪一档（0 = 没有）：做好了取进 ready 以后池里就不算「忙」，靠它才不重复交
        uint64_t built_lod_gen = 0, queued_lod_gen = 0;
        bool stand_in = false; // 挂着的是替身（行星包里还没有它的粗版）
        godot::MeshInstance3D *mesh = nullptr;
        int triangles = 0;
        godot::Vector3 center; // 网格的包围球（群的切平面坐标）：每帧按视锥的四个侧面自己剔
        double sphere = 0.0;
        bool shown = true;
    };
    std::map<int, Group> groups;
    std::set<int> hidden; // GroupStream 载入了、地面块挂齐了的群：粗版藏起来（行星计划 P3）
    std::shared_ptr<JobPool> pool; // 场景里共用的（PlanetJobs）；没有就借 map_path 那个岛群的
    PlanetView *planet = nullptr;
    godot::Ref<godot::ShaderMaterial> material;
    bool selected = false;
    double sel_lat = 0.0, sel_lon = 0.0;
    double season_hour = -1e9;
    int adopted = 0; // 挂上过几份网格（统计）
    uint64_t lod_gen_seen = 0; // 粗版更新后，只重建代次有变的群。
    int replaced = 0; // 替身换成了粗版几回（统计）
    void refresh_lods();
    std::vector<std::unique_ptr<JobResult>> ready; // 做好了等挂的（一帧挂一份：一份上传要几毫秒；走远了、档换了的每帧全扔）

    void select();
    void submit(Group &p_group);
    void adopt_ready();
    void place_all();
    void update_seasons(bool p_force);
    void cancel_all();

protected:
    static void _bind_methods();
    void _notification(int p_what);

public:
    void _ready() override;
    void _process(double p_delta) override;

    // 统计（测试与工具）
    int get_group_count() const; // 挂着网格的群数
    int get_chunk_count() const { return get_group_count(); }
    int get_triangle_count() const;
    int get_wanted_count() const { return int(groups.size()); }
    bool is_idle() const; // 该挂的都挂上了（档也对）
    godot::PackedInt32Array get_nodes() const; // 挂着网格的群号
    // 某群的粗版藏不藏（GroupStream 把它载入成近景 / 细版、地面块挂齐了就藏）
    void set_hidden(int p_node, bool p_hidden);
    bool is_hidden(int p_node) const { return hidden.count(p_node) != 0; }
    double get_group_resolution(int p_node) const; // 挂着的那一档（米）；没有 0
    int get_stand_in_count() const; // 挂着替身的群数
    int get_replaced_count() const { return replaced; } // 替身换成粗版几回
    bool is_stand_in(int p_node) const;

    void set_planet_path(const godot::NodePath &p_value) { planet_path = p_value; }
    godot::NodePath get_planet_path() const { return planet_path; }
    void set_map_path(const godot::NodePath &p_value) { map_path = p_value; }
    godot::NodePath get_map_path() const { return map_path; }
    void set_radius(double p_value) { radius = p_value; }
    double get_radius() const { return radius; }
    void set_fine_distance(double p_value) { fine_distance = p_value; }
    double get_fine_distance() const { return fine_distance; }
};
