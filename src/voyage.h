#pragma once

#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/node_path.hpp>

#include "island.h"
#include "player.h"
#include "ship.h"

// 切片一的编排：走上空艇 → 掌舵 → 飞到对岸 → 靠泊下船。
// 「现在是谁在受控」这份状态只在这里：Player 与 Ship 互不相识，各自只有一个 set_controlled。
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
    double helm_range = 2.2; // 离舵多近能上舵
    double dock_range = 45.0; // 离泊位多近能叫靠泊
    double leave_helm_speed = 3.0; // 半空中离舵：船速须低于此
    double kill_altitude = -30.0; // 掉到这以下算坠入云海

    State state = ON_FOOT;
    bool arrived = false;
    bool has_safe_footing = false;
    godot::Vector3 safe_footing;
    double notice_time = 0.0;
    godot::String notice;

    Island *origin = nullptr;
    Island *destination = nullptr;
    Ship *ship = nullptr;
    Player *player = nullptr;
    godot::Label *status_label = nullptr;
    godot::Label *prompt_label = nullptr;

    void build_hud();
    void update_hud();
    void board();
    void disembark();
    void seat_player();
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
};

VARIANT_ENUM_CAST(Voyage::State);
