#pragma once

#include <vector>

#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/node_path.hpp>

#include "helm.h"
#include "player.h"
#include "ship.h"
#include "site_stream.h"
#include "world.h"
#include "world_map.h"

// 真实比例岛群场景（world_2051.tscn）的编排。
// 开局：空艇停在出发码头（origin_dock，默认主码头 #7）的泊位上，人站在它的停机坪上；码头与村落由 SiteStream 按距离铺。
// 走上船按 E 掌舵（Helm，与甲乙两岛同一套：R/F 换档），飞到目的码头（destination_dock，默认乙岛 #2）按 E 靠泊下船；
// HUD 给目的码头的方位与距离，到过了就改指回程。M 开合地图；dev_tools 为真时在地图上左键一点即瞬移（掌舵时不行）。
// 掉进云海回到上一处站稳的地方，船漂远了就召回离那儿最近的码头。
// 地面与村落的焦点：步行时是人，掌舵时是船头前方几秒的地方（高档飞得快，地面要先铺出来）。
// 「谁在受控」「到没到过对岸」「地图开没开」「上一处站稳的地方」这些状态只在这里；World、SiteStream、WorldMap、Player、Ship 互不相识。
class WorldScene : public godot::Node3D {
    GDCLASS(WorldScene, godot::Node3D)

    godot::NodePath world_path = "World";
    godot::NodePath player_path = "Player";
    godot::NodePath ship_path = "Ship"; // 可以不接：那就只能走、只能瞬移
    godot::NodePath sites_path = "SiteStream"; // 可以不接：那就没有码头与村落
    int origin_dock = 7; // 生成器的码头号
    int destination_dock = 2;
    bool dev_tools = true; // 开发者工具（地图瞬移）开不开
    double kill_altitude = -40.0; // 掉到这以下算坠入云海（云带顶是 y = 0）
    double land_search_m = 3000.0; // 瞬移落点是云海时，往外找多远的陆地
    double focus_lead_s = 3.0; // 掌舵时焦点提前船头几秒
    double recall_distance = 200.0; // 坠落回位时船离落脚点比这远，就召回最近的码头

    World *world = nullptr;
    Player *player = nullptr;
    Ship *ship = nullptr;
    SiteStream *sites = nullptr;
    WorldMap *map = nullptr;
    godot::Label *status_label = nullptr;
    godot::Label *prompt_label = nullptr;
    godot::Label *notice_label = nullptr;
    Helm helm;
    int origin_index = -1; // World 的码头下标
    int destination_index = -1;
    bool arrived = false; // 在目的码头那座岛上站稳过
    bool map_open = false;
    bool map_viewed = false; // 开过一次了
    bool has_safe_footing = false;
    godot::Vector3 safe_footing;
    double notice_time = 0.0;
    godot::Vector3 spawn_point;

    void build_hud();
    void update_hud();
    void set_map_open(bool p_open);
    void show_notice(const godot::String &p_text);
    void on_teleport_requested(const godot::Vector3 &p_position);
    // 找 (x, z) 附近的陆地（由近及远一圈圈找）；找不到返回 false
    bool nearest_land(double p_x, double p_z, godot::Vector3 &r_point) const;
    std::vector<Berth> berths() const; // 已建好的码头的泊位
    godot::Vector3 focus_point() const;
    void track_footing();
    void respawn();
    int nearest_berthable_dock(const godot::Vector3 &p_point) const;
    godot::String dock_label(int p_index) const; // 「乙岛 #1 · 码头 #2」这种

protected:
    static void _bind_methods();

public:
    void _ready() override;
    void _process(double p_delta) override;
    void _physics_process(double p_delta) override;
    void _unhandled_input(const godot::Ref<godot::InputEvent> &p_event) override;

    // 把玩家放到该处（世界 xz；落在地面上）。落点没有地就找最近的陆地；都找不到返回 false
    bool teleport_to(const godot::Vector3 &p_position);
    bool is_map_open() const { return map_open; }
    godot::Vector3 get_spawn_point() const { return spawn_point; }
    WorldMap *get_map() const { return map; }
    int get_state() const { return int(helm.get_state()); } // Voyage.State 的同一套值：0 步行 / 1 掌舵 / 2 靠泊中
    bool has_arrived() const { return arrived; }
    godot::String get_notice() const { return notice_time > 0.0 && notice_label ? notice_label->get_text() : godot::String(); }
    godot::String get_prompt() const { return prompt_label ? prompt_label->get_text() : godot::String(); }
    godot::Transform3D get_destination_berth() const;
    godot::Transform3D get_origin_berth() const;

    void set_world_path(const godot::NodePath &p_value) { world_path = p_value; }
    godot::NodePath get_world_path() const { return world_path; }
    void set_player_path(const godot::NodePath &p_value) { player_path = p_value; }
    godot::NodePath get_player_path() const { return player_path; }
    void set_ship_path(const godot::NodePath &p_value) { ship_path = p_value; }
    godot::NodePath get_ship_path() const { return ship_path; }
    void set_sites_path(const godot::NodePath &p_value) { sites_path = p_value; }
    godot::NodePath get_sites_path() const { return sites_path; }
    void set_origin_dock(int p_value) { origin_dock = p_value; }
    int get_origin_dock() const { return origin_dock; }
    void set_destination_dock(int p_value) { destination_dock = p_value; }
    int get_destination_dock() const { return destination_dock; }
    void set_dev_tools(bool p_value);
    bool get_dev_tools() const { return dev_tools; }
    void set_kill_altitude(double p_value) { kill_altitude = p_value; }
    double get_kill_altitude() const { return kill_altitude; }
    void set_dock_range(double p_value) { helm.dock_range = p_value; }
    double get_dock_range() const { return helm.dock_range; }
};
