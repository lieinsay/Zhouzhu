#pragma once

#include <vector>

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/node_path.hpp>

#include "map_host.h"

class GroupStream;
class GroupWorld;

// 行星上的地图宿主（行星计划 P3，计划第五节 P3 第 6 条）：Expedition 的 map 指它，它实现 MapHost，按位置分派给对应的岛群（GroupStream 管着的 GroupWorld）。
// 玩家与船都在锚群的平面里（D7），所以地面、脚下的聚落、农田、船坞、采集、跑货、野泊、天气都问锚群；
// 流式焦点给锚群（它的地面、村子、散布），同时喂给 GroupStream（它按焦点给各群分档、给邻群换算焦点）。
// 跨群的：
//   地面高：锚群脚下没有地时再按真球面问载入了的邻群（瞬移找陆地、PlanetView 算离地多高）；
//   码头表：场景里摆的那一群（home，2051）的码头在前——下标、码头号都与它自己的一样（场景里配的 origin_dock = 1、destination_dock = 26 照旧）；
//     别的群的码头跟在后面，码头号 = 群号 × 1000 + 它的码头号（2050 的 #1 是 2050001），名字带群号；位置、泊位按真球面换到锚群平面，
//     还没载入的群按 sites.tsv 的点给位置（泊位没算，靠不了）；只有锚群的码头「此刻靠得上」（栈桥是它的 SiteStream 铺的）；
//   岛号：home 的照旧，别的群 = 群号 × 1000 + 岛号（到港判定按岛号比）；
//   HUD 第一行：「群 #2051 · 岛 #2 · 东 … 南 … · 高 …」；在云海上空时报离各群的陆地多远；
//   地图面板：画的是锚群；换了锚，下回打开时换成新锚群的图。
// 换锚（map_shift_*）转给 GroupStream：它定、Expedition 搬人船、它收尾。
class PlanetHost : public godot::Node3D, public MapHost {
    GDCLASS(PlanetHost, godot::Node3D)

    godot::NodePath stream_path = "../GroupStream";
    godot::NodePath home_path = "../World";

    GroupStream *stream = nullptr;
    // 场景里摆的那一群：它离开近景时 World 会被拆掉、回来时重建（GroupStream 管），所以不留指针——就绪前按 ObjectID 找原来那个，就绪后问 GroupStream
    godot::ObjectID home_id;
    int home_node = -1;
    GroupWorld *home_world() const;

    struct DockRef {
        int node = -1; // 群号
        int local = -1; // 那一群自己的码头下标
        int id = -1; // 这里的码头号
        bool home = false;
    };
    std::vector<DockRef> docks;
    bool docks_full = false; // GroupStream 就绪后补上了别的群的码头
    int docks_groups = 0; // 补码头时 GroupStream 有几群（按需生成的群加进来了就再补，行星计划 P4）
    int panel_node = -1; // 地图面板此刻画的是哪一群

    GroupWorld *anchor_world() const;
    void build_docks();
    int group_index_of(int p_node) const;
    int code_island(int p_node, int p_island) const;

protected:
    static void _bind_methods();

public:
    void _ready() override;
    void _process(double p_delta) override;

    GroupStream *get_stream() const { return stream; }

    // MapHost
    godot::Node3D *map_node() override;
    bool map_ready() const override;
    double map_surface(const godot::Vector3 &p_global) const override;
    int map_island_at(const godot::Vector3 &p_global) const override;
    bool map_footing(const godot::Vector3 &p_global) const override;
    int map_dock_count() const override;
    int map_dock_id(int p_index) const override;
    bool map_dock_open(int p_index) const override;
    godot::Transform3D map_dock_berth(int p_index) const override;
    godot::Transform3D map_dock_spawn(int p_index) const override;
    godot::Vector3 map_dock_position(int p_index) const override;
    int map_dock_island(int p_index) const override;
    godot::String map_dock_label(int p_index) const override;
    std::vector<Shipyard *> map_shipyards() const override;
    Farm *map_farm_at(const godot::Vector3 &p_global) const override;
    GroundHost *map_settlement_at(const godot::Vector3 &p_global) const override;
    void map_focus(const godot::Vector3 &p_global) override;
    void map_prepare(const godot::Vector3 &p_global) override;
    int map_pending() const override;
    godot::String map_location_text(const godot::Vector3 &p_global) const override;
    WorldMap *map_make_panel() override;
    void map_refresh_panel(WorldMap *p_panel) const override;
    double map_view_distance() const override { return 60000.0; }
    const std::vector<FreightPort> &map_freight_ports() const override;
    FreightPost map_freight_post_near(const godot::Vector3 &p_global, double p_range) override;
    bool map_freight_target(int p_port, godot::Transform3D &r_target) const override;
    ResourceSpot map_resource_near(const godot::Vector3 &p_global, double p_range) override;
    ResourceSpot map_gather_near(const godot::Vector3 &p_global, double p_face_range, double p_facing_yaw) override;
    void map_resource_taken(const ResourceSpot &p_spot) override;
    bool map_landing_height(double p_x, double p_z, double &r_y) const override;
    bool map_water_at(double p_x, double p_z) const override;
    WorksSite map_works_at(const godot::Vector3 &p_global) override;
    bool map_earth_cell(const godot::Vector3 &p_global, int &r_i, int &r_j) const override;
    bool map_earth_cell_center(int p_i, int p_j, godot::Vector3 &r_global) const override;
    bool map_earth_facts(int p_i, int p_j, const godot::Vector3 &p_player, EarthFacts &r_facts) override;
    int map_earth_step(int p_i, int p_j) const override;
    bool map_set_earth_step(int p_i, int p_j, int p_step) override;
    WeatherModel *map_weather() override;
    void map_weather_models(std::vector<WeatherModel *> &r_out) override;
    bool map_shift_pending() const override;
    bool map_shift_begin(MapShift &r_shift) override;
    void map_shift_commit() override;
    bool map_shift_toward(const godot::Vector3 &p_global) override;
    bool map_jump_begin(int p_node, godot::String &r_why) override;
    int map_jump_poll(double &r_progress, godot::String &r_text) override;
    bool map_jump_commit() override;
    bool map_jump_landing(godot::Transform3D &r_spawn, godot::Transform3D &r_berth, bool &r_has_berth, godot::String &r_where) override;
    void map_jump_end() override;

    // 给脚本（测试）
    int get_anchor_node() const;
    int find_dock_id(int p_node, int p_local_id) const; // 某群某码头在这里的码头号；没有 −1
    godot::Transform3D get_dock_berth_by_id(int p_id) const; // 锚群平面；没有返回单位变换
    godot::Transform3D get_dock_spawn_by_id(int p_id) const;
    godot::String get_location_text(const godot::Vector3 &p_global) const { return map_location_text(p_global); }

    void set_stream_path(const godot::NodePath &p_value) { stream_path = p_value; }
    godot::NodePath get_stream_path() const { return stream_path; }
    void set_home_path(const godot::NodePath &p_value) { home_path = p_value; }
    godot::NodePath get_home_path() const { return home_path; }
};
