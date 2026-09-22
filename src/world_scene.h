#pragma once

#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/node_path.hpp>

#include "player.h"
#include "world.h"
#include "world_map.h"

// 真实比例岛群场景（world_2051.tscn）的编排：玩家在 World 的流式地面上走，M 开合地图，
// dev_tools 为真时在地图上左键一点即瞬移过去（落点是云海就挪到最近的陆地）。
// 出生在邑治；掉进云海回到上一处站稳的地方。
// 「地图开没开」「上一处站稳的地方」「刚才提示了什么」这些状态只在这里；World、WorldMap、Player 互不相识。
class WorldScene : public godot::Node3D {
    GDCLASS(WorldScene, godot::Node3D)

    godot::NodePath world_path = "World";
    godot::NodePath player_path = "Player";
    bool dev_tools = true; // 开发者工具（地图瞬移）开不开
    double kill_altitude = -40.0; // 掉到这以下算坠入云海（云带顶是 y = 0）
    double land_search_m = 3000.0; // 瞬移落点是云海时，往外找多远的陆地

    World *world = nullptr;
    Player *player = nullptr;
    WorldMap *map = nullptr;
    godot::Label *status_label = nullptr;
    godot::Label *notice_label = nullptr;
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

    void set_world_path(const godot::NodePath &p_value) { world_path = p_value; }
    godot::NodePath get_world_path() const { return world_path; }
    void set_player_path(const godot::NodePath &p_value) { player_path = p_value; }
    godot::NodePath get_player_path() const { return player_path; }
    void set_dev_tools(bool p_value);
    bool get_dev_tools() const { return dev_tools; }
    void set_kill_altitude(double p_value) { kill_altitude = p_value; }
    double get_kill_altitude() const { return kill_altitude; }
};
