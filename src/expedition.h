#pragma once

#include <vector>

#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/node_path.hpp>

#include "build_mode.h"
#include "farm_hand.h"
#include "freight_hand.h"
#include "helm.h"
#include "interior_edit.h"
#include "map_host.h"
#include "pack_hand.h"
#include "player.h"
#include "refit_bench.h"
#include "ship.h"
#include "world_map.h"

// 玩家编排：哪张地图都是它（甲乙两岛 main.tscn、岛群 world_2051.tscn、平板岛测试图），场景只配属性。
// 地图只认地图宿主（MapHost，map 属性指过去）：地面、码头、船坞、农田、脚下的聚落、流式焦点、岛群地图面板都问它。
// 玩法是几个与场景无关的部件，这里持有、按优先级调度：
//   掌舵（Helm）：上舵、飞（R/F 换档）、叫靠泊 / 入坞、离舵；
//   背包与手持（PackHand，Tab）、农事（FarmHand，按住 E）、改装（RefitBench，画样台按 E）、布置模式（BuildMode，B，开发者）、
//   跑货（FreightHand：港口村的村仓前接托运、打包、交货，船上货位前装卸；抱着货箱不能掌舵、不能改船，船上有货也不能改船）、
//   室内编辑（InteriorEdit，T，开发者：布置模式里光标下那座，或人站在里面的那座）。
// 另外：开局把船停在出发码头的泊位上、人放在它的停机坪上；HUD（位置、目标、背包、钟点、提示）；M 开合岛群地图、开发者点图瞬移；
// 站稳记落脚点、掉进云海回位并召回漂远的船；到了目的码头那座岛就算到了（arrived），之后目标改指回程；接了托运时目标改指托运的目的港。
// 「现在是谁在受控」「哪个面板开着」「到没到过对岸」「上一处站稳的地方」这些状态只在这里（和它肚子里的部件）；
// Player、Ship、Farm、Shipyard、Settlement、各面板互不相识，各自只回答自己的事。
class Expedition : public godot::Node3D {
    GDCLASS(Expedition, godot::Node3D)

public:
    enum State {
        ON_FOOT,
        PILOTING,
        MOORING,
    };
    static constexpr const char *MAP_GROUP = "world_map"; // 有岛群地图面板的编排进这个组（开发者工具据此知道这张图按 M 有没有地图）

private:
    godot::NodePath map_path = "Map";
    godot::NodePath ship_path = "Ship"; // 可以不接：那就只能走
    godot::NodePath player_path = "Player";
    int origin_dock = 0; // 地图的码头号
    int destination_dock = 1;
    double helm_range = 2.2; // 离舵多近能上舵；离画样台多近能改船
    double dock_range = 45.0; // 离泊位多近能叫靠泊
    double leave_helm_speed = 3.0; // 半空中离舵：船速须低于此
    double kill_altitude = -30.0; // 掉到这以下算坠入云海
    double reach = 1.8; // 够得着身前（视线方向）多远的那一格地
    double arrive_radius = 500.0; // 在目的码头那座岛上、离码头这么近站稳就算到了
    double recall_distance = 60.0; // 坠落回位时船离落脚点比这远，就召回离那儿最近的码头
    double land_search_m = 3000.0; // 瞬移落点是云海时，往外找多远的陆地
    double focus_lead_s = 3.0; // 掌舵时流式焦点提前船头几秒
    double notice_seconds = 2.5;
    godot::Dictionary starting_pack; // 开局背包：物品 id → 份数
    bool dev_tools = true; // 开发者工具（布置模式、地图瞬移）开不开

    MapHost *map = nullptr;
    Ship *ship = nullptr;
    Player *player = nullptr;
    Helm helm;
    PackHand pack;
    FarmHand farm_hand;
    FreightHand freight;
    RefitBench refit;
    BuildMode build;
    InteriorEdit interior;
    int origin_index = -1; // 地图的码头下标
    int shown_destination = -1; // 地图面板上标成目的的码头号
    int destination_index = -1;
    bool arrived = false;
    bool has_safe_footing = false;
    godot::Vector3 safe_footing;
    godot::Vector3 spawn_point;
    double notice_time = 0.0;
    godot::String notice;
    bool map_open = false;
    bool map_viewed = false;

    godot::Label *status_label = nullptr;
    godot::Label *prompt_label = nullptr;
    godot::Label *notice_label = nullptr;
    WorldMap *map_panel = nullptr;

    Say say_fn() {
        return [this](const godot::String &p_text) { say(p_text); };
    }
    void say(const godot::String &p_text);
    void build_hud();
    void update_hud();
    void sync_helm_rules();
    void board();
    // 开合各个面板：同一时刻只开一个；开着的时候人站住、手上的活作废
    void close_panels();
    void set_pack_open(bool p_open);
    void set_map_open(bool p_open);
    void on_foot(double p_delta, bool p_interact);
    void toggle_interior(const godot::Vector3 &p_at);
    std::vector<Berth> berths() const;
    godot::Vector3 focus_point() const;
    void track_footing();
    int course_target() const; // 航行目标（码头下标）：托运的目的港，否则目的码头 / 回程
    void respawn();
    int nearest_open_dock(const godot::Vector3 &p_point) const;
    bool nearest_land(const godot::Vector3 &p_point, godot::Vector3 &r_point) const;
    godot::String clock_text() const;

public:
    godot::String get_status_text() const; // HUD 左上那几行（给测试看）

private:

protected:
    static void _bind_methods();

public:
    Expedition();

    void _ready() override;
    void _process(double p_delta) override;
    void _physics_process(double p_delta) override;
    void _unhandled_input(const godot::Ref<godot::InputEvent> &p_event) override;

    // 把玩家放到该处（全局 xz；落在地面上）。落点没有地就找最近的陆地；都找不到返回 false
    bool teleport_to(const godot::Vector3 &p_position);
    void on_teleport_requested(const godot::Vector3 &p_position);

    // 给测试与 HUD 查
    State get_state() const { return State(helm.get_state()); }
    bool has_arrived() const { return arrived; }
    godot::String get_notice() const { return notice_time > 0.0 ? notice : godot::String(); }
    godot::String get_prompt() const { return prompt_label ? prompt_label->get_text() : godot::String(); }
    godot::Vector3 get_spawn_point() const { return spawn_point; }
    godot::Transform3D get_origin_berth() const;
    godot::Transform3D get_destination_berth() const;
    bool is_map_open() const { return map_open; }
    WorldMap *get_map() const { return map_panel; }
    godot::Node3D *get_map_node() const { return map ? map->map_node() : nullptr; }
    bool is_working() const { return farm_hand.is_working(); }
    // 跑货（给测试查）
    bool is_carrying() const { return freight.is_holding(); }
    int get_cargo_aboard() const { return freight.cargo_aboard(); }
    bool has_freight_order() const { return freight.is_active(); }
    int get_freight_delivered() const { return freight.get_book().get_delivered(); }
    int get_freight_destination_dock() const { return map ? freight.destination_dock_id(map) : -1; }
    godot::String get_freight_status() const { return map ? freight.status_text(map) : godot::String(); }
    godot::Dictionary get_freight_order() const; // 接下的那一单：from / to（村号）、dock（目的码头号）、item、crates、per_crate、fee；没接单为空
    bool is_pack_open() const { return pack.is_open(); }
    int get_pack_cursor() const { return pack.get_cursor(); }
    godot::String get_held_seed() const { return godot::String::utf8(pack.held_seed().c_str()); }
    int get_item_count(const godot::String &p_item) const { return pack.items().count(play::to_std(p_item)); }
    godot::PackedStringArray get_pack_items() const;
    bool is_refit_open() const { return refit.is_open(); }
    godot::Vector3i get_refit_cursor() const;
    godot::String get_refit_kind() const { return godot::String(modules::def(refit.get_kind()).id); }
    bool is_ship_in_yard() const; // 空艇停在哪座坞的坞位上（改得了）
    bool is_build_open() const { return build.is_open(); }
    godot::Vector3 get_build_cursor() const { return build.is_open() ? build.cursor_snapped() : godot::Vector3(); }
    godot::String get_build_kind() const { return build.get_kind(); }
    int get_build_selected() const { return build.get_selected(); }
    double get_build_yaw_deg() const { return build.get_yaw_deg(); }
    double get_build_camera_yaw() const { return build.get_camera_yaw(); } // 布置视角的相机方位：W 把光标往 (−sin, 0, −cos) 挪
    godot::Node *get_build_target() const { return build.get_target(); } // 布置模式改的是哪个 Settlement
    godot::String get_build_style() const { return build.get_style(); }
    bool is_interior_open() const { return interior.is_open(); }
    godot::Vector3 get_interior_cursor() const { return interior.is_open() ? interior.get_cursor_global() : godot::Vector3(); }
    godot::String get_interior_tool() const { return interior.get_tool_name(); }
    int get_interior_level() const { return interior.get_level(); }
    godot::String get_interior_template() const { return interior.is_open() ? interior.get_template_id() : godot::String(); }
    int get_interior_building() const { return interior.is_open() ? interior.get_index() : -1; }
    double get_interior_camera_yaw() const { return interior.get_camera_yaw(); } // 室内编辑视角的相机方位：W 把光标往 (−sin, 0, −cos) 挪

    void set_map_path(const godot::NodePath &p_value) { map_path = p_value; }
    godot::NodePath get_map_path() const { return map_path; }
    void set_ship_path(const godot::NodePath &p_value) { ship_path = p_value; }
    godot::NodePath get_ship_path() const { return ship_path; }
    void set_player_path(const godot::NodePath &p_value) { player_path = p_value; }
    godot::NodePath get_player_path() const { return player_path; }
    void set_origin_dock(int p_value) { origin_dock = p_value; }
    int get_origin_dock() const { return origin_dock; }
    void set_destination_dock(int p_value) { destination_dock = p_value; }
    int get_destination_dock() const { return destination_dock; }
    void set_helm_range(double p_value) {
        helm_range = p_value;
        sync_helm_rules();
    }
    double get_helm_range() const { return helm_range; }
    void set_dock_range(double p_value) {
        dock_range = p_value;
        sync_helm_rules();
    }
    double get_dock_range() const { return dock_range; }
    void set_leave_helm_speed(double p_value) {
        leave_helm_speed = p_value;
        sync_helm_rules();
    }
    double get_leave_helm_speed() const { return leave_helm_speed; }
    void set_kill_altitude(double p_value) { kill_altitude = p_value; }
    double get_kill_altitude() const { return kill_altitude; }
    void set_reach(double p_value) { reach = p_value; }
    double get_reach() const { return reach; }
    void set_arrive_radius(double p_value) { arrive_radius = p_value; }
    double get_arrive_radius() const { return arrive_radius; }
    void set_recall_distance(double p_value) { recall_distance = p_value; }
    double get_recall_distance() const { return recall_distance; }
    void set_notice_seconds(double p_value) { notice_seconds = p_value; }
    double get_notice_seconds() const { return notice_seconds; }
    void set_starting_pack(const godot::Dictionary &p_value) { starting_pack = p_value; }
    godot::Dictionary get_starting_pack() const { return starting_pack; }
    void set_dev_tools(bool p_value);
    bool get_dev_tools() const { return dev_tools; }
};

VARIANT_ENUM_CAST(Expedition::State);
