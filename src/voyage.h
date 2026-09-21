#pragma once

#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/node_path.hpp>

#include "farm.h"
#include "island.h"
#include "player.h"
#include "ship.h"

// 主场景的编排。切片一：走上空艇 → 掌舵 → 飞到对岸 → 靠泊下船；切片二：步行时在河边开垦、播种、收获。
// 「现在是谁在受控」「手上的活干到哪了」「有多少粟」这些状态只在这里：
// Player、Ship、Farm 互不相识，各自只回答自己的事。
class Voyage : public godot::Node3D {
    GDCLASS(Voyage, godot::Node3D)

public:
    enum State {
        ON_FOOT,
        PILOTING,
        MOORING,
    };

private:
    godot::NodePath origin_island_path = "IslandA";
    godot::NodePath destination_island_path = "IslandB";
    godot::NodePath ship_path = "Ship";
    godot::NodePath player_path = "Player";
    godot::NodePath farm_path = "IslandA/Farm"; // 可以不接：那就没有农事
    double helm_range = 2.2; // 离舵多近能上舵
    double dock_range = 45.0; // 离泊位多近能叫靠泊
    double leave_helm_speed = 3.0; // 半空中离舵：船速须低于此
    double kill_altitude = -30.0; // 掉到这以下算坠入云海
    double reach = 1.8; // 够得着身前（视线方向）多远的那一格地
    int grain = 5; // 粟：既是收成也是种子

    State state = ON_FOOT;
    bool arrived = false;
    bool has_safe_footing = false;
    godot::Vector3 safe_footing;
    double notice_time = 0.0;
    godot::String notice;
    // 农事：视线前方的那一格、对它能做什么；按住 E 干的活（换格、松手即作废）
    godot::Vector2i target_cell = godot::Vector2i(-1, -1);
    Farm::Action target_action = Farm::ACTION_NONE;
    godot::Vector2i work_cell = godot::Vector2i(-1, -1);
    Farm::Action work_action = Farm::ACTION_NONE;
    double work_time = 0.0;

    Island *origin = nullptr;
    Island *destination = nullptr;
    Ship *ship = nullptr;
    Player *player = nullptr;
    Farm *farm = nullptr;
    godot::Label *status_label = nullptr;
    godot::Label *prompt_label = nullptr;

    void build_hud();
    void update_hud();
    void board();
    void disembark();
    void seat_player();
    void tend_farm(double p_delta, bool p_interact);
    void drop_work();
    void say(const godot::String &p_notice);
    godot::String refusal(const godot::Vector2i &p_cell) const;
    void track_footing();
    void respawn();
    Island *island_in_dock_range(godot::Transform3D &r_berth) const;
    Island *nearest_island(const godot::Vector3 &p_position) const;

protected:
    static void _bind_methods();

public:
    void _ready() override;
    void _physics_process(double p_delta) override;
    void _unhandled_input(const godot::Ref<godot::InputEvent> &p_event) override;

    State get_state() const { return state; }
    bool has_arrived() const { return arrived; }
    bool is_working() const { return work_action != Farm::ACTION_NONE; }

    void set_origin_island_path(const godot::NodePath &p_path) { origin_island_path = p_path; }
    godot::NodePath get_origin_island_path() const { return origin_island_path; }
    void set_destination_island_path(const godot::NodePath &p_path) { destination_island_path = p_path; }
    godot::NodePath get_destination_island_path() const { return destination_island_path; }
    void set_ship_path(const godot::NodePath &p_path) { ship_path = p_path; }
    godot::NodePath get_ship_path() const { return ship_path; }
    void set_player_path(const godot::NodePath &p_path) { player_path = p_path; }
    godot::NodePath get_player_path() const { return player_path; }
    void set_helm_range(double p_value) { helm_range = p_value; }
    double get_helm_range() const { return helm_range; }
    void set_dock_range(double p_value) { dock_range = p_value; }
    double get_dock_range() const { return dock_range; }
    void set_leave_helm_speed(double p_value) { leave_helm_speed = p_value; }
    double get_leave_helm_speed() const { return leave_helm_speed; }
    void set_kill_altitude(double p_value) { kill_altitude = p_value; }
    double get_kill_altitude() const { return kill_altitude; }
    void set_farm_path(const godot::NodePath &p_path) { farm_path = p_path; }
    godot::NodePath get_farm_path() const { return farm_path; }
    void set_reach(double p_value) { reach = p_value; }
    double get_reach() const { return reach; }
    void set_grain(int p_value) { grain = p_value; }
    int get_grain() const { return grain; }
};

VARIANT_ENUM_CAST(Voyage::State);
