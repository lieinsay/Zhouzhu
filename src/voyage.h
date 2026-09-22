#pragma once

#include <string>

#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/node_path.hpp>

#include "backpack_panel.h"
#include "farm.h"
#include "inventory.h"
#include "island.h"
#include "player.h"
#include "refit_overlay.h"
#include "refit_panel.h"
#include "ship.h"
#include "shipyard.h"

// 主场景的编排。切片一：走上空艇 → 掌舵 → 飞到对岸 → 靠泊下船；切片二：步行时在河边开垦、播种、收获；
// 拓展：种子从背包里选（Tab 开合背包，W/S 选、E 拿在手上），收成进背包；
// 切片三：掌舵时靠近船坞按 E 入坞；下船走到画样台按 E 改装；没有动力区的船上不了舵。
// 拓展：改装在装配视角里做——环绕船的相机（鼠标转），三维格子里挪光标（W/S 前后、A/D 左右、空格/Shift 上下层），
// 1/2/3 选模块，E 放一节或换类型，X 拆掉，Tab 合上；每改一下船立刻重拼并在坞里重新坐定。
// 「现在是谁在受控」「手上的活干到哪了」「背包里有什么、手上拿着哪种种子、背包开没开」「改装开没开、光标在哪一格、选了哪种模块」这些状态只在这里：
// Player、Ship、Farm、Shipyard、BackpackPanel、RefitPanel、RefitOverlay 互不相识，各自只回答自己的事。
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
    godot::NodePath shipyard_path = "IslandA/Shipyard"; // 可以不接：那就没处改船
    double helm_range = 2.2; // 离舵多近能上舵；离画样台多近能改船
    double dock_range = 45.0; // 离泊位多近能叫靠泊
    double leave_helm_speed = 3.0; // 半空中离舵：船速须低于此
    double kill_altitude = -30.0; // 掉到这以下算坠入云海
    double reach = 1.8; // 够得着身前（视线方向）多远的那一格地
    godot::Dictionary starting_pack; // 开局背包：物品 id → 份数

    State state = ON_FOOT;
    bool moor_to_yard = false; // 正在滑向的是坞位（不是泊位）
    bool arrived = false;
    bool has_safe_footing = false;
    godot::Vector3 safe_footing;
    double notice_time = 0.0;
    godot::String notice;
    // 背包：内容、手上拿着的种子（物品 id，空 = 没拿）、面板开合与光标
    Inventory pack;
    std::string held_seed;
    bool pack_open = false;
    int pack_cursor = 0;
    // 改装：开合、光标格、选了哪种模块；布局本身在 Ship 里，每一步直接改它
    bool refit_open = false;
    Cell refit_cursor;
    ModuleKind refit_kind = ModuleKind::STORAGE;
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
    Shipyard *shipyard = nullptr;
    godot::Label *status_label = nullptr;
    godot::Label *prompt_label = nullptr;
    BackpackPanel *pack_panel = nullptr;
    RefitPanel *refit_panel = nullptr;
    RefitOverlay *refit_overlay = nullptr;

    void build_hud();
    void update_hud();
    void board();
    void disembark();
    void seat_player();
    void set_pack_open(bool p_open);
    void browse_pack(bool p_interact);
    void set_refit_open(bool p_open);
    void browse_refit(bool p_interact);
    // 光标格眼下是什么 / 能不能放选中的模块；返回说明，r_state 给装配视角
    godot::String refit_cursor_status(RefitOverlay::CursorState &r_state) const;
    void refresh_refit_view();
    bool near_bench() const;
    void interact_at_bench();
    bool has_seed_in_hand() const { return !held_seed.empty() && pack.has(held_seed); }
    void tend_farm(double p_delta, bool p_interact);
    void finish_work();
    void drop_work();
    void say(const godot::String &p_notice);
    godot::String refusal(const godot::Vector2i &p_cell) const;
    void track_footing();
    void respawn();
    // dock_range 内最近的泊位 / 坞位；r_yard 说是不是坞位。没有返回 false
    bool moor_target_in_range(godot::Transform3D &r_target, bool &r_yard) const;
    Island *nearest_island(const godot::Vector3 &p_position) const;

protected:
    static void _bind_methods();

public:
    Voyage();

    void _ready() override;
    void _physics_process(double p_delta) override;
    void _unhandled_input(const godot::Ref<godot::InputEvent> &p_event) override;

    State get_state() const { return state; }
    bool has_arrived() const { return arrived; }
    bool is_working() const { return work_action != Farm::ACTION_NONE; }
    bool is_pack_open() const { return pack_open; }
    int get_pack_cursor() const { return pack_cursor; }
    bool is_refit_open() const { return refit_open; }
    godot::Vector3i get_refit_cursor() const { return godot::Vector3i(refit_cursor.x, refit_cursor.y, refit_cursor.z); }
    godot::String get_refit_kind() const { return godot::String(modules::def(refit_kind).id); }
    bool is_ship_in_yard() const; // 空艇停在坞位上（改得了）
    godot::String get_notice() const { return notice_time > 0.0 ? notice : godot::String(); }
    godot::String get_held_seed() const; // 手上拿着的种子（物品 id）；没拿返回空串
    int get_item_count(const godot::String &p_item) const;
    godot::PackedStringArray get_pack_items() const; // 背包里的物品 id，按面板里的顺序

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
    void set_shipyard_path(const godot::NodePath &p_path) { shipyard_path = p_path; }
    godot::NodePath get_shipyard_path() const { return shipyard_path; }
    void set_reach(double p_value) { reach = p_value; }
    double get_reach() const { return reach; }
    void set_starting_pack(const godot::Dictionary &p_value) { starting_pack = p_value; }
    godot::Dictionary get_starting_pack() const { return starting_pack; }
};

VARIANT_ENUM_CAST(Voyage::State);
